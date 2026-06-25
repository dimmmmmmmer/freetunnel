// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Official TrustTunnel deep link support: `tt://?<base64url>` with a binary
// TLV payload (QUIC/TLS varint encoding). See DEEP_LINK.md in the repo root.
//
// This module is intentionally self-contained (depends only on Qt Core, not on
// the VPN core), so it can be unit-tested without the native toolchain.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

namespace freetunnel {

// Maximum deep link format version this client understands.
inline constexpr int kDeepLinkMaxVersion = 1;

// Upstream transport protocol values (tag 0x09).
enum class UpstreamProtocol { Http2 = 1, Http3 = 2 };

struct DeepLinkConfig {
    int version = 0;
    QString hostname;                                  // 0x01 (required)
    QStringList addresses;                             // 0x02 (>=1 required), "host:port"
    QString customSni;                                 // 0x03
    bool hasIpv6 = true;                               // 0x04 (default true)
    QString username;                                  // 0x05 (required)
    QString password;                                  // 0x06 (required)
    bool skipVerification = false;                     // 0x07 (default false)
    QByteArray certificate;                            // 0x08 concatenated DER
    UpstreamProtocol upstreamProtocol = UpstreamProtocol::Http2; // 0x09
    bool antiDpi = false;                              // 0x0A (default false)
    QString clientRandomPrefix;                        // 0x0B "prefix[/mask]" (hex)
    QString name;                                      // 0x0C display name
    QStringList dnsUpstreams;                          // 0x0D
};

// Parse a `tt://?...` deep link. On failure returns nullopt and, if `error` is
// non-null, sets a human-readable message.
std::optional<DeepLinkConfig> parseDeepLink(const QString &uri, QString *error = nullptr);

// Encode a config back into a `tt://?...` deep link (for QR export / round-trip).
QString encodeDeepLink(const DeepLinkConfig &cfg);

// Render the config as TrustTunnel client TOML so the normal import path can
// consume it. Mirrors the schema produced by the create-config form.
QString deepLinkConfigToToml(const DeepLinkConfig &cfg);

} // namespace freetunnel
