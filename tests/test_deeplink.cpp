// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>
#include <QRandomGenerator>

#include "core/ConfigToml.h"
#include "core/DeepLink.h"

using namespace freetunnel;

class TestDeepLink : public QObject {
    Q_OBJECT

private slots:
    void roundTrip();
    void rejectsBadScheme();
    void rejectsMissingRequired();
    void rejectsFutureVersion();
    void ignoresUnknownTags();
    void decodesHandCrafted();
    void longValuesUseMultiByteVarint();
    void tomlContainsKeyFields();
    void tomlInjectionStripped();
    void malformedPayloadsNeverCrash();
    void structuredTlvFuzzNeverCrash();
    void mutatesValidLinksSafely();
    void rejectsOversizedTlvLength();
    void rejectsATlvLengthPastTheEndOfThePayload();
    void acceptsTrustTunnelQrFragment();
    void aCertificateChainSurvivesTheLink();
    void aMaskedClientRandomReachesTheConfigWhole();
    void aClientRandomTheCoreCannotUseIsRefused();
    void aShareLinkKeepsThePrefixTheCoreUses();
};

// A link with the required fields and the given client random, byte for byte as
// a link from elsewhere could carry it: encodeDeepLink() only writes good ones.
static QString linkWithClientRandom(const QByteArray &clientRandom)
{
    QByteArray p;
    auto tlv = [&](char tag, const QByteArray &v) {
        p.append(tag);
        if (v.size() > 63) // a two-byte varint length
            p.append(static_cast<char>(0x40 | (v.size() >> 8)));
        p.append(static_cast<char>(v.size() & 0xFF));
        p.append(v);
    };
    tlv(0x01, "vpn.example.com");
    tlv(0x02, "1.2.3.4:443");
    tlv(0x05, "u");
    tlv(0x06, "p");
    tlv(0x0B, clientRandom);
    return QStringLiteral("tt://?")
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

void TestDeepLink::roundTrip() {
    DeepLinkConfig in;
    in.hostname = "vpn.example.com";
    in.addresses = {"1.2.3.4:443", "[2001:db8::1]:443"};
    in.customSni = "example.org";
    in.hasIpv6 = false;
    in.username = "premium";
    in.password = "s3cretPass";
    in.skipVerification = true;
    in.upstreamProtocol = UpstreamProtocol::Http3;
    in.antiDpi = true;
    in.clientRandomPrefix = "deadbeef/ffff";
    in.name = "My Server";
    in.dnsUpstreams = {"1.1.1.1", "tls://dns.example.com"};

    const QString uri = encodeDeepLink(in);
    QVERIFY(uri.startsWith("tt://?"));

    QString err;
    auto out = parseDeepLink(uri, &err);
    QVERIFY2(out.has_value(), qPrintable(err));

    QCOMPARE(out->hostname, in.hostname);
    QCOMPARE(out->addresses, in.addresses);
    QCOMPARE(out->customSni, in.customSni);
    QCOMPARE(out->hasIpv6, false);
    QCOMPARE(out->username, in.username);
    QCOMPARE(out->password, in.password);
    QCOMPARE(out->skipVerification, true);
    QVERIFY(out->upstreamProtocol == UpstreamProtocol::Http3);
    QCOMPARE(out->antiDpi, true);
    QCOMPARE(out->clientRandomPrefix, in.clientRandomPrefix);
    QCOMPARE(out->name, in.name);
    QCOMPARE(out->dnsUpstreams, in.dnsUpstreams);
}

void TestDeepLink::rejectsBadScheme() {
    QString err;
    QVERIFY(!parseDeepLink("https://example.com", &err).has_value());
    QVERIFY(!err.isEmpty());
    QVERIFY(!parseDeepLink("tt://?", &err).has_value()); // empty payload
}

void TestDeepLink::rejectsMissingRequired() {
    DeepLinkConfig in;
    in.hostname = "h.example";
    // no addresses / username / password
    const QString uri = encodeDeepLink(in);
    QString err;
    QVERIFY(!parseDeepLink(uri, &err).has_value());
}

void TestDeepLink::rejectsFutureVersion() {
    // Hand-craft a payload that declares version 99 plus all required fields.
    QByteArray p;
    auto tlv = [&](char tag, const QByteArray &v) {
        p.append(tag);
        p.append(static_cast<char>(v.size()));
        p.append(v);
    };
    tlv(0x00, QByteArray(1, 2)); // version = 2 (> kDeepLinkMaxVersion); fits one varint byte
    tlv(0x01, "h");
    tlv(0x02, "1.2.3.4:443");
    tlv(0x05, "u");
    tlv(0x06, "pw");
    const QString uri = "tt://?"
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
    QString err;
    QVERIFY(!parseDeepLink(uri, &err).has_value());
    QVERIFY(err.contains("version"));
}

void TestDeepLink::ignoresUnknownTags() {
    QByteArray p;
    auto tlv = [&](char tag, const QByteArray &v) {
        p.append(tag);
        p.append(static_cast<char>(v.size()));
        p.append(v);
    };
    tlv(0x01, "h.example");
    tlv(0x02, "1.2.3.4:443");
    tlv(0x05, "user");
    tlv(0x06, "pass");
    tlv(0x2A, "future-field-payload"); // unknown tag must be skipped
    const QString uri = "tt://?"
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
    QString err;
    auto out = parseDeepLink(uri, &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QCOMPARE(out->hostname, QStringLiteral("h.example"));
    QCOMPARE(out->hasIpv6, true); // default preserved
}

void TestDeepLink::decodesHandCrafted() {
    // Decoder must work independently of our encoder.
    QByteArray p;
    auto tlv = [&](char tag, const QByteArray &v) {
        p.append(tag);
        p.append(static_cast<char>(v.size()));
        p.append(v);
    };
    tlv(0x01, "host.tld");
    tlv(0x02, "10.0.0.1:8443");
    tlv(0x02, "10.0.0.2:8443");
    tlv(0x05, "alice");
    tlv(0x06, "wonderland");
    const QString uri = "tt://?"
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
    auto out = parseDeepLink(uri);
    QVERIFY(out.has_value());
    QCOMPARE(out->addresses.size(), 2);
    QCOMPARE(out->addresses.at(1), QStringLiteral("10.0.0.2:8443"));
}

void TestDeepLink::longValuesUseMultiByteVarint() {
    DeepLinkConfig in;
    in.hostname = QString(200, QChar('a')); // > 63 bytes -> 2-byte length varint
    in.addresses = {"1.2.3.4:443"};
    in.username = "u";
    in.password = "p";
    const QString uri = encodeDeepLink(in);
    auto out = parseDeepLink(uri);
    QVERIFY(out.has_value());
    QCOMPARE(out->hostname.size(), 200);
}

void TestDeepLink::tomlContainsKeyFields() {
    DeepLinkConfig in;
    in.hostname = "vpn.example.com";
    in.addresses = {"1.2.3.4:443"};
    in.username = "u";
    in.password = "p";
    in.upstreamProtocol = UpstreamProtocol::Http3;
    const QString toml = deepLinkConfigToToml(in);
    QVERIFY(toml.contains("[endpoint]"));
    QVERIFY(toml.contains("hostname = \"vpn.example.com\""));
    QVERIFY(toml.contains("addresses = [\"1.2.3.4:443\"]"));
    QVERIFY(toml.contains("upstream_protocol = \"http3\""));
}

// A crafted field with control characters must not be able to break out of its
// quoted TOML value and inject extra keys into the generated config.
void TestDeepLink::tomlInjectionStripped() {
    DeepLinkConfig in;
    in.hostname = "h.example";
    in.addresses = {"1.2.3.4:443"};
    in.username = "u\nskip_verification = true\nx = \""; // newline + quote break-out attempt
    in.password = "p";
    const QString toml = deepLinkConfigToToml(in);
    // No injected key can appear at the start of a line...
    QVERIFY(!toml.contains(QStringLiteral("\nskip_verification = true")));
    // ...and the legitimate value is intact.
    QVERIFY(toml.contains(QStringLiteral("skip_verification = false")));
    // The newline is stripped, so the value collapses onto its own (single) line.
    QVERIFY(toml.contains(QStringLiteral("username = \"uskip_verification = true")));
}

void TestDeepLink::malformedPayloadsNeverCrash()
{
    for (int seed = 0; seed < 3000; ++seed) {
        QByteArray p;
        p.resize(seed % 512);
        for (int i = 0; i < p.size(); ++i)
            p[i] = static_cast<char>((seed * 31 + i * 17) & 0xFF);
        const QString uri = QStringLiteral("tt://?")
                + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding));
        QString err;
        (void)parseDeepLink(uri, &err);
    }
    QVERIFY(true);
}

