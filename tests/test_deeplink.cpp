// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>
#include <QRandomGenerator>

#include "certificate_vectors.h"
#include "core/ConfigToml.h"
#include "core/DeepLink.h"
#include "deeplink_v2_vectors.h"

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
    void deepLinkCertificatesSaysWhatItDropped_data();
    void deepLinkCertificatesSaysWhatItDropped();
    void encodeDeepLinkWritesOnlyCertificates();
    void aLinkWithAKeyInTag8KeepsTheCertificate();
    void randomCertificateFieldsKeepOnlyCertificates();
    void aMaskedClientRandomReachesTheConfigWhole();
    void aClientRandomTheCoreCannotUseIsRefused();
    void aShareLinkKeepsThePrefixTheCoreUses();

    // Version 2: TrustTunnel endpoint 1.3.0 and later.
    void readsAVersion2LinkWithoutTheNewTags();
    void readsAFullVersion2Link();
    void readsASubscriptionOnlyLink();
    void aSubscriptionAddressMustBeHttps();
    void aSubscriptionAddressHoldsNoControlCharactersOrSpaces();
    void anEmptyAuthKeyIsNoKey();
    void theVersionIsReadBeforeTheFields();
    void aHugeVersionIsNotCutDownToOneThisClientReads();
    void version1LinksReadAsBefore();
    void ourOwnLinksStayVersion1();
    void ourLinkWithAnAuthKeyIsVersion2();
    void theTomlHoldsTheAuthKeyAndNeverTheSubscription();
    void encodesTheLinksTheAndroidAppPins();
    void readsASelfSignedVersion2Link();
    void aVersion2LinkIgnoresTagsItDoesNotDefine();
    void everyVersionRecordCounts();
    void aVersionThatCannotBeReadIsDamage();
};

using v2::linkFromRecords;

// A version 2 link with the required fields and the given subscription address.
static QString linkWithSubscription(const QByteArray &url)
{
    return linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "vpn.example.com"},
                            {0x02, "1.2.3.4:443"}, {0x05, "alice"}, {0x06, "s3cret"},
                            {0x0E, url}});
}

// The tags of a link's records, in order, for what a link carries rather than
// what the parser makes of it.
static QList<quint64> tagsOf(const QString &uri)
{
    const QByteArray p = QByteArray::fromBase64(uri.mid(QStringLiteral("tt://?").size()).toLatin1(),
                                                QByteArray::Base64UrlEncoding);
    QList<quint64> tags;
    int pos = 0;
    while (pos + 1 < p.size()) {
        // Every tag and length our encoder writes for these is under 16384.
        auto varint = [&]() -> quint64 {
            const auto first = static_cast<quint8>(p.at(pos));
            if ((first >> 6) == 0) {
                ++pos;
                return first;
            }
            const quint64 v = (quint64(first & 0x3F) << 8) | static_cast<quint8>(p.at(pos + 1));
            pos += 2;
            return v;
        };
        tags << varint();
        pos += static_cast<int>(varint());
    }
    return tags;
}

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
    // Hand-craft a payload that declares the version after the newest this client
    // reads, plus all required fields. It was 2 until TrustTunnel 1.3.0 made 2
    // the version of every link it writes.
    QByteArray p;
    auto tlv = [&](char tag, const QByteArray &v) {
        p.append(tag);
        p.append(static_cast<char>(v.size()));
        p.append(v);
    };
    QCOMPARE(kDeepLinkMaxVersion, 2);
    tlv(0x00, QByteArray(1, 3)); // version = 3 (> kDeepLinkMaxVersion); fits one varint byte
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
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));

    // The endpoint's own version 3 vector, which is otherwise a good link.
    QVERIFY(!parseDeepLink(v2::kVersion3, &err).has_value());
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));

    // And the same payload one version down is read.
    p[2] = 2;
    QVERIFY2(parseDeepLink("tt://?" + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                                                      | QByteArray::OmitTrailingEquals)),
                           &err)
                     .has_value(),
             qPrintable(err));
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

// The seed of the two random loops below. They used to draw from
// QRandomGenerator::global(), seeded afresh by every run and never printed, so an
// input the sanitizer job crashed on could not be fed in again. A fixed seed
// repeats the same inputs on every run; FT_FUZZ_SEED picks other ones. Either
// way the seed is printed, and the log of a failed run says how to repeat it.
static quint32 fuzzSeed(const char *loop, quint32 fixed)
{
    bool fromEnv = false;
    const quint32 env = qEnvironmentVariable("FT_FUZZ_SEED").toUInt(&fromEnv);
    const quint32 seed = fromEnv ? env : fixed;
    qInfo("%s: seed %u (FT_FUZZ_SEED=%u repeats it)", loop, seed, seed);
    return seed;
}

