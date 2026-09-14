// cppcheck-suppress-file missingIncludeSystem
#include "ConfigToml.h"

#include <QRegularExpression>
#include <QStringList>

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

static QString csvToTomlArray(const QString &csv) {
    QStringList items;
    for (const QString &raw : csv.split(',', Qt::SkipEmptyParts)) {
        const QString v = raw.trimmed();
        if (!v.isEmpty())
            items << QStringLiteral("\"%1\"").arg(tomlEsc(v));
    }
    return items.join(QStringLiteral(", "));
}

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

// Walk one line, updating what it leaves open. Quoted text is stepped over, so a
// bracket inside a string does not count - an IPv6 address is nothing but
// brackets - and a # outside a string ends the line.
void advanceOpenValue(OpenValue &v, const QString &line)
{
    const int n = line.size();
    int i = 0;
    while (i < n) {
        if (v.basic) {
            const int end = line.indexOf(QLatin1String("\"\"\""), i);
            if (end < 0)
                return;
            v.basic = false;
            i = end + 3;
            continue;
        }
        if (v.literal) {
            const int end = line.indexOf(QLatin1String("'''"), i);
            if (end < 0)
                return;
            v.literal = false;
            i = end + 3;
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
            const QChar quote = c;
            ++i;
            while (i < n && line.at(i) != quote) {
                // Only a basic string has escapes; inside '...' a backslash is a
                // backslash, which is the entire point of the literal spelling.
                if (quote == QLatin1Char('"') && line.at(i) == QLatin1Char('\\'))
                    ++i;
                ++i;
            }
            ++i;
            continue;
        }
        if (c == QLatin1Char('['))
            ++v.brackets;
        else if (c == QLatin1Char(']') && v.brackets > 0)
            --v.brackets;
        else if (c == QLatin1Char('{'))
            ++v.braces;
        else if (c == QLatin1Char('}') && v.braces > 0)
            --v.braces;
        ++i;
    }
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
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
            out.append({header, body});
            header = t.mid(1, t.size() - 2).trimmed();
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
    return eq < 0 ? QString() : t.left(eq).trimmed();
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

const QStringList &knownRootKeys()
{
    static const QStringList k{QStringLiteral("loglevel"), QStringLiteral("vpn_mode"),
                               QStringLiteral("killswitch_enabled"),
                               QStringLiteral("post_quantum_group_enabled"),
                               QStringLiteral("dns_upstreams")};
    return k;
}

const QStringList &knownEndpointKeys()
{
    static const QStringList k{
            QStringLiteral("hostname"),     QStringLiteral("addresses"),
            QStringLiteral("username"),     QStringLiteral("password"),
            QStringLiteral("client_random"), QStringLiteral("custom_sni"),
            QStringLiteral("has_ipv6"),     QStringLiteral("skip_verification"),
            QStringLiteral("upstream_protocol"), QStringLiteral("anti_dpi"),
            QStringLiteral("certificate")};
    return k;
}

} // namespace