// Random TLV-shaped byte streams (not necessarily valid base64url payloads).
void TestDeepLink::structuredTlvFuzzNeverCrash()
{
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 2000; ++i) {
        QByteArray p;
        p.resize(rng->bounded(1, 384));
        for (int j = 0; j < p.size(); ++j)
            p[j] = static_cast<char>(rng->generate() & 0xFF);
        const QString uri = QStringLiteral("tt://?")
                + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                                 | QByteArray::OmitTrailingEquals));
        QString err;
        (void)parseDeepLink(uri, &err);
    }
    QVERIFY(true);
}

void TestDeepLink::mutatesValidLinksSafely()
{
    DeepLinkConfig in;
    in.hostname = QStringLiteral("vpn.example.com");
    in.addresses = {QStringLiteral("1.2.3.4:443")};
    in.username = QStringLiteral("user");
    in.password = QStringLiteral("pass");
    const QString valid = encodeDeepLink(in);
    const QString payload = valid.mid(QStringLiteral("tt://?").size());

    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 500; ++i) {
        QByteArray mutated = payload.toLatin1();
        const int n = rng->bounded(1, 8);
        for (int j = 0; j < n; ++j) {
            const int pos = rng->bounded(mutated.size() + 1);
            if (pos == mutated.size())
                mutated.append(static_cast<char>(rng->generate() & 0xFF));
            else
                mutated[pos] = static_cast<char>(rng->generate() & 0xFF);
        }
        if (rng->bounded(4) == 0 && !mutated.isEmpty())
            mutated.chop(rng->bounded(1, qMin(16, mutated.size())));
        const QString uri = QStringLiteral("tt://?")
                + QString::fromLatin1(mutated);
        QString err;
        (void)parseDeepLink(uri, &err);
    }
    QVERIFY(true);
}

