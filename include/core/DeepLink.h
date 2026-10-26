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

// Maximum deep link format version this client understands. TrustTunnel's
// endpoint writes version 2 on every link from 1.3.0 on, with or without the two
// tags it added (0x0E and 0x0F), so a client that stopped at 1 refused them all.
inline constexpr int kDeepLinkMaxVersion = 2;

// The version this client writes on its own links. Not the maximum: version 2
// only adds tags these links never carry, and FreeTunnel 1.2.3 and earlier and
// TrustTunnel's clients up to 1.1.11 refuse a version 2 link outright, so writing
// it would only stop them reading a link they understand in full.
inline constexpr int kDeepLinkWriteVersion = 1;

// The longest subscription address (tag 0x0E) a link may carry, in bytes. An
// address that long is already far past any real one.
inline constexpr int kMaxSubscriptionUrlBytes = 2048;

// Upstream transport protocol values (tag 0x09).
enum class UpstreamProtocol { Http2 = 1, Http3 = 2 };

struct DeepLinkConfig {
    int version = 0;                                   // 0x00, the newest of them if several
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
    // Version 2. Neither is written anywhere: not by encodeDeepLink(), not by
    // deepLinkConfigToToml(). The subscription address holds the VPN password in
    // plain text, and the core this app runs cannot use the auth key.
    QString subscriptionUrl;                           // 0x0E https:// address
    QString clientRandomAuthKey;                       // 0x0F hex; empty = none
};

// Parse a `tt://?...` deep link. On failure returns nullopt and, if `error` is
// non-null, sets a human-readable message.
//
// A link with a subscription address (tag 0x0E) needs none of the required
// fields, as the format says; deepLinkHasStaticServer() tells whether it has them.
std::optional<DeepLinkConfig> parseDeepLink(const QString &uri, QString *error = nullptr);

// Whether the link names a server that can be connected to without its
// subscription: a hostname, an address, a username and a password, none of them
// empty, which is how TrustTunnel's own decoder reads "required". Asked of links
// with a subscription address; one without has to carry all four to parse.
bool deepLinkHasStaticServer(const DeepLinkConfig &cfg);

// Encode a config back into a `tt://?...` deep link (for QR export / round-trip).
// It is always version kDeepLinkWriteVersion, and never carries tag 0x0E or 0x0F.
QString encodeDeepLink(const DeepLinkConfig &cfg);

// Render the config as TrustTunnel client TOML so the normal import path can
// consume it. Mirrors the schema produced by the create-config form.
QString deepLinkConfigToToml(const DeepLinkConfig &cfg);

} // namespace freetunnel
