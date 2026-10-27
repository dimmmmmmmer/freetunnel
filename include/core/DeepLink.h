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
// only adds tags (0x0E, 0x0F), and FreeTunnel 1.2.3 and earlier and TrustTunnel's
// clients up to 1.1.11 refuse a version 2 link outright, so writing it on a link
// that does not need it would only stop them reading a link they understand in
// full.
inline constexpr int kDeepLinkWriteVersion = 1;

// The version of a link that carries a client random auth key (tag 0x0F). The
// server such a link names cannot be connected to without the key, so a client
// that refuses version 2 loses nothing by refusing the link.
inline constexpr int kDeepLinkAuthKeyWriteVersion = 2;

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
    QByteArray certificate;                            // 0x08 concatenated DER, as the link has it
    UpstreamProtocol upstreamProtocol = UpstreamProtocol::Http2; // 0x09
    bool antiDpi = false;                              // 0x0A (default false)
    QString clientRandomPrefix;                        // 0x0B "prefix[/mask]" (hex)
    QString name;                                      // 0x0C display name
    QStringList dnsUpstreams;                          // 0x0D
    // Version 2. The subscription address is written nowhere: not by
    // encodeDeepLink(), not by deepLinkConfigToToml(). It holds the VPN password
    // in plain text. The auth key is written by both: it is what the core derives
    // the TLS client random from, and the server turns a client away without it.
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

// The certificates among the DER elements of a certificate field (tag 0x08), one
// after another (see isCertificateDer). *dropped says whether anything else was
// there: an element of another kind, such as a private key, or bytes that are
// not elements at all. parseDeepLink() keeps the field as the link has it, and
// refuses only bytes that are not elements; the rest is what this leaves out.
QByteArray deepLinkCertificates(const QByteArray &der, bool *dropped = nullptr);

// Encode a config back into a `tt://?...` deep link (for QR export / round-trip).
// It is version kDeepLinkWriteVersion, or kDeepLinkAuthKeyWriteVersion with the
// client random auth key as tag 0x0F when the config has one, and never carries
// tag 0x0E. Its tag 0x08 holds the certificates alone (deepLinkCertificates).
QString encodeDeepLink(const DeepLinkConfig &cfg);

// Render the config as TrustTunnel client TOML so the normal import path can
// consume it. Mirrors the schema produced by the create-config form. The
// certificate holds the certificates alone, one PEM block each. The password and
// the auth key are in it, for the import to move to the credential store.
QString deepLinkConfigToToml(const DeepLinkConfig &cfg);

} // namespace freetunnel