void TestDeepLink::rejectsOversizedTlvLength()
{
    // Tag 0x01 + 8-byte varint length > INT_MAX must fail safely (no overflow crash).
    QByteArray p;
    p.append(static_cast<char>(0x01));
    p.append(static_cast<char>(0xC0));
    for (int i = 0; i < 7; ++i)
        p.append(static_cast<char>(0xFF));
    const QString uri = QStringLiteral("tt://?")
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
    QString err;
    QVERIFY(!parseDeepLink(uri, &err).has_value());
    QVERIFY(!err.isEmpty());
}

// rejectsOversizedTlvLength above uses a length above INT_MAX, which the first
// guard in tlvLengthFits() catches. The third guard — a length that is a
// perfectly ordinary int but reaches past the bytes actually present — had no
// test, and deleting it broke nothing. That is the truncated link: a share link
// cut short by a chat client, a copy that missed the tail.
//
// It matters beyond neatness because QByteArray::mid() silently clamps. Without
// the bounds check nothing crashes; the parser simply accepts a short value as
// though it were whole, so a link claiming a 64-byte password and carrying four
// bytes would import as a four-byte password and connect with it.
void TestDeepLink::rejectsATlvLengthPastTheEndOfThePayload()
{
    QByteArray p;
    auto tlv = [&](char tag, const QByteArray &v) {
        p.append(tag);
        p.append(static_cast<char>(v.size()));
        p.append(v);
    };
    tlv(0x01, "host.tld");
    tlv(0x02, "10.0.0.1:8443");
    tlv(0x05, "alice");
    // Declares 32 bytes of password and then stops after four.
    p.append(static_cast<char>(0x06));
    p.append(static_cast<char>(32));
    p.append("word");

    const QString uri = QStringLiteral("tt://?")
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
    QString err;
    const auto out = parseDeepLink(uri, &err);
    QVERIFY2(!out.has_value(),
             "a TLV reaching past the end of the payload must be refused, not silently "
             "truncated to whatever bytes happen to be there");
    QVERIFY(!err.isEmpty());
}