// Random TLV-shaped byte streams (not necessarily valid base64url payloads).
void TestDeepLink::structuredTlvFuzzNeverCrash()
{
    QRandomGenerator seeded(fuzzSeed("structuredTlvFuzzNeverCrash", 20250611u));
    auto *rng = &seeded;
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
    // Our own link, and version 2 links as TrustTunnel 1.3 writes them: the tags
    // it added go through the parser's new paths, which a version 1 seed never
    // reaches.
    const QStringList seeds{encodeDeepLink(in), v2::kFull, v2::kSubscriptionOnly, v2::kAuthKey,
                            v2::kSelfSigned, v2::kUnknownTags, v2::kTwoVersions};

    QRandomGenerator seeded(fuzzSeed("mutatesValidLinksSafely", 20250612u));
    auto *rng = &seeded;
    for (int i = 0; i < 500 * seeds.size(); ++i) {
        const QString payload = seeds.at(i % seeds.size()).mid(QStringLiteral("tt://?").size());
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
// side means it: certificatesDer() concatenates the DER of every certificate it
// finds. The import side wrapped the whole blob in one BEGIN/END pair, so a
// config pinning a leaf and its intermediate came back through its OWN share
// link as a single block that is not a certificate at all. Nothing caught it
// because no test here ever set a certificate.
void TestDeepLink::aCertificateChainSurvivesTheLink()
{
    // The outer shape of a certificate rather than real ones: what broke is the
    // framing, and framing is what this checks. One short-form length, one long.
    // These were bare SEQUENCEs of filler before, which a link keeps no longer:
    // a private key is a SEQUENCE as well.
    freetunnel::DeepLinkConfig cfg;
    cfg.version = freetunnel::kDeepLinkMaxVersion;
    cfg.hostname = QStringLiteral("vpn.example.org");
    cfg.addresses = {QStringLiteral("1.2.3.4:443")};
    cfg.username = QStringLiteral("u");
    cfg.password = QStringLiteral("p");
    cfg.certificate = certs::certShaped(300) + certs::certShaped(100);

    QString err;
    const auto back = freetunnel::parseDeepLink(freetunnel::encodeDeepLink(cfg), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->certificate, cfg.certificate);

    const QString toml = freetunnel::deepLinkConfigToToml(*back);
    QCOMPARE(toml.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 2);
    QCOMPARE(toml.count(QStringLiteral("-----END CERTIFICATE-----")), 2);

    // A single certificate still comes out as exactly one block.
    freetunnel::DeepLinkConfig one = cfg;
    one.certificate = certs::certShaped(64);
    QCOMPARE(freetunnel::deepLinkConfigToToml(one)
                     .count(QStringLiteral("-----BEGIN CERTIFICATE-----")),
             1);

    // A blob that is not a sequence of elements used to be emitted whole, as one
    // "certificate". A link that carries one is refused as damaged now, as
    // TrustTunnel's own decoder refuses it, and nothing of it reaches a config.
    const QString junkLink = v2::linkFromRecords({{0x01, "vpn.example.org"}, {0x02, "1.2.3.4:443"},
                                                  {0x05, "u"}, {0x06, "p"}, {0x08, "not der at all"}});
    QVERIFY(!freetunnel::parseDeepLink(junkLink, &err).has_value());
    QCOMPARE(err, QStringLiteral("malformed certificate"));
    freetunnel::DeepLinkConfig junk = cfg;
    junk.certificate = QByteArray("not der at all");
    const QString junkToml = freetunnel::deepLinkConfigToToml(junk);
    QVERIFY2(!junkToml.contains(QStringLiteral("BEGIN")), qPrintable(junkToml));
    QVERIFY2(junkToml.contains(QStringLiteral("certificate = \"\"\n")), qPrintable(junkToml));
    // Nor does a certificate with damage after it take the damage along.
    junk.certificate = certs::certShaped(64) + QByteArray("\x30\x05", 2);
    QCOMPARE(freetunnel::deepLinkConfigToToml(junk).count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
}

// The certificates of a certificate field, and what else was there.
void TestDeepLink::deepLinkCertificatesSaysWhatItDropped_data()
{
    QTest::addColumn<QByteArray>("field");
    QTest::addColumn<QByteArray>("kept");
    QTest::addColumn<bool>("dropped");
    const QByteArray ca = certs::derOf(certs::kCaPem);
    const QByteArray leaf = certs::derOf(certs::kLeafPem);
    QTest::newRow("nothing") << QByteArray() << QByteArray() << false;
    QTest::newRow("a certificate") << ca << ca << false;
    QTest::newRow("a chain") << leaf + ca << leaf + ca << false;
    QTest::newRow("a certificate and its key") << leaf + certs::kPkcs8Der << leaf << true;
    QTest::newRow("a key inside a chain") << leaf + certs::kSec1Der + ca << leaf + ca << true;
    QTest::newRow("a key alone") << certs::kPkcs8Der << QByteArray() << true;
    QTest::newRow("a SEQUENCE of an INTEGER") << QByteArray::fromHex("3003020101") << QByteArray() << true;
    QTest::newRow("bytes after a certificate") << ca + QByteArray("junk") << ca << true;
    QTest::newRow("not DER at all") << QByteArray("junk") << QByteArray() << true;
}

void TestDeepLink::deepLinkCertificatesSaysWhatItDropped()
{
    QFETCH(QByteArray, field);
    QFETCH(QByteArray, kept);
    QFETCH(bool, dropped);
    bool lost = !dropped;
    QCOMPARE(deepLinkCertificates(field, &lost), kept);
    QCOMPARE(lost, dropped);
    QCOMPARE(deepLinkCertificates(kept, &lost), kept);
    QVERIFY(!lost);
}

// No caller of encodeDeepLink() can put a key into a link: the config it is given
// may hold one beside its certificate, and the link carries the certificate alone.
void TestDeepLink::encodeDeepLinkWritesOnlyCertificates()
{
    const QByteArray leaf = certs::derOf(certs::kLeafPem);
    DeepLinkConfig in;
    in.hostname = QStringLiteral("vpn.fixture.invalid");
    in.addresses = {QStringLiteral("1.2.3.4:443")};
    in.username = QStringLiteral("u");
    in.password = QStringLiteral("p");
    in.certificate = certs::kPkcs8Der + leaf + certs::kEncryptedPkcs8Der;
    QString err;
    // The parser keeps tag 0x08 as the link has it, so this is what the link holds.
    auto back = parseDeepLink(encodeDeepLink(in), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->certificate, leaf);

    // Nothing but a key: no tag 0x08 at all.
    in.certificate = certs::kPkcs8Der;
    const QString keyOnly = encodeDeepLink(in);
    QVERIFY(!tagsOf(keyOnly).contains(0x08));
    back = parseDeepLink(keyOnly, &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QVERIFY(back->certificate.isEmpty());
}

// A link someone made by hand, with a key beside the certificate in tag 0x08. It
// is read as it is, and the config it becomes has the certificate alone.
void TestDeepLink::aLinkWithAKeyInTag8KeepsTheCertificate()
{
    const QByteArray leaf = certs::derOf(certs::kLeafPem);
    const QString link = v2::linkFromRecords({{0x00, QByteArray(1, 1)}, {0x01, "vpn.fixture.invalid"},
                                              {0x02, "1.2.3.4:443"}, {0x05, "u"}, {0x06, "p"},
                                              {0x08, leaf + certs::kPkcs8Der}});
    QString err;
    const auto cfg = parseDeepLink(link, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->certificate, leaf + certs::kPkcs8Der);
    bool dropped = false;
    QCOMPARE(deepLinkCertificates(cfg->certificate, &dropped), leaf);
    QVERIFY(dropped);
    const QString toml = deepLinkConfigToToml(*cfg);
    QCOMPARE(toml.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
    QVERIFY2(!toml.contains(certs::keyLabel()), qPrintable(toml));
    QVERIFY2(!toml.contains(QString::fromLatin1(certs::kPkcs8Der.toBase64())), qPrintable(toml));
    QCOMPARE(parseConfigToml(toml).certificate.trimmed(), certs::kLeafPem.trimmed());
}

// Random bytes as a certificate field: nothing crashes, only certificates are
// kept, and what is kept is kept as it is.
void TestDeepLink::randomCertificateFieldsKeepOnlyCertificates()
{
    QRandomGenerator rng(fuzzSeed("randomCertificateFieldsKeepOnlyCertificates", 20261026u));
    const QList<QByteArray> pieces{certs::derOf(certs::kCaPem), certs::kPkcs8Der, certs::kSec1Der,
                                   certs::certShaped(3), QByteArray::fromHex("3003020101")};
    for (int i = 0; i < 3000; ++i) {
        QByteArray field;
        const int count = rng.bounded(0, 4);
        for (int j = 0; j < count; ++j)
            field += pieces.at(rng.bounded(static_cast<int>(pieces.size())));
        if (!field.isEmpty() && rng.bounded(2) == 0)
            field[rng.bounded(static_cast<int>(field.size()))] = static_cast<char>(rng.generate() & 0xFF);
        if (rng.bounded(4) == 0)
            field += static_cast<char>(rng.generate() & 0xFF);
        bool dropped = false;
        const QByteArray kept = deepLinkCertificates(field, &dropped);
        QVERIFY(dropped || kept == field);
        bool again = true;
        QVERIFY(deepLinkCertificates(kept, &again) == kept && !again);
        DeepLinkConfig cfg;
        cfg.certificate = field;
        const QString toml = deepLinkConfigToToml(cfg);
        QVERIFY(certificatesDer(parseConfigToml(toml).certificate) == kept);
    }
}

// Every link TrustTunnel's endpoint exports from 1.3.0 on is version 2, the ones
// with nothing new in them too, and every one was refused as "unsupported deep
// link version 2".
void TestDeepLink::readsAVersion2LinkWithoutTheNewTags()
{
    QString err;
    const auto cfg = parseDeepLink(v2::kPlain, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 2);
    QCOMPARE(cfg->hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(cfg->addresses, QStringList{QStringLiteral("1.2.3.4:443")});
    QCOMPARE(cfg->username, QStringLiteral("alice"));
    QCOMPARE(cfg->password, QStringLiteral("secret"));
    QVERIFY(cfg->subscriptionUrl.isEmpty());
    QVERIFY(cfg->clientRandomAuthKey.isEmpty());
    QVERIFY(deepLinkHasStaticServer(*cfg));
}

void TestDeepLink::readsAFullVersion2Link()
{
    QString err;
    const auto cfg = parseDeepLink(v2::kFull, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 2);
    QCOMPARE(cfg->hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(cfg->addresses, (QStringList{QStringLiteral("1.2.3.4:443"), QStringLiteral("[2001:db8::1]:443")}));
    QCOMPARE(cfg->username, QStringLiteral("alice"));
    QCOMPARE(cfg->password, QStringLiteral("s3cret"));
    QCOMPARE(cfg->clientRandomPrefix, QStringLiteral("aabbcc/ff00ff"));
    QCOMPARE(cfg->customSni, QStringLiteral("example.org"));
    QCOMPARE(cfg->hasIpv6, false);
    QCOMPARE(cfg->antiDpi, true);
    QVERIFY(cfg->upstreamProtocol == UpstreamProtocol::Http3);
    QCOMPARE(cfg->name, QStringLiteral("Example"));
    QCOMPARE(cfg->dnsUpstreams, (QStringList{QStringLiteral("1.1.1.1"), QStringLiteral("tls://dns.example.com")}));
    QCOMPARE(cfg->subscriptionUrl, v2::kSubscription);
    QVERIFY(deepLinkHasStaticServer(*cfg));
}

// The format lets a link with a subscription address leave out every other
// field. It parses; that it names no server is for the import to act on.
void TestDeepLink::readsASubscriptionOnlyLink()
{
    QString err;
    const auto cfg = parseDeepLink(v2::kSubscriptionOnly, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->subscriptionUrl, v2::kSubscription);
    QVERIFY(cfg->hostname.isEmpty());
    QVERIFY(cfg->addresses.isEmpty());
    QVERIFY(!deepLinkHasStaticServer(*cfg));

    // A server only half there is no server either: TrustTunnel's decoder takes an
    // empty field as a missing one.
    const auto half = parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "vpn.example.com"},
                                                     {0x02, "1.2.3.4:443"}, {0x05, "alice"}, {0x06, ""},
                                                     {0x0E, v2::kSubscription.toUtf8()}}),
                                    &err);
    QVERIFY2(half.has_value(), qPrintable(err));
    QVERIFY(!deepLinkHasStaticServer(*half));

    // Without the address the required fields are required, as in version 1.
    QVERIFY(!parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "vpn.example.com"}}), &err)
                     .has_value());
    QVERIFY2(err.contains(QStringLiteral("required")), qPrintable(err));
}

