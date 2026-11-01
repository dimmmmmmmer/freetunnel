// cppcheck-suppress-file missingIncludeSystem
#include "ConfigTomlSyntax.h"
#include <QRegularExpression>

namespace freetunnel::toml_syntax {
QString tomlEsc(const QString &s) {
    // Escape backslash/quote and strip C0 control characters (newlines, tabs, …)
    // and DEL so a field value can't break out of its quoted TOML string.
    QString o;
    o.reserve(s.size());
    for (const QChar &c : s) {
        if (c < QChar(0x20) || c == QChar(0x7F))
            continue;
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            o += QLatin1Char('\\');
        o += c;
    }
    return o;
}


// How much of a value one line leaves unfinished.
//
// Only """ was tracked before, because """ is what this editor writes. A file it
// did not write is under no obligation to agree: an array or an inline table can
// span lines, and so can a ''' literal block. The result was not a misread but a
// rewrite - a continuation line carries no `=`, so it was taken for the end of
// the value and dropped along with the bracket that closed it, and the config
// came back as `exclusions = [` and nothing else. That file no longer parses,
// and it is the text handed to the root helper on the next connect.


// Step past a single-line quoted string, and say where it ends.
int endOfQuoted(const QString &line, int i)
{
    const QChar quote = line.at(i);
    const int n = line.size();
    ++i;
    while (i < n && line.at(i) != quote) {
        // Only a basic string has escapes; inside '...' a backslash is a
        // backslash, which is the entire point of the literal spelling.
        if (quote == QLatin1Char('"') && line.at(i) == QLatin1Char('\\'))
            ++i;
        ++i;
    }
    return i + 1;
}

// Inside a multi-line string already: step to just past its closing delimiter,
// or to the end of the text when it does not close in it.
//
// A basic block has escapes, as a one-line basic string does: \" is a quote of
// its text, so \""" does not close it. Taken for the close, the block ended
// early, and the next """ opened one that TOML says is closed, hiding whatever
// came after it - an [endpoint] header among the rest. Inside ''' a backslash is
// only a backslash.
int endOfOpenMultiline(OpenValue *v, const QString &text, int i)
{
    const QLatin1String fence =
            v->basic ? QLatin1String("\"\"\"") : QLatin1String("'''");
    const int n = text.size();
    while (i < n) {
        if (v->basic && text.at(i) == QLatin1Char('\\')) {
            i += 2;
            continue;
        }
        if (QStringView(text).mid(i).startsWith(fence)) {
            v->basic = false;
            v->literal = false;
            return i + 3;
        }
        ++i;
    }
    return n; // all of what is left belongs to the string
}

void countBracket(OpenValue *v, QChar c)
{
    if (c == QLatin1Char('['))
        ++v->brackets;
    else if (c == QLatin1Char(']') && v->brackets > 0)
        --v->brackets;
    else if (c == QLatin1Char('{')) {
        ++v->braces;
        v->inlineTable = true;
    } else if (c == QLatin1Char('}') && v->braces > 0)
        --v->braces;
}

// Walk one line, updating what it leaves open. Quoted text is stepped over, so a
// bracket inside a string does not count - an IPv6 address is nothing but
// brackets - and a # outside a string ends the line.
void advanceOpenValue(OpenValue &v, const QString &line)
{
    const int n = line.size();
    int i = 0;
    while (i < n) {
        if (v.basic || v.literal) {
            i = endOfOpenMultiline(&v, line, i);
            continue;
        }
        const QStringView rest = QStringView(line).mid(i);
        if (rest.startsWith(QLatin1String("\"\"\""))) {
            v.basic = true;
            i += 3;
            continue;
        }
        if (rest.startsWith(QLatin1String("'''"))) {
            v.literal = true;
            i += 3;
            continue;
        }
        const QChar c = line.at(i);
        if (c == QLatin1Char('#'))
            return;
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            i = endOfQuoted(line, i);
            continue;
        }
        countBracket(&v, c);
        ++i;
    }
}

// Where the comment on a line starts, or -1 when it has none. A # inside a quoted
// string is part of the string.
int commentStart(const QString &line)
{
    int i = 0;
    while (i < line.size()) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('#'))
            return i;
        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
            i = endOfQuoted(line, i);
        else
            ++i;
    }
    return -1;
}

// A key as the core names it. TOML lets any key be quoted, and `"password"` or
// `'password'` is the same key as `password`. Compared as written, a quoted key
// was neither read nor known: a quoted password was carried over as an unknown
// key, never moved to the credential store and never used, and once one was
// stored anyway the text sent to the core named the password twice, which it
// refuses. Escapes are not decoded: a name spelled with one stays unknown, and is
// carried over as written.
QString unquotedKey(const QString &key)
{
    if (key.size() < 2)
        return key;
    const QChar quote = key.at(0);
    if ((quote != QLatin1Char('"') && quote != QLatin1Char('\'')) || !key.endsWith(quote))
        return key;
    return key.mid(1, key.size() - 2);
}