QString buildConfigToml(const ConfigToml &c, const QString &logLevel) {
    QString t;
    t += QStringLiteral("loglevel = \"%1\"\n").arg(logLevel);
    t += QStringLiteral("vpn_mode = \"general\"\n");
    t += QStringLiteral("killswitch_enabled = false\n");
    t += QStringLiteral("post_quantum_group_enabled = true\n");
    t += QStringLiteral("dns_upstreams = [%1]\n").arg(csvToTomlArray(c.dns));
    t += c.extraRootKeys;
    t += QStringLiteral("\n[endpoint]\n");
    t += QStringLiteral("hostname = \"%1\"\n").arg(tomlEsc(c.hostname));
    t += QStringLiteral("addresses = [%1]\n").arg(csvToTomlArray(c.addresses));
    t += QStringLiteral("username = \"%1\"\n").arg(tomlEsc(c.username));
    if (!c.password.isEmpty())
        t += QStringLiteral("password = \"%1\"\n").arg(tomlEsc(c.password));
    t += QStringLiteral("client_random = \"%1\"\n").arg(tomlEsc(c.clientRandom));
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
static void carryOverUnknownTables(const QString &toml, ConfigToml &c) {
    for (const auto &table : splitTomlTables(toml)) {
        const QString &header = table.first;
        const QString &body = table.second;
        if (header.isEmpty()) {
            c.extraRootKeys = unknownKeyLines(body, knownRootKeys());
        } else if (header == QLatin1String("endpoint")) {
            c.extraEndpointKeys = unknownKeyLines(body, knownEndpointKeys());
        } else if (header == QLatin1String("listener.tun")) {
            c.tunSection = normalizeBody(body);
        } else {
            c.extraSections += QStringLiteral("\n[%1]\n").arg(header) + normalizeBody(body);
        }
    }
}

ConfigToml parseConfigToml(const QString &toml) {
    ConfigToml c;
    // Character by character rather than two chained replaces. The old pair ran
    // \" first and \\ second, so a value ending in an escaped backslash was
    // decoded by the wrong rule; and neither knew about \n, which is the only way
    // a newline can appear in a single-line basic string - the spelling a
    // certificate arrives in when the file did not come from here.
    auto unesc = [](const QString &v) {
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
    };
    // Both spellings of a TOML string. This editor writes "...", but a file it did
    // not write is free to use '...', where nothing is an escape. Reading one of
    // those as "absent" did not merely skip it: these are keys the rebuild writes
    // itself, so the value it could not read was replaced with an empty one and
    // the config was emptied in place.
    auto str = [&](const char *key) -> QString {
        const QRegularExpression basic(
                QStringLiteral("(?m)^%1\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")
                        .arg(QLatin1String(key)));
        const auto bm = basic.match(toml);
        if (bm.hasMatch())
            return unesc(bm.captured(1));
        const QRegularExpression literal(
                QStringLiteral("(?m)^%1\\s*=\\s*'([^']*)'").arg(QLatin1String(key)));
        const auto lm = literal.match(toml);
        return lm.hasMatch() ? lm.captured(1) : QString();
    };
    auto arr = [&](const char *key) -> QString {
        // From the opening [ to the ] that matches it, however many lines that
        // takes. A provider formats an array one entry per line as readily as on
        // one, and the single-line read that was here returned nothing for the
        // other spelling - which, for `addresses`, is the whole config.
        const QRegularExpression open(
                QStringLiteral("(?m)^%1\\s*=\\s*\\[").arg(QLatin1String(key)));
        const auto m = open.match(toml);
        if (!m.hasMatch())
            return QString();
        const int start = m.capturedEnd();
        int i = start;
        int depth = 1;
        const int n = toml.size();
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
            if (ch == QLatin1Char('#')) { // a comment inside the array
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
        if (depth != 0)
            return QString();
        const QString body = toml.mid(start, i - 1 - start);
        QStringList out;
        static const QRegularExpression item(
                QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\"|'([^']*)'"));
        auto it = item.globalMatch(body);
        while (it.hasNext()) {
            const auto im = it.next();
            out << (im.capturedStart(1) >= 0 ? unesc(im.captured(1)) : im.captured(2));
        }
        return out.join(QStringLiteral(", "));
    };

    c.hostname = str("hostname");
    c.addresses = arr("addresses");
    c.username = str("username");
    c.password = str("password");
    c.protocol = str("upstream_protocol");
    if (c.protocol.isEmpty()) c.protocol = QStringLiteral("http2");
    c.dns = arr("dns_upstreams");
    c.customSni = str("custom_sni");
    c.clientRandom = str("client_random");
    // Anchored to the start of a line so a `true`/`false` token sitting inside the
    // certificate block or a comment can't flip a flag (skip_verification in
    // particular is security-significant — it disables server cert checking).
    auto boolKey = [&](const char *key, bool dflt) -> bool {
        const QRegularExpression re(
                QStringLiteral("(?m)^%1\\s*=\\s*(true|false)\\b").arg(QLatin1String(key)));
        const auto m = re.match(toml);
        return m.hasMatch() ? (m.captured(1) == QLatin1String("true")) : dflt;
    };
    c.allowIpv6 = boolKey("has_ipv6", true);
    c.skipVerification = boolKey("skip_verification", false);
    c.antiDpi = boolKey("anti_dpi", false);
    // Every spelling a certificate can arrive in, not only the one written here.
    // `certificate` is a key the rebuild writes itself, so a spelling the reader
    // did not know was not merely skipped: it was written back as an empty value,
    // and the pinned trust anchor was gone from the file on disk.
    static const QRegularExpression certBasic(
            QStringLiteral("(?m)^certificate\\s*=\\s*\"\"\"\\n?(.*?)\\n?\"\"\""),
            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression certLiteral(
            QStringLiteral("(?m)^certificate\\s*=\\s*'''\\n?(.*?)\\n?'''"),
            QRegularExpression::DotMatchesEverythingOption);
    const auto cm = certBasic.match(toml);
    if (cm.hasMatch()) {
        // Same unescaping as the quoted fields: buildConfigToml() escapes quotes
        // and backslashes in the block, and a PEM (which has neither) still round
        // trips byte for byte.
        c.certificate = unesc(cm.captured(1));
    } else {
        const auto lm = certLiteral.match(toml);
        // A literal block is literal: no escape is processed inside one.
        c.certificate = lm.hasMatch() ? lm.captured(1) : str("certificate");
    }

    carryOverUnknownTables(toml, c);
    return c;
}

} // namespace freetunnel
