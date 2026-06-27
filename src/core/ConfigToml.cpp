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

static QString csvToTomlArray(const QString &csv) {
    QStringList items;
    for (const QString &raw : csv.split(',', Qt::SkipEmptyParts)) {
        const QString v = raw.trimmed();
        if (!v.isEmpty())
            items << QStringLiteral("\"%1\"").arg(tomlEsc(v));
    }
    return items.join(QStringLiteral(", "));
}

QString buildConfigToml(const ConfigToml &c, const QString &logLevel) {
    QString t;
    t += QStringLiteral("loglevel = \"%1\"\n").arg(logLevel);
    t += QStringLiteral("vpn_mode = \"general\"\n");
    t += QStringLiteral("killswitch_enabled = false\n");
    t += QStringLiteral("post_quantum_group_enabled = true\n");
    t += QStringLiteral("dns_upstreams = [%1]\n").arg(csvToTomlArray(c.dns));
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
        t += QStringLiteral("certificate = \"\"\"\n%1\n\"\"\"\n").arg(c.certificate.trimmed());
    else
        t += QStringLiteral("certificate = \"\"\n");
    t += QStringLiteral("\n[listener.tun]\n");
    t += QStringLiteral("bound_if = \"\"\nmtu_size = 1500\nchange_system_dns = true\n");
    t += QStringLiteral("included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n");
    t += QStringLiteral("excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", "
                        "\"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n");
    return t;
}

ConfigToml parseConfigToml(const QString &toml) {
    ConfigToml c;
    auto unesc = [](QString v) { return v.replace("\\\"", "\"").replace("\\\\", "\\"); };
    auto str = [&](const char *key) -> QString {
        QRegularExpression re(QStringLiteral("(?m)^%1\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")
                                      .arg(QLatin1String(key)));
        const auto m = re.match(toml);
        return m.hasMatch() ? unesc(m.captured(1)) : QString();
    };
    auto arr = [&](const char *key) -> QString {
        // Greedy to the last ] on the line so bracketed IPv6 addresses survive.
        QRegularExpression re(QStringLiteral("(?m)^%1\\s*=\\s*\\[(.*)\\]").arg(QLatin1String(key)));
        const auto m = re.match(toml);
        if (!m.hasMatch()) return QString();
        QStringList out;
        static const QRegularExpression item(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
        auto it = item.globalMatch(m.captured(1));
        while (it.hasNext()) out << unesc(it.next().captured(1));
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
    c.allowIpv6 = !toml.contains(QStringLiteral("has_ipv6 = false"));
    c.skipVerification = toml.contains(QStringLiteral("skip_verification = true"));
    c.antiDpi = toml.contains(QStringLiteral("anti_dpi = true"));
    static const QRegularExpression certRe(
            QStringLiteral("certificate\\s*=\\s*\"\"\"\\n?(.*?)\\n?\"\"\""),
            QRegularExpression::DotMatchesEverythingOption);
    const auto cm = certRe.match(toml);
    c.certificate = cm.hasMatch() ? cm.captured(1) : QString();
    return c;
}

} // namespace freetunnel