// A table's name as the core names it. Like a key, each dotted part of it may be
// quoted: ["endpoint"] is [endpoint], and ["listener".tun] is [listener.tun].
// Compared as written, a quoted [endpoint] was not the endpoint, so every one of
// its settings read empty and the import failed. A part is unquoted only where
// the quotes change nothing; one that needs them keeps the name as written, and
// that is right, not merely safe: ["listener.tun"] is a table of its own whose
// name has a dot in it, not [listener.tun].
QString tableName(const QString &written)
{
    static const QRegularExpression bare(QStringLiteral("^[A-Za-z0-9_-]+$"));
    QStringList parts;
    for (const QString &part : written.split(QLatin1Char('.'))) {
        const QString key = unquotedKey(part.trimmed());
        if (!bare.match(key).hasMatch())
            return written;
        parts << key;
    }
    return parts.join(QLatin1Char('.'));
}

// Whether a line opens a table, and which. A header may end in a comment,
// `[endpoint] # main server`, as legally as any other line may. One that did was
// not taken for a header, so its keys stayed in the table before it, the root -
// and `password` is not a root key this editor writes, so it was carried over as
// an unknown one: written back in plain text on every rewrite, and read again as
// the password each time the credential store had just been given it.
bool isTableHeader(const QString &line, QString *name)
{
    QString t = line.trimmed();
    if (!t.startsWith(QLatin1Char('[')))
        return false;
    const int comment = commentStart(t);
    if (comment >= 0)
        t = t.left(comment).trimmed();
    if (!t.endsWith(QLatin1Char(']')))
        return false;
    *name = tableName(t.mid(1, t.size() - 2).trimmed());
    return true;
}

// Split a TOML document into its top-level tables: pairs of (header, body), with
// an empty header for the keys that precede the first table. Multi-line basic
// strings are stepped over, so a certificate block whose content happens to look
// like a table header or a key is never mistaken for one — and a pasted PEM is
// exactly the kind of value that could.
QList<QPair<QString, QString>> splitTomlTables(const QString &toml)
{
    QList<QPair<QString, QString>> out;
    QString header;
    QString body;
    OpenValue open;
    const QStringList lines = toml.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (open.open()) {
            body += line + QLatin1Char('\n');
            advanceOpenValue(open, line);
            continue;
        }
        QString name;
        if (isTableHeader(line, &name)) {
            out.append({header, body});
            header = name;
            body.clear();
            continue;
        }
        body += line + QLatin1Char('\n');
        advanceOpenValue(open, line);
    }
    out.append({header, body});
    return out;
}

// A captured table body ends with however many blank lines the file had before
// the next header. Re-emitting them verbatim adds one more every save, so the
// file grows without bound and no round trip is ever stable.
QString normalizeBody(QString body)
{
    while (body.endsWith(QLatin1Char('\n')))
        body.chop(1);
    return body.isEmpty() ? body : body + QLatin1Char('\n');
}

QString keyOf(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
        return QString();
    const int eq = t.indexOf(QLatin1Char('='));
    return eq < 0 ? QString() : unquotedKey(t.left(eq).trimmed());
}

// The lines of `body` whose key this editor does not write, with any multi-line
// value kept whole. Blank lines and comments are dropped: they belong to the key
// they sit next to, and there is no way to tell which one that is.
QString unknownKeyLines(const QString &body, const QStringList &known)
{
    QString out;
    bool keeping = false;
    OpenValue open;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (open.open()) {
            if (keeping)
                out += line + QLatin1Char('\n');
            advanceOpenValue(open, line);
            continue;
        }
        const QString key = keyOf(line);
        if (key.isEmpty()) {
            keeping = false;
            continue;
        }
        keeping = !known.contains(key);
        if (keeping)
            out += line + QLatin1Char('\n');
        advanceOpenValue(open, line);
    }
    return out;
}

const QStringList &knownEndpointKeys()
{
    static const QStringList k{
            QStringLiteral("hostname"),     QStringLiteral("addresses"),
            QStringLiteral("username"),     QStringLiteral("password"),
            QStringLiteral("client_random"), QStringLiteral("custom_sni"),
            // Written by 1.2.2 and earlier, never read by the core: merged into
            // client_random on load (see parseConfigToml), and not carried over.
            QStringLiteral("client_random_mask"),
            QStringLiteral("has_ipv6"),     QStringLiteral("skip_verification"),
            QStringLiteral("upstream_protocol"), QStringLiteral("anti_dpi"),
            QStringLiteral("certificate"),
            // Read from here when the endpoint has it, and written at the root,
            // where the core reads it next (see readDns).
            QStringLiteral("dns_upstreams")};
    return k;
}

const QStringList &knownRootKeys()
{
    // The endpoint's keys as well. At the root they mean nothing to the core,
    // which reads them from [endpoint] alone, and the one way they got there is
    // 1.2.2 missing a header with a comment after it (see isTableHeader) and
    // copying that table's keys up a level, password included. Not carried over,
    // they are gone after the next rewrite.
    static const QStringList k = QStringList{QStringLiteral("loglevel"), QStringLiteral("vpn_mode"),
                                             QStringLiteral("killswitch_enabled"),
                                             QStringLiteral("post_quantum_group_enabled"),
                                             QStringLiteral("dns_upstreams")}
            + knownEndpointKeys();
    return k;
}

}
