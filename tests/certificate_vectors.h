// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Certificates, and what a certificate field can hold that is not one, shared by
// the tests of the rule that keeps only the certificates (isCertificateDer,
// certificatesOnly and the link, import, editor and share paths built on them),
// so each path is checked against the same bytes. Android's tests are to carry
// byte-identical copies, so a change here goes there too.
//
// The certificates are real and public: an EC (P-256) certificate authority and
// a server certificate it signed, made for these tests with OpenSSL. Their keys
// were not kept. No private key is here, and none is needed: the rule tells a key
// from a certificate by its DER shape, so a few bytes in a key's shape stand in
// for one. The BEGIN and END lines of a key block are put together at run time,
// so that no line of the source reads as a key to a secret scanner.

#include <QByteArray>
#include <QString>

namespace certs {

// Self-signed: O = FreeTunnel tests, CN = ca.fixture.invalid, valid to 2054.
inline const QString kCaPem = QStringLiteral(
        "-----BEGIN CERTIFICATE-----\n"
        "MIIBxjCCAW2gAwIBAgIUMggTEeksVpGHcjPlkyH1oitg5NwwCgYIKoZIzj0EAwIw\n"
        "ODEZMBcGA1UECgwQRnJlZVR1bm5lbCB0ZXN0czEbMBkGA1UEAwwSY2EuZml4dHVy\n"
        "ZS5pbnZhbGlkMCAXDTI2MTAyNjEzNTMxNVoYDzIwNTQwMzEzMTM1MzE1WjA4MRkw\n"
        "FwYDVQQKDBBGcmVlVHVubmVsIHRlc3RzMRswGQYDVQQDDBJjYS5maXh0dXJlLmlu\n"
        "dmFsaWQwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATpBntG7ibqv0jd9BJTO2mc\n"
        "RteaMpEPy9WesCwtGK8VO+eSJwDmZAnH8qE1BcghMBLM8KG3yy6IurbrRHoRGZk2\n"
        "o1MwUTAdBgNVHQ4EFgQUowpTqPMvc1UujbnAJTjUyUdCFQUwHwYDVR0jBBgwFoAU\n"
        "owpTqPMvc1UujbnAJTjUyUdCFQUwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQD\n"
        "AgNHADBEAiAGbdQRIykqGpGXOlE7Fu1ygkC8+Bx1zYbjbKSLVRIjFwIgfMVbcUL3\n"
        "NOi1/hJlMfPkPenNzmCFmQ3fKh6vnLqZH5I=\n"
        "-----END CERTIFICATE-----\n");

// Signed by kCaPem: O = FreeTunnel tests, CN = vpn.fixture.invalid, with that
// name as its subject alternative name, valid to 2054.
inline const QString kLeafPem = QStringLiteral(
        "-----BEGIN CERTIFICATE-----\n"
        "MIIB1zCCAX2gAwIBAgIUR89LmrAjTDz3XjuZF76HEZe4LHgwCgYIKoZIzj0EAwIw\n"
        "ODEZMBcGA1UECgwQRnJlZVR1bm5lbCB0ZXN0czEbMBkGA1UEAwwSY2EuZml4dHVy\n"
        "ZS5pbnZhbGlkMCAXDTI2MTAyNjEzNTMxNVoYDzIwNTQwMzEzMTM1MzE1WjA5MRkw\n"
        "FwYDVQQKDBBGcmVlVHVubmVsIHRlc3RzMRwwGgYDVQQDDBN2cG4uZml4dHVyZS5p\n"
        "bnZhbGlkMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE2X8QzmoYE7iTMx5zPHcx\n"
        "ao/Rpe9SkUHxxIGQj3MY6Vs+6pMjnl6m5NiecjCrWCwSJ4ApX+JNiJJ6YgBDUli0\n"
        "gKNiMGAwHgYDVR0RBBcwFYITdnBuLmZpeHR1cmUuaW52YWxpZDAdBgNVHQ4EFgQU\n"
        "HeFjex5n8uZHZr/wnf11A0U6JoMwHwYDVR0jBBgwFoAUowpTqPMvc1UujbnAJTjU\n"
        "yUdCFQUwCgYIKoZIzj0EAwIDSAAwRQIgXwB9HTwKPF3zwPc2TNBnB8DVRc8CbRgn\n"
        "KtNe8GiLyvoCIQCjUj8JIRn0w/+nwQC8iUiwBO+YsgsXlQu1BpFeFAxm/g==\n"
        "-----END CERTIFICATE-----\n");

// The chain a server pins: its own certificate, then the one that signed it.
inline const QString kChainPem = kLeafPem + kCaPem;

// The DER of a PEM text that is one block, such as the certificates above.
inline QByteArray derOf(const QString &pem)
{
    const qsizetype begin = pem.indexOf(QLatin1Char('\n')) + 1;
    const qsizetype end = pem.indexOf(QLatin1String("-----END"));
    return QByteArray::fromBase64(pem.mid(begin, end - begin).toLatin1());
}

// A PEM block of any label around any bytes, 64 base64 characters a line.
inline QString pemBlock(const QString &label, const QByteArray &der)
{
    const QByteArray b64 = der.toBase64();
    QString out = QStringLiteral("-----BEGIN ") + label + QStringLiteral("-----\n");
    for (qsizetype i = 0; i < b64.size(); i += 64)
        out += QString::fromLatin1(b64.mid(i, 64)) + QLatin1Char('\n');
    return out + QStringLiteral("-----END ") + label + QStringLiteral("-----\n");
}

// The label of a private key block, with the kind in front ("RSA ", "EC ",
// "ENCRYPTED "), or none. In two pieces, never one literal (see the top).
inline QString keyLabel(const QString &kind = QString())
{
    return kind + QStringLiteral("PRIVATE") + QStringLiteral(" KEY");
}

// DER in the shape of each kind of key, which is all the rule looks at. Every one
// is a SEQUENCE, as a certificate is, and none is SEQUENCE { SEQUENCE, SEQUENCE,
// BIT STRING }.
// PKCS#8: SEQUENCE { INTEGER 0, SEQUENCE { OID }, OCTET STRING }.
inline const QByteArray kPkcs8Der = QByteArray::fromHex("300a02010030030601000400");
// PKCS#1 (RSA): SEQUENCE { INTEGER 0, INTEGER n, INTEGER e }.
inline const QByteArray kPkcs1Der = QByteArray::fromHex("3009020100020105020103");
// SEC1 (EC): SEQUENCE { INTEGER 1, OCTET STRING }.
inline const QByteArray kSec1Der = QByteArray::fromHex("30070201010402aabb");
// Encrypted PKCS#8: SEQUENCE { SEQUENCE { OID, NULL }, OCTET STRING }.
inline const QByteArray kEncryptedPkcs8Der = QByteArray::fromHex("300b300506010005000402aabb");

// A PKCS#8 key as a PEM block, the way a file that combines a certificate with
// its key holds it.
inline QString keyPem()
{
    return pemBlock(keyLabel(), kPkcs8Der);
}

// The text an export of a PKCS#12 file writes before each block.
inline const QString kBagAttributes = QStringLiteral("Bag Attributes\n"
                                                     "    localKeyID: 01 02 03 04\n"
                                                     "subject=O = FreeTunnel tests, CN = vpn.fixture.invalid\n");

// A DER element: the tag, the length in the short or the long form, the content.
inline QByteArray derElement(char tag, const QByteArray &content)
{
    QByteArray out(1, tag);
    const qsizetype n = content.size();
    if (n < 0x80) {
        out += static_cast<char>(n);
    } else if (n < 0x100) {
        out += static_cast<char>(0x81);
        out += static_cast<char>(n);
    } else {
        out += static_cast<char>(0x82);
        out += static_cast<char>((n >> 8) & 0xFF);
        out += static_cast<char>(n & 0xFF);
    }
    return out + content;
}

// The outer shape of a certificate and nothing inside it: SEQUENCE { SEQUENCE of
// `n` filler bytes, SEQUENCE { NULL }, BIT STRING 00 }. For what is about framing
// and not about the certificate, such as how a chain is split.
inline QByteArray certShaped(int n)
{
    return derElement(0x30, derElement(0x30, QByteArray(n, 'A')) + QByteArray::fromHex("30020500")
                                    + QByteArray::fromHex("030100"));
}

} // namespace certs