// The format's one rule for the address, and the one that matters: it is fetched
// with the VPN password in it.
void TestDeepLink::aSubscriptionAddressMustBeHttps()
{
    QString err;
    QVERIFY(!parseDeepLink(v2::kHttpSubscription, &err).has_value());
    QCOMPARE(err, QStringLiteral("subscription_url is not a valid https:// address"));

    // With a whole server beside it, too: the link is refused, not half read.
    for (const QByteArray &bad : {QByteArray("http://alice:s3cret@vpn.example.com/subscription"),
                                  QByteArray("HTTPS://alice:s3cret@vpn.example.com/subscription"),
                                  QByteArray("ftp://vpn.example.com/s3cret"), QByteArray("vpn.example.com"),
                                  // The scheme without both slashes is not the prefix.
                                  QByteArray("https:vpn.example.com/s"), QByteArray("https:/vpn.example.com/s"),
                                  QByteArray("")}) {
        QVERIFY2(!parseDeepLink(linkWithSubscription(bad), &err).has_value(), bad.constData());
        QVERIFY2(err.contains(QStringLiteral("https://")), qPrintable(err));
        // The address holds the password: no error repeats it.
        QVERIFY2(!err.contains(QStringLiteral("s3cret")), qPrintable(err));
    }
}

void TestDeepLink::aSubscriptionAddressHoldsNoControlCharactersOrSpaces()
{
    const QByteArray base("https://alice:s3cret@vpn.example.com/subscription");
    // Each is refused wherever it sits; the middle is as good a place as any.
    const QList<QByteArray> inserted{
            QByteArray("\n"),         QByteArray("\r"),          QByteArray("\t"),
            QByteArray(" "),          QByteArray(1, '\0'),       QByteArray("\x7f"),
            QByteArray("\xc2\x85"),   // U+0085, a C1 control
            QByteArray("\xc2\xa0"),   // U+00A0, a no-break space
            QByteArray("\xe2\x80\xa8"), // U+2028, a line separator
            QByteArray("\xe2\x80\x8b"), // U+200B, a zero-width space (a format character)
            QByteArray("\xe2\x80\xae"), // U+202E, right-to-left override
            QByteArray("\xf3\xa0\x80\x81"), // U+E0001, a format character outside the BMP
            QByteArray("\xff"),       // not UTF-8 at all
    };
    QString err;
    for (const QByteArray &bad : inserted) {
        const QByteArray url = base.left(20) + bad + base.mid(20);
        QVERIFY2(!parseDeepLink(linkWithSubscription(url), &err).has_value(), bad.toHex().constData());
        QCOMPARE(err, QStringLiteral("subscription_url is not a valid https:// address"));
    }

    // A letter outside ASCII is not one of them.
    QVERIFY2(parseDeepLink(linkWithSubscription(base + "/\xd0\xbf\xd1\x83\xd1\x82\xd1\x8c"), &err).has_value(),
             qPrintable(err));

    // At most kMaxSubscriptionUrlBytes, counted in bytes.
    const QByteArray longest = base + "?" + QByteArray(kMaxSubscriptionUrlBytes - base.size() - 1, 'a');
    QCOMPARE(longest.size(), kMaxSubscriptionUrlBytes);
    QVERIFY2(parseDeepLink(linkWithSubscription(longest), &err).has_value(), qPrintable(err));
    QVERIFY(!parseDeepLink(linkWithSubscription(longest + "a"), &err).has_value());
    QCOMPARE(err, QStringLiteral("subscription_url is not a valid https:// address"));
}

