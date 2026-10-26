// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Version 2 deep links (TrustTunnel endpoint 1.3.0 and later), shared by the link,
// import and backend tests so each rule is checked against the same bytes.
//
// Built by hand from TrustTunnel's DEEP_LINK.md (endpoint v1.3.0-beta.2), in the
// field order its encoder writes (deeplink/src/encode.rs), and checked against
// the endpoint's own decoder and encoder tests (deeplink/src/decode.rs,
// encode.rs). The records of each are in its comment, as tag, length, value.

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

namespace v2 {

// The address every vector uses, from the endpoint's tests.
inline const QString kSubscription = QStringLiteral("https://alice:s3cret@vpn.example.com/subscription");

// What endpoint 1.3 exports for a server without subscriptions or an auth key:
// version 2 and the version 1 fields, nothing more.
// 00 01 02 | 01 0f vpn.example.com | 05 05 alice | 06 06 secret | 02 0b 1.2.3.4:443
inline const QString kPlain =
        QStringLiteral("tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz");

// Every static field TrustTunnel's encoder writes, in its order, but 0x07 and
// 0x08, then the subscription address the endpoint adds when subscriptions are
// on. The endpoint's export never sets 0x07; for 0x08, see kSelfSigned.
// 00 01 02 | 01 vpn.example.com | 05 alice | 06 s3cret | 02 1.2.3.4:443
// | 02 [2001:db8::1]:443 | 0b aabbcc/ff00ff | 03 example.org | 04 00 | 0a 01
// | 09 02 | 0c Example | 0d [1.1.1.1, tls://dns.example.com] | 0e kSubscription
inline const QString kFull = QStringLiteral(
        "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzAhFbMjAwMTpkYjg6OjFdOjQ0Mw"
        "sNYWFiYmNjL2ZmMDBmZgMLZXhhbXBsZS5vcmcEAQAKAQEJAQIMB0V4YW1wbGUNHgcxLjEuMS4xFXRsczovL2Rucy5leGFtcG"
        "xlLmNvbQ4xaHR0cHM6Ly9hbGljZTpzM2NyZXRAdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg");

// What the endpoint exports for a server whose certificate the system does not
// trust, which is every self-signed one, with subscriptions on: the certificate
// goes in the link (0x08, as DER), just before the address. A one-element DER
// sequence stands in for the certificate; the link carries the bytes as they are.
// 00 01 02 | 01 vpn.example.com | 05 alice | 06 s3cret | 02 1.2.3.4:443
// | 08 05 30 03 02 01 01 | 0e 31 kSubscription
inline const QString kSelfSigned = QStringLiteral(
        "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzCAUwAwIBAQ4xaHR0cHM6Ly9hbGljZT"
        "pzM2NyZXRAdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg");

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

// Tags no version defines yet, beside both new ones: 0x10, the record the
// endpoint's own test_tlv_parser_unknown_tag uses and the next tag TrustTunnel
// would add, and 0x40, a tag written as a two-byte varint. Both are ignored, and
// the server imports without its subscription address; the auth key is empty.
// 00 01 02 | 01 vpn.example.com | 05 alice | 06 s3cret | 02 1.2.3.4:443 | 0f 00
// | 10 03 01 02 03 | 40 40 02 ab cd | 0e 31 kSubscription
inline const QString kUnknownTags = QStringLiteral(
        "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzDwAQAwECA0BAAqvNDjFodHRwczovL2"
        "FsaWNlOnMzY3JldEB2cG4uZXhhbXBsZS5jb20vc3Vic2NyaXB0aW9u");

// Two version records, 3 and then 2, before a whole server. TrustTunnel's decoder
// refuses a link if any version record is above what it reads, and so does
// FreeTunnel: this is version 3.
// 00 01 03 | 00 01 02 | 01 01 h | 02 0b 1.2.3.4:443 | 05 01 u | 06 01 p
inline const QString kTwoVersions = QStringLiteral("tt://?AAEDAAECAQFoAgsxLjIuMy40OjQ0MwUBdQYBcA");

// A version record with no value, and one whose value is the first byte of a
// two-byte varint. Neither says a version, and both are refused as damaged, as
// TrustTunnel's decoder refuses them; they are not version 0.
// 00 00 | 01 01 h | 02 0b 1.2.3.4:443 | 05 01 u | 06 01 p
inline const QString kEmptyVersion = QStringLiteral("tt://?AAABAWgCCzEuMi4zLjQ6NDQzBQF1BgFw");
// 00 01 40 | 01 01 h | 02 0b 1.2.3.4:443 | 05 01 u | 06 01 p
inline const QString kTruncatedVersion = QStringLiteral("tt://?AAFAAQFoAgsxLjIuMy40OjQ0MwUBdQYBcA");

// A link made of the given records, each tag one varint byte and each length one
// or two, for the cases above with one thing changed.
inline QString linkFromRecords(const QList<QPair<char, QByteArray>> &records)
{
    QByteArray p;
    for (const auto &[tag, v] : records) {
        p.append(tag);
        if (v.size() > 63) // a two-byte varint length
            p.append(static_cast<char>(0x40 | (v.size() >> 8)));
        p.append(static_cast<char>(v.size() & 0xFF));
        p.append(v);
    }
    return QStringLiteral("tt://?")
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

} // namespace v2
