// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Version 2 deep links (TrustTunnel endpoint 1.3.0 and later), shared by the link,
// import and backend tests so each rule is checked against the same bytes.
//
// Built by hand from TrustTunnel's DEEP_LINK.md (endpoint v1.3.0-beta.2), in the
// field order its encoder writes (deeplink/src/encode.rs), and checked against
// the endpoint's own decoder and encoder tests (deeplink/src/decode.rs,
// encode.rs). The records of each are in its comment, as tag, length, value.

#include <QString>

namespace v2 {

// The address every vector uses, from the endpoint's tests.
inline const QString kSubscription = QStringLiteral("https://alice:s3cret@vpn.example.com/subscription");

// What endpoint 1.3 exports for a server without subscriptions or an auth key:
// version 2 and the version 1 fields, nothing more.
// 00 01 02 | 01 0f vpn.example.com | 05 05 alice | 06 06 secret | 02 0b 1.2.3.4:443
inline const QString kPlain =
        QStringLiteral("tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz");

// Every static field the endpoint writes, then the subscription address it adds
// when subscriptions are on.
// 00 01 02 | 01 vpn.example.com | 05 alice | 06 s3cret | 02 1.2.3.4:443
// | 02 [2001:db8::1]:443 | 0b aabbcc/ff00ff | 03 example.org | 04 00 | 0a 01
// | 09 02 | 0c Example | 0d [1.1.1.1, tls://dns.example.com] | 0e kSubscription
inline const QString kFull = QStringLiteral(
        "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzAhFbMjAwMTpkYjg6OjFdOjQ0Mw"
        "sNYWFiYmNjL2ZmMDBmZgMLZXhhbXBsZS5vcmcEAQAKAQEJAQIMB0V4YW1wbGUNHgcxLjEuMS4xFXRsczovL2Rucy5leGFtcG"
        "xlLmNvbQ4xaHR0cHM6Ly9hbGljZTpzM2NyZXRAdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg");

// --subscription-only: tags 0x00 and 0x0E and nothing else, the payload the
// endpoint's test_encode_tlv_payload_subscription_only pins.
// 00 01 02 | 0e 31 kSubscription
inline const QString kSubscriptionOnly =
        QStringLiteral("tt://?AAECDjFodHRwczovL2FsaWNlOnMzY3JldEB2cG4uZXhhbXBsZS5jb20vc3Vic2NyaXB0aW9u");

// kPlain with an auth key, as in the endpoint's test_auth_key_roundtrip.
// ... | 0f 08 aabbccdd
inline const QString kAuthKey =
        QStringLiteral("tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzDwhhYWJiY2NkZA");

// kPlain with an empty auth key, which the endpoint's encoder leaves out and a
// hand-made link may not.
// ... | 0f 00
inline const QString kEmptyAuthKey =
        QStringLiteral("tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzDwA");

// Version 3, as in the endpoint's test_decode_rejects_version_3.
// 00 01 03 | 0e 24 https://vpn.example.com/subscription
inline const QString kVersion3 = QStringLiteral("tt://?AAEDDiRodHRwczovL3Zwbi5leGFtcGxlLmNvbS9zdWJzY3JpcHRpb24");

// A plain http:// address, as in test_decode_rejects_non_https_subscription_url.
// 00 01 02 | 0e 23 http://vpn.example.com/subscription
inline const QString kHttpSubscription =
        QStringLiteral("tt://?AAECDiNodHRwOi8vdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg");

} // namespace v2