// The endpoint writes an auth key only when there is one; a link that has the
// tag empty is a link without a key. Anything else has to be whole bytes of hex,
// as TrustTunnel's own decoder takes it.
void TestDeepLink::anEmptyAuthKeyIsNoKey()
{
    QString err;
    auto cfg = parseDeepLink(v2::kEmptyAuthKey, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QVERIFY(cfg->clientRandomAuthKey.isEmpty());

    cfg = parseDeepLink(v2::kAuthKey, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->clientRandomAuthKey, QStringLiteral("aabbccdd"));
    cfg = parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "h"}, {0x02, "1.2.3.4:443"},
                                         {0x05, "u"}, {0x06, "p"}, {0x0F, "AABBccdd"}}),
                        &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    // Each end of each range of digits.
    cfg = parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "h"}, {0x02, "1.2.3.4:443"},
                                         {0x05, "u"}, {0x06, "p"}, {0x0F, "09afAF"}}),
                        &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->clientRandomAuthKey, QStringLiteral("09afAF"));

    // And the character just past each end: '/' and ':' around the digits, '@' and
    // 'G' around A to F, '`' and 'g' around a to f.
    for (const QByteArray &bad : {QByteArray("abc"), QByteArray("zz"), QByteArray("aa bb"), QByteArray(" aabb"),
                                  QByteArray("/0"), QByteArray(":0"), QByteArray("@0"), QByteArray("G0"),
                                  QByteArray("`0"), QByteArray("g0")}) {
        QVERIFY2(!parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "h"}, {0x02, "1.2.3.4:443"},
                                                 {0x05, "u"}, {0x06, "p"}, {0x0F, bad}}),
                                &err)
                          .has_value(),
                 bad.constData());
        QCOMPARE(err, QStringLiteral("malformed client_random_auth_key value"));
    }
}

