// cppcheck-suppress-file missingIncludeSystem
#include "ConfigToml.h"

#include <QRegularExpression>
#include <QStringList>

#include <optional>

namespace freetunnel {

static QString tomlEsc(const QString &s) {
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

// Escape a value for a multi-line basic string (""" … """). Same job as
// tomlEsc(), minus the newline stripping a PEM block needs: escaping every quote
// means the content can never contain the """ that would close the string early
// and let a pasted "certificate" inject arbitrary TOML into the file the ROOT
// helper later parses.
static QString tomlEscMultiline(const QString &s) {
    QString o;
    o.reserve(s.size());
    for (const QChar &c : s) {
        if (c == QLatin1Char('\r'))
            continue; // CRLF from a pasted PEM: keep the \n, drop the \r
        if (c == QLatin1Char('\n')) {
            o += c;
            continue;
        }
        if (c < QChar(0x20) || c == QChar(0x7F))
            continue;
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            o += QLatin1Char('\\');
        o += c;
    }
    return o;
}

static QString listToTomlArray(const QStringList &values) {
    QStringList items;
    for (const QString &raw : values) {
        const QString v = raw.trimmed();
        if (!v.isEmpty())
            items << QStringLiteral("\"%1\"").arg(tomlEsc(v));
    }
    return items.join(QStringLiteral(", "));
}

static QString csvToTomlArray(const QString &csv) {
    return listToTomlArray(csv.split(',', Qt::SkipEmptyParts));
}

// clientRandomForCore / isValidClientRandom / splitDnsList live in
// ConfigTomlValues.cpp.

namespace {

// How much of a value one line leaves unfinished.
//
// Only """ was tracked before, because """ is what this editor writes. A file it
// did not write is under no obligation to agree: an array or an inline table can
// span lines, and so can a ''' literal block. The result was not a misread but a
// rewrite - a continuation line carries no `=`, so it was taken for the end of
// the value and dropped along with the bracket that closed it, and the config
// came back as `exclusions = [` and nothing else. That file no longer parses,
// and it is the text handed to the root helper on the next connect.
struct OpenValue {
    bool basic = false;   // inside """ ... """
    bool literal = false; // inside ''' ... '''
    int brackets = 0;     // [ ... ] depth
    int braces = 0;       // { ... } depth
    bool open() const { return basic || literal || brackets > 0 || braces > 0; }
};

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
// or to the end of the line when it does not close here.
int endOfOpenMultiline(OpenValue *v, const QString &line, int i)
{
    const QLatin1String fence =
            v->basic ? QLatin1String("\"\"\"") : QLatin1String("'''");
    const int end = line.indexOf(fence, i);
    if (end < 0)
        return line.size(); // all of what is left belongs to the string
    v->basic = false;
    v->literal = false;
    return end + 3;
}

void countBracket(OpenValue *v, QChar c)
{
    if (c == QLatin1Char('['))
        ++v->brackets;
    else if (c == QLatin1Char(']') && v->brackets > 0)
        --v->brackets;
    else if (c == QLatin1Char('{'))
        ++v->braces;
    else if (c == QLatin1Char('}') && v->braces > 0)
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

} // namespace

QString buildConfigToml(const ConfigToml &c, const QString &logLevel) {
    QString t;
    t += QStringLiteral("loglevel = \"%1\"\n").arg(logLevel);
    t += QStringLiteral("vpn_mode = \"general\"\n");
    t += QStringLiteral("killswitch_enabled = false\n");
    t += QStringLiteral("post_quantum_group_enabled = %1\n").arg(c.postQuantum ? "true" : "false");
    t += QStringLiteral("dns_upstreams = [%1]\n").arg(listToTomlArray(splitDnsList(c.dns)));
    t += c.extraRootKeys;
    t += QStringLiteral("\n[endpoint]\n");
    t += QStringLiteral("hostname = \"%1\"\n").arg(tomlEsc(c.hostname));
    t += QStringLiteral("addresses = [%1]\n").arg(csvToTomlArray(c.addresses));
    t += QStringLiteral("username = \"%1\"\n").arg(tomlEsc(c.username));
    if (!c.password.isEmpty())
        t += QStringLiteral("password = \"%1\"\n").arg(tomlEsc(c.password));
    t += QStringLiteral("client_random = \"%1\"\n").arg(tomlEsc(clientRandomForCore(c.clientRandom)));
    t += QStringLiteral("custom_sni = \"%1\"\n").arg(tomlEsc(c.customSni));
    t += QStringLiteral("has_ipv6 = %1\n").arg(c.allowIpv6 ? "true" : "false");
    t += QStringLiteral("skip_verification = %1\n").arg(c.skipVerification ? "true" : "false");
    t += QStringLiteral("upstream_protocol = \"%1\"\n").arg(c.protocol == "http3" ? "http3" : "http2");
    t += QStringLiteral("anti_dpi = %1\n").arg(c.antiDpi ? "true" : "false");
    if (!c.certificate.trimmed().isEmpty())
        t += QStringLiteral("certificate = \"\"\"\n%1\n\"\"\"\n")
                     .arg(tomlEscMultiline(c.certificate.trimmed()));
    else
        t += QStringLiteral("certificate = \"\"\n");
    t += c.extraEndpointKeys;
    t += QStringLiteral("\n[listener.tun]\n");
    if (!c.tunSection.isEmpty()) {
        // The file already said how it wants to be routed. Overwriting that with
        // our defaults is how an imported config quietly lost its routing.
        t += c.tunSection;
    } else {
        t += QStringLiteral("bound_if = \"\"\nmtu_size = 1500\nchange_system_dns = true\n");
        t += QStringLiteral("included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n");
        t += QStringLiteral("excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", "
                            "\"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n");
    }
    t += c.extraSections;
    return t;
}

// Carry over everything ConfigToml has no field for, so buildConfigToml() can put
// it back. Without this the round trip is lossy in a way nobody sees: the file
// still parses, still connects, and quietly routes differently.
static void carryOverUnknownTables(const QList<QPair<QString, QString>> &tables, ConfigToml &c) {
    for (const auto &table : tables) {
        const QString &header = table.first;
        const QString &body = table.second;
        if (header.isEmpty()) {
            c.extraRootKeys = unknownKeyLines(body, knownRootKeys());
        } else if (header == QLatin1String("endpoint")) {
            c.extraEndpointKeys = unknownKeyLines(body, knownEndpointKeys());
        } else if (header == QLatin1String("listener.tun")) {
            c.tunSection = normalizeBody(body);
        } else if (header == QLatin1String("listener") || header.startsWith(QLatin1String("listener."))) {
            // Every other listener is left out. FreeTunnel runs the tunnel, and
            // buildConfigToml() always writes [listener.tun]: a [listener.socks]
            // kept beside it, as TrustTunnel's own client sets up a local proxy,
            // named two listeners, and the core refuses such a config outright.
            // [listener] itself holds nothing else the core reads.
        } else {
            c.extraSections += QStringLiteral("\n[%1]\n").arg(header) + normalizeBody(body);
        }
    }
}

namespace {

// Character by character rather than two chained replaces. The old pair ran \"
// first and \\ second, so a value ending in an escaped backslash was decoded by
// the wrong rule; and neither knew about \n, which is the only way a newline can
// appear in a single-line basic string — the spelling a certificate arrives in
// when the file did not come from here.
QString unescapeBasic(const QString &v)
{
    QString o;
    o.reserve(v.size());
    for (int i = 0; i < v.size(); ++i) {
        const QChar ch = v.at(i);
        if (ch != QLatin1Char('\\') || i + 1 >= v.size()) {
            o += ch;
            continue;
        }
        const QChar next = v.at(++i);
        switch (next.unicode()) {
        case 'n': o += QLatin1Char('\n'); break;
        case 't': o += QLatin1Char('\t'); break;
        case 'r': break; // a CR is not content anything here wants
        case '"': o += QLatin1Char('"'); break;
        case '\\': o += QLatin1Char('\\'); break;
        default: o += QLatin1Char('\\'); o += next; break; // leave the rest alone
        }
    }
    return o;
}

// Where a key's value starts in the body of one table: just past the `=` of the
// line that sets it, or -1 when the table does not set it.
//
// Per table, because the core reads each key from one table and no other. Read
// from anywhere in the file, another table's `password` - a SOCKS listener's -
// was taken for the server's, and once the real one had gone to the credential
// store it was the only one left, so it was stored over the VPN password on every
// connect. A key may also be indented, as TOML allows: only keys at the start of
// a line were found, and as keys the rebuild writes they were not carried over
// either, so an indented certificate came back empty, an indented address list
// failed the import, and an indented password was neither moved to the store nor
// used to connect. A line inside another key's multi-line value is stepped over,
// so a certificate's text cannot pass for a key, indented or not.
int valueStart(const QString &body, const char *key)
{
    OpenValue open;
    int pos = 0;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!open.open() && keyOf(line) == QLatin1String(key))
            return pos + line.indexOf(QLatin1Char('=')) + 1;
        advanceOpenValue(open, line);
        pos += line.size() + 1;
    }
    return -1;
}

// `value` matched where `key`'s value starts in `body`, and nowhere else.
QRegularExpressionMatch matchValue(const QString &body, const char *key, const QRegularExpression &value)
{
    const int at = valueStart(body, key);
    if (at < 0)
        return QRegularExpressionMatch();
    return value.match(body, at, QRegularExpression::NormalMatch,
                       QRegularExpression::AnchorAtOffsetMatchOption);
}

// A value written as a block, """...""" or '''...''', on one line or several.
// Tried before the one-line spellings, which match the first two quotes of the
// three and read an empty string: `password = """secret"""` lost the password,
// which the rewrite then dropped from the file. A newline straight after the
// opening quotes is not part of the value, as in TOML, and neither is one
// straight before the closing quotes, which this editor writes there.
std::optional<QString> readBlock(const QString &table, const char *key)
{
    static const QRegularExpression basicBlock(
            QStringLiteral("\\s*\"\"\"\\n?(.*?)\\n?\"\"\""),
            QRegularExpression::DotMatchesEverythingOption);
    const auto bm = matchValue(table, key, basicBlock);
    // Same unescaping as the quoted fields: buildConfigToml() escapes quotes and
    // backslashes in the block, and a PEM (which has neither) still round trips
    // byte for byte.
    if (bm.hasMatch())
        return unescapeBasic(bm.captured(1));
    static const QRegularExpression literalBlock(
            QStringLiteral("\\s*'''\\n?(.*?)\\n?'''"),
            QRegularExpression::DotMatchesEverythingOption);
    const auto lm = matchValue(table, key, literalBlock);
    // A literal block is literal: no escape is processed inside one.
    if (lm.hasMatch())
        return lm.captured(1);
    return std::nullopt;
}

// Every spelling of a TOML string. This editor writes "...", but a file it did
// not write is free to use '...', where nothing is an escape, or either kind of
// block. Reading one of those as "absent" did not merely skip it: these are keys
// the rebuild writes itself, so the value it could not read was replaced with an
// empty one and the config was emptied in place.
QString readString(const QString &table, const char *key)
{
    if (const std::optional<QString> block = readBlock(table, key))
        return *block;
    static const QRegularExpression basic(QStringLiteral("\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    const auto bm = matchValue(table, key, basic);
    if (bm.hasMatch())
        return unescapeBasic(bm.captured(1));
    static const QRegularExpression literal(QStringLiteral("\\s*'([^']*)'"));
    const auto lm = matchValue(table, key, literal);
    return lm.hasMatch() ? lm.captured(1) : QString();
}

// Where the array opened at `start` closes, or -1 when it never does. Quoted
// items and comments are stepped over so a bracket inside either does not count.
int endOfArray(const QString &toml, int start)
{
    const int n = toml.size();
    int depth = 1;
    int i = start;
    while (i < n && depth > 0) {
        const QChar ch = toml.at(i);
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            const QChar quote = ch;
            ++i;
            while (i < n && toml.at(i) != quote) {
                if (quote == QLatin1Char('"') && toml.at(i) == QLatin1Char('\\'))
                    ++i;
                ++i;
            }
            ++i;
            continue;
        }
        if (ch == QLatin1Char('#')) {
            while (i < n && toml.at(i) != QLatin1Char('\n'))
                ++i;
            continue;
        }
        if (ch == QLatin1Char('['))
            ++depth;
        else if (ch == QLatin1Char(']'))
            --depth;
        ++i;
    }
    return depth == 0 ? i - 1 : -1;
}

// An array as the CSV the form holds, from the opening [ to the ] that matches
// it, however many lines that takes. A provider formats an array one entry per
// line as readily as on one, and the single-line read that was here returned
// nothing for the other spelling — which, for `addresses`, is the whole config.
QString readArray(const QString &table, const char *key)
{
    static const QRegularExpression open(QStringLiteral("\\s*\\["));
    const auto m = matchValue(table, key, open);
    if (!m.hasMatch())
        return QString();
    const int start = m.capturedEnd();
    const int close = endOfArray(table, start);
    if (close < 0)
        return QString();
    static const QRegularExpression item(
            QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\"|'([^']*)'"));
    QStringList out;
    auto it = item.globalMatch(table.mid(start, close - start));
    while (it.hasNext()) {
        const auto im = it.next();
        out << (im.capturedStart(1) >= 0 ? unescapeBasic(im.captured(1)) : im.captured(2));
    }
    return out.join(QStringLiteral(", "));
}

// Read on the flag's own line only (see valueStart), so a `true`/`false` token
// sitting inside the certificate block or a comment can't flip a flag
// (skip_verification in particular is security-significant — it disables server
// cert checking).
bool readBool(const QString &table, const char *key, bool dflt)
{
    static const QRegularExpression re(QStringLiteral("\\s*(true|false)\\b"));
    const auto m = matchValue(table, key, re);
    return m.hasMatch() ? (m.captured(1) == QLatin1String("true")) : dflt;
}

// The body of the first table of that name; the root is the one with none.
QString tableBody(const QList<QPair<QString, QString>> &tables, const QString &name)
{
    for (const auto &table : tables) {
        if (table.first == name)
            return table.second;
    }
    return QString();
}

// The endpoint's password, or failing that one left at the root, where 1.2.2 put
// it when it missed a header with a comment after it (see isTableHeader). It is
// still the password: it goes to the credential store like any other, and the
// rewrite that follows does not carry it over (see knownRootKeys).
QString readPassword(const QString &root, const QString &endpoint)
{
    const QString own = readString(endpoint, "password");
    return own.isEmpty() ? readString(root, "password") : own;
}

// The DNS list where the core looks for it: [endpoint] first, and the root only
// when the endpoint has no list. TrustTunnel's own template puts it in [endpoint];
// this editor writes it at the root, which the core reads next, so the endpoint's
// copy is not carried over (see knownEndpointKeys) and the list written back is
// the one the core uses. Kept, it would have overridden any change made here.
QString readDns(const QString &root, const QString &endpoint)
{
    static const QRegularExpression list(QStringLiteral("\\s*\\["));
    const bool own = matchValue(endpoint, "dns_upstreams", list).hasMatch();
    return readArray(own ? endpoint : root, "dns_upstreams");
}

// A config imported from a link by 1.2.2 or earlier has the mask under a key of
// its own, which the core never read: the connection went out without it. Joined
// back on here, which is also the path to the core, so such a config works again
// without being opened. A mask with no prefix had nothing to mask and is dropped,
// as the core effectively did.
QString readClientRandom(const QString &endpoint)
{
    const QString prefix = readString(endpoint, "client_random");
    const QString mask = readString(endpoint, "client_random_mask").trimmed();
    if (!mask.isEmpty() && !prefix.trimmed().isEmpty() && !prefix.contains(QLatin1Char('/')))
        return clientRandomForCore(prefix.trimmed() + QLatin1Char('/') + mask);
    return clientRandomForCore(prefix);
}

} // namespace

// Each key from the table the core reads it from (see valueStart).
ConfigToml parseConfigToml(const QString &toml) {
    ConfigToml c;
    const QList<QPair<QString, QString>> tables = splitTomlTables(toml);
    const QString root = tableBody(tables, QString());
    const QString endpoint = tableBody(tables, QStringLiteral("endpoint"));
    c.hostname = readString(endpoint, "hostname");
    c.addresses = readArray(endpoint, "addresses");
    c.username = readString(endpoint, "username");
    c.password = readPassword(root, endpoint);
    c.protocol = readString(endpoint, "upstream_protocol");
    if (c.protocol.isEmpty())
        c.protocol = QStringLiteral("http2");
    c.dns = readDns(root, endpoint);
    c.postQuantum = readBool(root, "post_quantum_group_enabled", true);
    c.customSni = readString(endpoint, "custom_sni");
    c.clientRandom = readClientRandom(endpoint);
    c.allowIpv6 = readBool(endpoint, "has_ipv6", true);
    c.skipVerification = readBool(endpoint, "skip_verification", false);
    c.antiDpi = readBool(endpoint, "anti_dpi", false);
    // In every spelling (see readString): the rebuild writes it itself, so one it
    // could not read was written back empty, and the pinned trust anchor was gone.
    c.certificate = readString(endpoint, "certificate");

    carryOverUnknownTables(tables, c);
    return c;
}
} // namespace freetunnel
