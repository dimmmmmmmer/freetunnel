#include "ConfigInspector.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <QTcpSocket>

#include <toml++/toml.h>

static bool splitHostPort(QString s, QString *host, quint16 *port) {
    s = s.trimmed();
    if (s.startsWith("|")) {
        s.remove(0, 1);
    }
    if (s.startsWith("[")) {
        const int close = s.indexOf("]:");
        if (close <= 0) {
            return false;
        }
        *host = s.mid(1, close - 1);
        bool ok = false;
        const int p = s.mid(close + 2).toInt(&ok);
        if (!ok || p <= 0 || p > 65535) {
            return false;
        }
        *port = static_cast<quint16>(p);
        return true;
    }

    const int colon = s.lastIndexOf(':');
    if (colon <= 0 || colon == s.size() - 1) {
        return false;
    }
    *host = s.left(colon);
    bool ok = false;
    const int p = s.mid(colon + 1).toInt(&ok);
    if (!ok || p <= 0 || p > 65535) {
        return false;
    }
    *port = static_cast<quint16>(p);
    return true;
}

QString pingConfigFile(const QString &path) {
    toml::parse_result parsed = toml::parse_file(path.toStdString());
    if (!parsed) {
        return "Config parse error";
    }

    const toml::table *endpoint = parsed["endpoint"].as_table();
    if (!endpoint) {
        return "No [endpoint] section";
    }
    const toml::array *addrs = (*endpoint)["addresses"].as_array();
    if (!addrs || addrs->empty()) {
        return "No endpoint.addresses";
    }

    int attempts = 0;
    for (const toml::node &n : *addrs) {
        std::optional<std::string_view> sv = n.value<std::string_view>();
        if (!sv || sv->empty()) {
            continue;
        }
        QString host;
        quint16 port = 0;
        if (!splitHostPort(QString::fromUtf8(sv->data(), static_cast<int>(sv->size())), &host, &port)) {
            continue;
        }

        attempts++;
        QTcpSocket sock;
        QElapsedTimer t;
        t.start();
        sock.connectToHost(host, port);
        if (sock.waitForConnected(1800)) {
            sock.abort();
            return QString("OK: %1:%2 in %3 ms").arg(host).arg(port).arg(t.elapsed());
        }
    }

    if (attempts == 0) {
        return "No valid host:port to ping";
    }
    return "Fail: all endpoints timed out/unreachable";
}

QString buildConfigSummaryHtml(const QString &path) {
    toml::parse_result parsed = toml::parse_file(path.toStdString());
    if (!parsed) {
        return QString("Failed to parse config: %1")
                .arg(QString::fromUtf8(parsed.error().description().data(),
                                       static_cast<int>(parsed.error().description().size())));
    }
    auto getStr = [&](const toml::node_view<const toml::node> &n, const QString &def = "-") {
        if (auto v = n.value<std::string_view>()) return QString::fromUtf8(v->data(), static_cast<int>(v->size()));
        if (auto b = n.value<bool>()) return *b ? QStringLiteral("true") : QStringLiteral("false");
        if (auto i = n.value<int64_t>()) return QString::number(*i);
        return def;
    };
    auto getArrayStrings = [&](const toml::node_view<const toml::node> &n) {
        QStringList out;
        if (const toml::array *arr = n.as_array()) {
            for (const toml::node &item : *arr) {
                if (auto sv = item.value<std::string_view>()) {
                    out.push_back(QString::fromUtf8(sv->data(), static_cast<int>(sv->size())));
                } else if (auto iv = item.value<int64_t>()) {
                    out.push_back(QString::number(*iv));
                }
            }
        }
        return out;
    };
    const toml::table &t = parsed.table();
    QString endpointHost = getStr(t["endpoint"]["hostname"]);
    QString endpointUser = getStr(t["endpoint"]["username"]);
    QString endpointAddresses = getArrayStrings(t["endpoint"]["addresses"]).join("<br/>");
    if (endpointAddresses.isEmpty()) {
        endpointAddresses = "-";
    }
    QString protocol = getStr(t["endpoint"]["upstream_protocol"]);
    QString fallbackProtocol = getStr(t["endpoint"]["upstream_fallback_protocol"]);
    QString loglevel = getStr(t["loglevel"]);
    QString mode = getStr(t["vpn_mode"]);
    QString killswitch = getStr(t["killswitch_enabled"]);
    QString boundIf = getStr(t["listener"]["tun"]["bound_if"]);
    QString mtu = getStr(t["listener"]["tun"]["mtu_size"]);
    QString dnsChange = getStr(t["listener"]["tun"]["change_system_dns"]);
    QStringList includedRoutes = getArrayStrings(t["listener"]["tun"]["included_routes"]);
    QStringList excludedRoutes = getArrayStrings(t["listener"]["tun"]["excluded_routes"]);
    QStringList dnsUpstreams = getArrayStrings(t["dns_upstreams"]);
    QString listenerType = t["listener"]["tun"].is_table() ? "tun" : (t["listener"]["socks"].is_table() ? "socks" : "-");

    return QString(
            "<h3>Config Overview</h3>"
            "<table cellspacing='6' cellpadding='2'>"
            "<tr><td><b>Endpoint host</b></td><td>%1</td></tr>"
            "<tr><td><b>Endpoint addresses</b></td><td>%2</td></tr>"
            "<tr><td><b>Username</b></td><td>%3</td></tr>"
            "<tr><td><b>Protocol</b></td><td>%4</td></tr>"
            "<tr><td><b>Fallback protocol</b></td><td>%5</td></tr>"
            "<tr><td><b>Log level</b></td><td>%6</td></tr>"
            "<tr><td><b>VPN mode</b></td><td>%7</td></tr>"
            "<tr><td><b>Killswitch</b></td><td>%8</td></tr>"
            "<tr><td><b>Listener type</b></td><td>%9</td></tr>"
            "<tr><td><b>Bound interface</b></td><td>%10</td></tr>"
            "<tr><td><b>MTU</b></td><td>%11</td></tr>"
            "<tr><td><b>Change system DNS</b></td><td>%12</td></tr>"
            "<tr><td><b>DNS upstreams</b></td><td>%13</td></tr>"
            "<tr><td><b>Included routes</b></td><td>%14</td></tr>"
            "<tr><td><b>Excluded routes</b></td><td>%15</td></tr>"
            "</table>")
            .arg(endpointHost.toHtmlEscaped(),
                 endpointAddresses.toHtmlEscaped().replace("\n", "<br/>"),
                 endpointUser.toHtmlEscaped(),
                 protocol.toHtmlEscaped(),
                 fallbackProtocol.toHtmlEscaped(),
                 loglevel.toHtmlEscaped(),
                 mode.toHtmlEscaped(),
                 killswitch.toHtmlEscaped(),
                 listenerType.toHtmlEscaped(),
                 boundIf.toHtmlEscaped(),
                 mtu.toHtmlEscaped(),
                 dnsChange.toHtmlEscaped(),
                 QString::number(dnsUpstreams.size()),
                 QString::number(includedRoutes.size()),
                 QString::number(excludedRoutes.size()));
}

