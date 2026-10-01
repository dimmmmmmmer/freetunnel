// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Pure, core-independent generation/parsing of the client config TOML, so the
// create/edit round-trip can be unit-tested without the VPN core. addresses and
// dns are comma-separated strings (as entered in the form).

#include <QString>
#include <QStringList>

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
    bool skipVerification = false; // accept the server's TLS cert without validation
    bool antiDpi = false;          // enable the core's anti-DPI obfuscation
    // post_quantum_group_enabled, as the file has it. The editor has no switch for
    // it, but a config may turn it off, and every rewrite turned it back on - the
    // connect path's included, so the core never saw the file's setting.
    bool postQuantum = true;

    // Everything in the file that this editor does not itself generate, kept
    // verbatim so a round trip does not throw it away.
    //
    // A config is rewritten more often than it looks: migrateConfigPassword()
    // parses and rebuilds every config on import, so a provider's .toml used to
    // lose its routing and any section this struct has no field for the moment it
    // was added — silently, with the file on disk still looking like a valid
    // config. These carry that content back out through buildConfigToml().
    QString tunSection;         // [listener.tun] body; empty = write the defaults
    QString extraRootKeys;      // unknown keys before the first table
    QString extraEndpointKeys;  // unknown keys inside [endpoint]
    QString extraSections;      // whole tables other than [endpoint]/[listener.tun]
};

// Render a ConfigToml to the client TOML format.
QString buildConfigToml(const ConfigToml &c, const QString &logLevel = QStringLiteral("info"));

// Parse a client TOML back into a ConfigToml (endpoint fields only).
ConfigToml parseConfigToml(const QString &toml);

// A client random as the core reads it, from the one `client_random` key:
// "prefix" or "prefix/mask", in hex. The core splits at the slash itself and has
// no key for the mask alone, and it refuses the whole config when the slash is
// followed by nothing, so an empty mask is dropped here (however many slashes
// it took), and so is a mask with no prefix in front of it, which masks nothing.
QString clientRandomForCore(const QString &value);

// Whether the core can use a client random as it is written: empty, or a prefix
// and an optional /mask, each whole bytes in hex and at most 32 of them, the size
// of a TLS client random. The core decodes each part as bytes and quietly does
// without one it cannot decode - an odd digit out is enough - and uses no more
// than 32 bytes. The config editor and link import both check with this.
bool isValidClientRandom(const QString &value);

// The DNS servers in a list as the editor takes it, one per entry, separated by
// commas, semicolons or spaces. Validation and everything written from the list
// have to agree on this: split on commas alone, "1.1.1.1 8.8.8.8" passed the
// check and went to the core as one server, which core 1.1.5 refused to connect
// with and 1.1.7 takes, leaving the tunnel up with no DNS.
QStringList splitDnsList(const QString &dns);

} // namespace freetunnel