// A newer format is free to mean something else by a field, so a link in one is
// refused for its version, whatever its fields hold and wherever the version
// sits among them. Read in one pass, a version 3 link with an http:// address
// was refused as having a bad address.
void TestDeepLink::theVersionIsReadBeforeTheFields()
{
    QString err;
    QVERIFY(!parseDeepLink(linkFromRecords({{0x0E, "http://vpn.example.com/subscription"},
                                            {0x0B, "not hex"},
                                            {0x00, QByteArray(1, 3)}}),
                           &err)
                     .has_value());
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));

    // Damage to the records themselves still comes first: there is no reading a
    // version out of them.
    QVERIFY(!parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 3)}}) + QStringLiteral("AQ"), &err)
                     .has_value());
    QVERIFY2(!err.contains(QStringLiteral("version")), qPrintable(err));
}

// The version is a varint of up to 62 bits, and an int holds 31. Cut to its low
// bits, 2^32 + 1 read as 1, and a link of a format this client has never heard
// of was read as one of its own.
void TestDeepLink::aHugeVersionIsNotCutDownToOneThisClientReads()
{
    // An 8-byte varint: 0xC0, then the low 56 bits of 0x0000000100000001.
    const QByteArray huge = QByteArray::fromHex("c000000100000001");
    QCOMPARE(huge.size(), 8);
    QString err;
    QVERIFY(!parseDeepLink(linkFromRecords({{0x00, huge}, {0x01, "h"}, {0x02, "1.2.3.4:443"},
                                            {0x05, "u"}, {0x06, "p"}}),
                           &err)
                     .has_value());
    QVERIFY2(err.contains(QStringLiteral("version")), qPrintable(err));
}

