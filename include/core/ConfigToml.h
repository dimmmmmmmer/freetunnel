// cppcheck-suppress-file missingIncludeSystem
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
};

// Render a ConfigToml to the client TOML format.
QString buildConfigToml(const ConfigToml &c);

// Parse a client TOML back into a ConfigToml (endpoint fields only).
ConfigToml parseConfigToml(const QString &toml);

} // namespace freetunnel
