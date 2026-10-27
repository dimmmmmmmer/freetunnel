// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Subscription documents (TrustTunnel endpoint 1.3.0 and later), shared by the
// subscription tests so each rule is checked against the same bytes. Android's
// subscription tests are to carry byte-identical copies, as they do the deep link
// vectors, so a change here goes there too.
//
// Built by hand as endpoint v1.3.0-beta.2 serves them: serde_json's compact
// output of its SubscriptionResponse (lib/src/subscription.rs), so the fields come
// in that struct's order, and the values its handler fills in
// (lib/src/http_subscription_handler.rs). The values are the deep link vectors'
// own (deeplink_v2_vectors.h).

#include <QByteArray>

#include "certificate_vectors.h"

namespace sub {

// What the endpoint serves for a host whose certificate the system trusts, with
// none of the optional fields set: has_ipv6, upstream_protocol, anti_dpi and
// skip_verification are fixed in 1.3.0-beta.2.
inline const QByteArray kMinimal(
        R"({"version":1,"hostname":"vpn.example.com","address":"1.2.3.4:443","username":"alice",)"
        R"("password":"s3cret","has_ipv6":true,"upstream_protocol":"http2","anti_dpi":false,)"
        R"("skip_verification":false})");

// Every field the format has but the certificate, in the endpoint's order, each
// set to what is not its default: the values a later endpoint may serve, once
// the fixed four are settings, as TrustTunnel's own client already reads them.
inline const QByteArray kFull(
        R"({"version":1,"hostname":"vpn.example.com","address":"1.2.3.4:443","username":"alice",)"
        R"("password":"s3cret","has_ipv6":false,"upstream_protocol":"http3","anti_dpi":true,)"
        R"("skip_verification":false,"custom_sni":"example.org","client_random_prefix":"aabbcc/ff00ff",)"
        R"("name":"Example","dns_upstreams":["1.1.1.1","tls://dns.example.com"]})");

// A host whose certificate the system does not trust, which is every self-signed
// one: the endpoint serves its certificate file, here certs::kCaPem, as written.
inline const QByteArray kSelfSigned(
        R"({"version":1,"hostname":"vpn.example.com","address":"1.2.3.4:443","username":"alice",)"
        R"("password":"s3cret","has_ipv6":true,"upstream_protocol":"http2","anti_dpi":false,)"
        R"("skip_verification":false,"certificate":"-----BEGIN CERTIFICATE-----\n)"
        R"(MIIBxjCCAW2gAwIBAgIUMggTEeksVpGHcjPlkyH1oitg5NwwCgYIKoZIzj0EAwIw\n)"
        R"(ODEZMBcGA1UECgwQRnJlZVR1bm5lbCB0ZXN0czEbMBkGA1UEAwwSY2EuZml4dHVy\n)"
        R"(ZS5pbnZhbGlkMCAXDTI2MTAyNjEzNTMxNVoYDzIwNTQwMzEzMTM1MzE1WjA4MRkw\n)"
        R"(FwYDVQQKDBBGcmVlVHVubmVsIHRlc3RzMRswGQYDVQQDDBJjYS5maXh0dXJlLmlu\n)"
        R"(dmFsaWQwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATpBntG7ibqv0jd9BJTO2mc\n)"
        R"(RteaMpEPy9WesCwtGK8VO+eSJwDmZAnH8qE1BcghMBLM8KG3yy6IurbrRHoRGZk2\n)"
        R"(o1MwUTAdBgNVHQ4EFgQUowpTqPMvc1UujbnAJTjUyUdCFQUwHwYDVR0jBBgwFoAU\n)"
        R"(owpTqPMvc1UujbnAJTjUyUdCFQUwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQD\n)"
        R"(AgNHADBEAiAGbdQRIykqGpGXOlE7Fu1ygkC8+Bx1zYbjbKSLVRIjFwIgfMVbcUL3\n)"
        R"(NOi1/hJlMfPkPenNzmCFmQ3fKh6vnLqZH5I=\n)"
        R"(-----END CERTIFICATE-----\n"})");

// kSelfSigned from a host whose certificate file also holds its private key, as
// the endpoint's own tests set one up: it serves the whole file. Put together at
// run time, as certs::keyPem() is, so that no line of the source reads as a key.
inline QByteArray selfSignedWithKey()
{
    QByteArray key = certs::keyPem().toLatin1();
    key.replace("\n", "\\n");
    const QByteArray end = QByteArrayLiteral("-----END CERTIFICATE-----\\n");
    QByteArray out = kSelfSigned;
    out.insert(out.indexOf(end) + end.size(), key);
    return out;
}

// kMinimal with an auth key, the key the deep link's 0x0F carries as well (endpoint
// 1.3.0-beta.2). The document field is read as the link's tag is: whole bytes of hex.
inline const QByteArray kAuthKey(
        R"({"version":1,"hostname":"vpn.example.com","address":"1.2.3.4:443","username":"alice",)"
        R"("password":"s3cret","has_ipv6":true,"upstream_protocol":"http2","anti_dpi":false,)"
        R"("skip_verification":false,"client_random_auth_key":"aabbccdd"})");

// A version 2 document, which no endpoint serves yet, in a shape version 1 would
// call damaged: a list of addresses where the one address was.
inline const QByteArray kVersion2(
        R"({"version":2,"hostname":"vpn.example.com","addresses":["1.2.3.4:443","5.6.7.8:443"],)"
        R"("username":"alice","password":"s3cret"})");

} // namespace sub