// The example in DEEP_LINK.md, which every FreeTunnel share link looks like.
void TestDeepLink::version1LinksReadAsBefore()
{
    QString err;
    const auto cfg = parseDeepLink(QStringLiteral("tt://?AAEBAQ92cG4uZXhhbXBsZS5jb20CEDIwMy4wLjExMy4xMDo0ND"
                                                  "MFBWFsaWNlBgZzZWNyZXQMB0V4YW1wbGU"),
                                   &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 1);
    QCOMPARE(cfg->hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(cfg->addresses, QStringList{QStringLiteral("203.0.113.10:443")});
    QCOMPARE(cfg->username, QStringLiteral("alice"));
    QCOMPARE(cfg->password, QStringLiteral("secret"));
    QCOMPARE(cfg->name, QStringLiteral("Example"));
    QVERIFY(cfg->subscriptionUrl.isEmpty());
    QVERIFY(cfg->clientRandomAuthKey.isEmpty());

    // A version 1 link still has to carry its server: the subscription is what
    // excuses the fields, not the version number.
    QVERIFY(!parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 1)}, {0x01, "vpn.example.com"}}), &err)
                     .has_value());
}

// Version 2 adds tags our links carry only when they have to, and FreeTunnel
// 1.2.3 and earlier, like TrustTunnel's clients up to 1.1.11, refuse a version 2
// link outright. So a share link stays version 1, whatever the config it came
// from was imported from, unless it carries an auth key (ourLinkWithAnAuthKeyIsVersion2).
void TestDeepLink::ourOwnLinksStayVersion1()
{
    QCOMPARE(kDeepLinkWriteVersion, 1);
    QVERIFY(kDeepLinkWriteVersion < kDeepLinkMaxVersion);

    QString err;
    auto cfg = parseDeepLink(v2::kFull, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    cfg->version = kDeepLinkMaxVersion; // what the struct says is not what is written
    const QString ours = encodeDeepLink(*cfg);

    const QByteArray payload = QByteArray::fromBase64(ours.mid(QStringLiteral("tt://?").size()).toLatin1(),
                                                      QByteArray::Base64UrlEncoding);
    QCOMPARE(payload.left(3), QByteArray("\x00\x01\x01", 3)); // tag 0, length 1, version 1
    const QList<quint64> tags = tagsOf(ours);
    QVERIFY2(!tags.contains(0x0E) && !tags.contains(0x0F), qPrintable(ours));

    // And it is the same server.
    const auto back = parseDeepLink(ours, &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->version, 1);
    QCOMPARE(back->hostname, cfg->hostname);
    QCOMPARE(back->addresses, cfg->addresses);
    QCOMPARE(back->password, cfg->password);
    QVERIFY(back->subscriptionUrl.isEmpty());
    QVERIFY(back->clientRandomAuthKey.isEmpty());
}

// With the key, though: the link is the only way it reaches the config. The
// import moves it from the TOML to the credential store, with the password.
void TestDeepLink::ourLinkWithAnAuthKeyIsVersion2()
{
    QCOMPARE(kDeepLinkAuthKeyWriteVersion, 2);
    DeepLinkConfig c;
    c.hostname = QStringLiteral("vpn.example.com");
    c.addresses = {QStringLiteral("1.2.3.4:443")};
    c.username = QStringLiteral("alice");
    c.password = QStringLiteral("secret");
    // Without a key: version 1, byte for byte as before.
    QCOMPARE(encodeDeepLink(c), v2::kWrittenWithoutAuthKey);
    // With one, and not a byte more: version 2 and the key as the last record.
    c.clientRandomAuthKey = QStringLiteral("aabbccdd");
    c.version = 1; // the struct's own version is not what is written
    QCOMPARE(encodeDeepLink(c), v2::kWrittenWithAuthKey);

    QString err;
    const auto back = parseDeepLink(encodeDeepLink(c), &err);
    QVERIFY2(back.has_value(), qPrintable(err));
    QCOMPARE(back->version, 2);
    QCOMPARE(back->clientRandomAuthKey, QStringLiteral("aabbccdd"));
    QCOMPARE(back->hostname, c.hostname);
    QCOMPARE(back->password, c.password);
    QVERIFY(back->subscriptionUrl.isEmpty());

    // A key and an address: still no subscription address, which a link of ours
    // never carries.
    c.subscriptionUrl = v2::kSubscription;
    QVERIFY(parseDeepLink(encodeDeepLink(c))->subscriptionUrl.isEmpty());
    QVERIFY2(!tagsOf(encodeDeepLink(c)).contains(0x0E), qPrintable(encodeDeepLink(c)));
    // Every record is there once, the key last.
    const QList<quint64> tags = tagsOf(encodeDeepLink(c));
    QCOMPARE(tags.last(), quint64(0x0F));
    QCOMPARE(tags.count(0x0F), 1);
}

// The Android app writes the same share links (DeepLinkImport.encode in android/), and its test
// pins these two strings, byte for byte. They are what this encoder writes for the two servers
// below, so that a change to either encoder that changes the bytes fails a test on both sides.
void TestDeepLink::encodesTheLinksTheAndroidAppPins()
{
    DeepLinkConfig full;
    full.hostname = "vpn.example.com";
    full.addresses = {"1.2.3.4:443", "[2001:db8::1]:443"};
    full.customSni = "example.org";
    full.hasIpv6 = false;
    full.username = "premium";
    full.password = "s3cretPass";
    full.skipVerification = true;
    full.upstreamProtocol = UpstreamProtocol::Http3;
    full.antiDpi = true;
    full.clientRandomPrefix = "deadbeef/ffff";
    full.name = "My Server";
    full.dnsUpstreams = {"1.1.1.1", "tls://dns.example.com"};
    QCOMPARE(encodeDeepLink(full),
             QStringLiteral("tt://?AAEBAQ92cG4uZXhhbXBsZS5jb20CCzEuMi4zLjQ6NDQzAhFbMjAwMTpkYjg6OjFdOjQ0MwUHcHJlbWl1bQYKczNjcmV0"
                            "UGFzcwQBAAcBAQoBAQMLZXhhbXBsZS5vcmcJAQILDWRlYWRiZWVmL2ZmZmYMCU15IFNlcnZlcg0eBzEuMS4xLjEVdGxzOi8vZG5zLmV4YW1wbGUuY29t"));

    DeepLinkConfig plain;
    plain.hostname = "frankfurt.example.com";
    plain.addresses = {"203.0.113.10:443"};
    plain.username = "user";
    plain.password = "pass";
    plain.name = QStringLiteral("DE \u00b7 Frankfurt");
    QCOMPARE(encodeDeepLink(plain),
             QStringLiteral("tt://?AAEBARVmcmFua2Z1cnQuZXhhbXBsZS5jb20CEDIwMy4wLjExMy4xMDo0NDMFBHVzZXIGBHBhc3MMD0RFIMK3IEZyYW5rZnVydA"));
}

// The subscription address carries the VPN password in plain text; what a link
// becomes on disk never holds it. The auth key it does hold, for the import to
// move to the credential store with the password; a link without one writes none.
void TestDeepLink::theTomlHoldsTheAuthKeyAndNeverTheSubscription()
{
    QString err;
    auto cfg = parseDeepLink(v2::kFull, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QVERIFY2(!deepLinkConfigToToml(*cfg).contains(QStringLiteral("auth_key")),
             qPrintable(deepLinkConfigToToml(*cfg)));
    cfg->clientRandomAuthKey = QStringLiteral("aabbccdd");
    const QString toml = deepLinkConfigToToml(*cfg);
    QVERIFY2(!toml.contains(QStringLiteral("subscription")), qPrintable(toml));
    QVERIFY2(!toml.contains(QStringLiteral("https://alice")), qPrintable(toml));
    QVERIFY2(toml.contains(QStringLiteral("client_random_auth_key = \"aabbccdd\"\n")), qPrintable(toml));
    QVERIFY2(toml.contains(QStringLiteral("hostname = \"vpn.example.com\"")), qPrintable(toml));
    QCOMPARE(parseConfigToml(toml).clientRandomAuthKey, QStringLiteral("aabbccdd"));
}

// What the endpoint exports for every server whose certificate the system does
// not trust: the certificate rides in the link, before the subscription address.
void TestDeepLink::readsASelfSignedVersion2Link()
{
    QString err;
    const auto cfg = parseDeepLink(v2::kSelfSigned, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 2);
    QCOMPARE(cfg->certificate, v2::kSelfSignedCertificate);
    QCOMPARE(cfg->subscriptionUrl, v2::kSubscription);
    QVERIFY(deepLinkHasStaticServer(*cfg));
    const QString toml = deepLinkConfigToToml(*cfg);
    QCOMPARE(toml.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
    QCOMPARE(certificatesDer(parseConfigToml(toml).certificate), v2::kSelfSignedCertificate);
    QVERIFY2(!toml.contains(QStringLiteral("subscription")), qPrintable(toml));
}

// The format's rule for what comes next: a tag a reader does not know is skipped,
// in version 2 as in 1. TrustTunnel's next tag will be 0x10, and refusing it would
// refuse every link that has it.
void TestDeepLink::aVersion2LinkIgnoresTagsItDoesNotDefine()
{
    QString err;
    const auto cfg = parseDeepLink(v2::kUnknownTags, &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 2);
    QCOMPARE(cfg->hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(cfg->addresses, QStringList{QStringLiteral("1.2.3.4:443")});
    QCOMPARE(cfg->username, QStringLiteral("alice"));
    QCOMPARE(cfg->password, QStringLiteral("s3cret"));
    QCOMPARE(cfg->subscriptionUrl, v2::kSubscription);
    QVERIFY(cfg->clientRandomAuthKey.isEmpty());

    // Every one-byte tag after the last one defined, alone beside a whole server.
    for (int tag = 0x10; tag <= 0x3F; ++tag) {
        const QString link = linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "h"}, {0x02, "1.2.3.4:443"},
                                              {0x05, "u"}, {0x06, "p"}, {static_cast<char>(tag), "x"}});
        QVERIFY2(parseDeepLink(link, &err).has_value(), qPrintable(QString::number(tag, 16) + ": " + err));
    }
}

// TrustTunnel's decoder refuses a link if any version record is above what it
// reads, wherever it sits. A link that says 3 and 2 is a version 3 link, and the
// version a link is read as is the one its fields were checked for.
void TestDeepLink::everyVersionRecordCounts()
{
    QString err;
    QVERIFY(!parseDeepLink(v2::kTwoVersions, &err).has_value());
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));

    const auto fields = [](const QByteArray &first, const QByteArray &second) {
        return linkFromRecords({{0x00, first}, {0x00, second}, {0x01, "h"}, {0x02, "1.2.3.4:443"},
                                {0x05, "u"}, {0x06, "p"}});
    };
    QVERIFY(!parseDeepLink(fields(QByteArray(1, 2), QByteArray(1, 3)), &err).has_value());
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));

    const auto cfg = parseDeepLink(fields(QByteArray(1, 2), QByteArray(1, 1)), &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 2);
}