QString buildConfigValidationHtml(const QString &path) {
    toml::parse_result parsed = toml::parse_file(path.toStdString());
    if (!parsed) {
        return QString("<h3>Validation</h3><p style='color:#c33'><b>Parse error:</b> %1</p>")
                .arg(QString::fromUtf8(parsed.error().description().data(),
                                       static_cast<int>(parsed.error().description().size())).toHtmlEscaped());
    }
    const toml::table &t = parsed.table();
    QStringList errors;
    QStringList warnings;

    if (!t["endpoint"].is_table()) {
        errors << "Missing [endpoint] section";
    } else {
        if (!t["endpoint"]["hostname"].is_string()) errors << "endpoint.hostname is required";
        if (!t["endpoint"]["username"].is_string()) warnings << "endpoint.username is empty or missing";
        if (!t["endpoint"]["password"].is_string()) warnings << "endpoint.password is empty or missing";
        const toml::array *addrs = t["endpoint"]["addresses"].as_array();
        if (!addrs || addrs->empty()) errors << "endpoint.addresses must contain at least one host:port";
    }
    if (!t["listener"].is_table()) {
        errors << "Missing [listener] section";
    } else if (!t["listener"]["tun"].is_table() && !t["listener"]["socks"].is_table()) {
        errors << "listener.tun or listener.socks must be defined";
    }
    if (!t["vpn_mode"].is_string()) warnings << "vpn_mode is not set (default behavior may be used)";
    if (!t["loglevel"].is_string()) warnings << "loglevel is not set (default level may be used)";

    QString html = "<h3>Validation</h3>";
    if (errors.isEmpty() && warnings.isEmpty()) {
        html += "<p style='color:#1f7a3f'><b>OK:</b> no obvious structural issues found.</p>";
        return html;
    }
    if (!errors.isEmpty()) {
        html += "<p style='color:#c33'><b>Errors</b></p><ul>";
        for (const QString &e : errors) html += "<li>" + e.toHtmlEscaped() + "</li>";
        html += "</ul>";
    }
    if (!warnings.isEmpty()) {
        html += "<p style='color:#b26a00'><b>Warnings</b></p><ul>";
        for (const QString &w : warnings) html += "<li>" + w.toHtmlEscaped() + "</li>";
        html += "</ul>";
    }
    return html;
}

QString loadLicenseText() {
    QStringList candidates = {
            QCoreApplication::applicationDirPath() + "/../LICENSE",
            QCoreApplication::applicationDirPath() + "/../../LICENSE",
            QCoreApplication::applicationDirPath() + "/../../../LICENSE",
            QDir::currentPath() + "/LICENSE",
            ":/assets/LICENSE"
    };

    for (const QString &path : candidates) {
        QFile f(path);
        if (f.exists() && f.open(QIODevice::ReadOnly)) {
            return QString::fromUtf8(f.readAll());
        }
    }
    return "LICENSE file not found.";
}