void TestDeepLink::acceptsTrustTunnelQrFragment()
{
    DeepLinkConfig in;
    in.hostname = "vpn.example.com";
    in.addresses = {"1.2.3.4:443"};
    in.username = "user";
    in.password = "pass";
    const QString uri = encodeDeepLink(in);
    const QString payload = uri.mid(QStringLiteral("tt://?").size());
    const QString qr = QStringLiteral("https://trusttunnel.org/qr.html#tt=") + payload;
    QString err;
    auto out = parseDeepLink(qr, &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QCOMPARE(out->hostname, in.hostname);
}

// A link can give the client random with a mask, "prefix/mask", and the core reads
// it that way from the one client_random key. It was split into client_random and a
// client_random_mask key the core has no such key for, and the mask was lost.
void TestDeepLink::aMaskedClientRandomReachesTheConfigWhole() {
    DeepLinkConfig in;
    in.hostname = "vpn.example.com";
    in.addresses = {"1.2.3.4:443"};
    in.username = "u";
    in.password = "p";
    in.clientRandomPrefix = "deadbeef/ffff0000";
    QString err;
    const std::optional<DeepLinkConfig> back = parseDeepLink(encodeDeepLink(in), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    const QString toml = deepLinkConfigToToml(*back);
    QVERIFY2(toml.contains(QStringLiteral("client_random = \"deadbeef/ffff0000\"\n")), qPrintable(toml));
    QVERIFY2(!toml.contains(QStringLiteral("client_random_mask")), qPrintable(toml));
    QCOMPARE(parseConfigToml(toml).clientRandom, QStringLiteral("deadbeef/ffff0000"));

    // With nothing after the slash the core refuses the whole config: no mask then.
    in.clientRandomPrefix = "deadbeef/";
    QVERIFY(deepLinkConfigToToml(in).contains(QStringLiteral("client_random = \"deadbeef\"\n")));
}

// The core decodes the prefix and the mask as whole bytes in hex, quietly does
// without a part it cannot decode, and uses no more than the 32 bytes of a TLS
// client random. A link's value was taken as it came: one trailing slash was
// dropped and nothing else looked at, so "aa//" went on as "aa/", the empty mask
// that makes the core refuse the config, and any other text went into the config
// for the editor to refuse later.
void TestDeepLink::aClientRandomTheCoreCannotUseIsRefused()
{
    // What the core reads as no mask, or as no prefix, is repaired.
    const QList<QPair<QByteArray, QString>> repaired{
            {"deadbeef/", QStringLiteral("deadbeef")},
            {"aa//", QStringLiteral("aa")},
            {"aa/ff00///", QStringLiteral("aa/ff00")},
            {"/ffff", QString()},
            {"//", QString()},
            {" AABB/ff00 ", QStringLiteral("AABB/ff00")},
    };
    for (const auto &[raw, written] : repaired) {
        QString err;
        const auto cfg = parseDeepLink(linkWithClientRandom(raw), &err);
        QVERIFY2(cfg.has_value(), qPrintable(QString::fromLatin1(raw) + QStringLiteral(": ") + err));
        const QString toml = deepLinkConfigToToml(*cfg);
        QVERIFY2(toml.contains(QStringLiteral("client_random = \"%1\"\n").arg(written)), qPrintable(toml));
    }

    // Anything else is damage, and the link is refused as for any other.
    const QByteArray tooLong(66, 'a');
    for (const QByteArray &bad : {QByteArray("xyz"), QByteArray("abc"), QByteArray("aa/bbb"),
                                  QByteArray("aa/bb/cc"), QByteArray("aa//bb"), QByteArray("aa bb"),
                                  tooLong, QByteArray("aa/") + tooLong}) {
        QString err;
        QVERIFY2(!parseDeepLink(linkWithClientRandom(bad), &err).has_value(), bad.constData());
        QVERIFY2(err.contains(QStringLiteral("client_random")), qPrintable(err));
    }
    // The longest that is still whole: 32 bytes each side.
    const QByteArray longest = QByteArray(64, 'f') + '/' + QByteArray(64, '0');
    QVERIFY(parseDeepLink(linkWithClientRandom(longest)).has_value());

    // And a config's own link never carries one its import would refuse.
    DeepLinkConfig own;
    own.hostname = QStringLiteral("vpn.example.com");
    own.addresses = {QStringLiteral("1.2.3.4:443")};
    own.username = QStringLiteral("u");
    own.password = QStringLiteral("p");
    own.clientRandomPrefix = QStringLiteral("abc");
    QString err;
    auto back = parseDeepLink(encodeDeepLink(own), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QVERIFY(back->clientRandomPrefix.isEmpty());
    own.clientRandomPrefix = QStringLiteral("aa//");
    back = parseDeepLink(encodeDeepLink(own), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->clientRandomPrefix, QStringLiteral("aa"));
}

// A mask the core cannot decode is done without, and the prefix goes out
// unmasked. 1.2.2 could leave such a pair in a config, prefix and mask under keys
// of their own; its share link left the client random out altogether, so whoever
// imported it connected without one, unlike the config it came from.
void TestDeepLink::aShareLinkKeepsThePrefixTheCoreUses()
{
    const ConfigToml stored = parseConfigToml(QStringLiteral(
            "[endpoint]\nhostname = \"vpn.example.com\"\naddresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\npassword = \"p\"\n"
            "client_random = \"deadbeef\"\nclient_random_mask = \"fff\"\n"));
    DeepLinkConfig own;
    own.hostname = stored.hostname;
    own.addresses = {stored.addresses};
    own.username = stored.username;
    own.password = stored.password;
    own.clientRandomPrefix = stored.clientRandom;
    QString err;
    auto back = parseDeepLink(encodeDeepLink(own), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->clientRandomPrefix, QStringLiteral("deadbeef"));

    // A prefix the core cannot decode leaves nothing for it to use either.
    own.clientRandomPrefix = QStringLiteral("abc/ffff");
    back = parseDeepLink(encodeDeepLink(own), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QVERIFY(back->clientRandomPrefix.isEmpty());
}

QTEST_MAIN(TestDeepLink)
// Tag 0x08 is a chain — the header says so, DEEP_LINK.md says so, and the export
// side means it: pemCertsToDer() concatenates the DER of every PEM block it
// finds. The import side wrapped the whole blob in one BEGIN/END pair, so a
// config pinning a leaf and its intermediate came back through its OWN share
// link as a single block that is not a certificate at all. Nothing caught it
// because no test here ever set a certificate.
void TestDeepLink::aCertificateChainSurvivesTheLink()
{
    // ASN.1 SEQUENCEs rather than real certificates: what broke is the framing,
    // and framing is what this checks. One short-form length, one long.
    auto sequence = [](int n) {
        QByteArray out;
        out += char(0x30);
        if (n < 0x80) {
            out += char(n);
        } else {
            out += char(0x82);
            out += char((n >> 8) & 0xFF);
            out += char(n & 0xFF);
        }
        return out + QByteArray(n, 'A');
    };

    freetunnel::DeepLinkConfig cfg;
    cfg.version = freetunnel::kDeepLinkMaxVersion;
    cfg.hostname = QStringLiteral("vpn.example.org");
    cfg.addresses = {QStringLiteral("1.2.3.4:443")};
    cfg.username = QStringLiteral("u");
    cfg.password = QStringLiteral("p");
    cfg.certificate = sequence(300) + sequence(120);

    QString err;
    const auto back = freetunnel::parseDeepLink(freetunnel::encodeDeepLink(cfg), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->certificate, cfg.certificate);

    const QString toml = freetunnel::deepLinkConfigToToml(*back);
    QCOMPARE(toml.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 2);
    QCOMPARE(toml.count(QStringLiteral("-----END CERTIFICATE-----")), 2);

    // A single certificate still comes out as exactly one block.
    freetunnel::DeepLinkConfig one = cfg;
    one.certificate = sequence(64);
    QCOMPARE(freetunnel::deepLinkConfigToToml(one)
                     .count(QStringLiteral("-----BEGIN CERTIFICATE-----")),
             1);

    // And a blob that is not a sequence of elements is still emitted rather than
    // dropped: no worse than before, and losing it silently would be worse.
    freetunnel::DeepLinkConfig junk = cfg;
    junk.certificate = QByteArray("not der at all");
    QCOMPARE(freetunnel::deepLinkConfigToToml(junk)
                     .count(QStringLiteral("-----BEGIN CERTIFICATE-----")),
             1);
}

#include "test_deeplink.moc"