// A version record whose value is not a varint says no version at all. Read as
// version 0, it let a link of any format through as one of the first.
void TestDeepLink::aVersionThatCannotBeReadIsDamage()
{
    QString err;
    for (const QString &link : {v2::kEmptyVersion, v2::kTruncatedVersion}) {
        QVERIFY2(!parseDeepLink(link, &err).has_value(), qPrintable(link));
        QCOMPARE(err, QStringLiteral("malformed version value"));
    }

    // Beside a version this client reads, too: no record of it is passed over.
    QVERIFY(!parseDeepLink(linkFromRecords({{0x00, QByteArray(1, 2)}, {0x00, QByteArray()}, {0x01, "h"},
                                            {0x02, "1.2.3.4:443"}, {0x05, "u"}, {0x06, "p"}}),
                           &err)
                     .has_value());
    QCOMPARE(err, QStringLiteral("malformed version value"));

    // The bytes after a whole varint are not looked at, as TrustTunnel's decoder
    // does not look at them.
    const auto cfg = parseDeepLink(linkFromRecords({{0x00, QByteArray("\x02\xff", 2)}, {0x01, "h"},
                                                    {0x02, "1.2.3.4:443"}, {0x05, "u"}, {0x06, "p"}}),
                                   &err);
    QVERIFY2(cfg.has_value(), qPrintable(err));
    QCOMPARE(cfg->version, 2);
}

#include "test_deeplink.moc"
