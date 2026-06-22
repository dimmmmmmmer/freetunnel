#pragma once

// Pure, core-independent generation/parsing of the client config TOML, so the
// create/edit round-trip can be unit-tested without the VPN core. addresses and
// dns are comma-separated strings (as entered in the form).

#include <QString>

namespace freetunnel {

struct ConfigToml {
    QString name;
    QString hostname;
    QString addresses;   // CSV of host:port
    QString username;
    QString password;
    QString protocol = QStringLiteral("http2"); // "http2" | "http3"
    QString dns;         // CSV of DNS upstreams
    QString customSni;
    QString clientRandom;
    QString certificate; // PEM body, optional
    bool allowIpv6 = true;

    // Listener: false = TUN (system-wide VPN, needs the elevated helper);
    // true = SOCKS5 (a local proxy on socksListen, no elevation needed).
    bool socks5 = false;
    QString socksListen = QStringLiteral("127.0.0.1:1080"); // host:port to bind
    QString socksUser;   // optional SOCKS auth username
    QString socksPass;   // optional SOCKS auth password
};

// Render a ConfigToml to the client TOML format.
QString buildConfigToml(const ConfigToml &c);

// Parse a client TOML back into a ConfigToml (endpoint fields only).
ConfigToml parseConfigToml(const QString &toml);

} // namespace freetunnel
