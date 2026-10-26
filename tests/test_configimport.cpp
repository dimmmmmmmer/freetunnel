// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "certificate_vectors.h"
#include "core/ConfigToml.h"
#include "core/DeepLink.h"
#include "deeplink_v2_vectors.h"
#include "endpoint_export_vectors.h"

using namespace freetunnel;

class TestConfigImport : public QObject {
    Q_OBJECT

private slots:
    void importsValidLink();
    void skipVerificationFlagPropagates();
    void fileNameFromServerName();
    void fileNameSanitized();
    void aLongNameIsCutToTheLimit();
    void rejectsInvalidLink();

    // Version 2: TrustTunnel endpoint 1.3.0 and later.
    void importsAVersion2Link();
    void importsTheServerOfASubscriptionLinkWithoutTheAddress();
    void refusesASubscriptionOnlyLink();
    void refusesAServerThatNeedsAnAuthKey();
    void aDamagedLinkIsSaidToBeDamaged();
    void aSubscriptionLinkNeedsEveryServerField_data();
    void aSubscriptionLinkNeedsEveryServerField();
    void anAuthKeyIsRefusedBeforeAMissingServer();
    void importsASelfSignedSubscriptionLink();
    void importsALinkWithTagsItDoesNotKnow();
    void aTwoVersionLinkIsRefusedForTheNewer();

    // A certificate keeps only its certificates.
    void aLinkCarryingAKeyImportsTheCertificateAlone();
    void aLinkWhoseCertificateHoldsNoCertificateIsDamaged();

    // The TOML file an endpoint exports.
    void anEndpointExportImportsAsTheLinkWithItsServerDoes();
    void anEndpointExportIsNamedByItsFileWhenItHasNoName();
    void anEndpointExportLeavesItsSubscriptionAddressOut();
    void anEndpointExportKeepsOnlyItsCertificates();
    void anEndpointExportIsRefusedAsALinkIs();
    void anEndpointExportsLinkGivesTheSameConfig();
    void everyExportAskedAboutCanBeAnswered();
};

// A link with a whole server and the given certificate field (tag 0x08).
static QString linkWithCertificate(const QByteArray &certificate)
{
    return v2::linkFromRecords({{0x00, QByteArray(1, 1)}, {0x01, "vpn.fixture.invalid"},
                                {0x02, "1.2.3.4:443"}, {0x05, "u"}, {0x06, "p"}, {0x08, certificate}});
}

static QString makeLink(const QString &name = QString()) {
    DeepLinkConfig c;
    c.hostname = "vpn.example.com";
    c.addresses = {"1.2.3.4:443"};
    c.username = "u";
    c.password = "p";
    c.name = name;
    return encodeDeepLink(c);
}

void TestConfigImport::importsValidLink() {
    QString err;
    auto out = prepareDeepLinkImport(makeLink(), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->fileName.endsWith(".toml"));
    QVERIFY(out->tomlContent.contains("[endpoint]"));
    QVERIFY(out->tomlContent.contains("hostname = \"vpn.example.com\""));
    QVERIFY(!out->skipVerification);
}

void TestConfigImport::skipVerificationFlagPropagates() {
    DeepLinkConfig c;
    c.hostname = "vpn.example.com";
    c.addresses = {"1.2.3.4:443"};
    c.username = "u";
    c.password = "p";
    c.skipVerification = true;
    auto out = prepareDeepLinkImport(encodeDeepLink(c));
    QVERIFY(out.has_value());
    QVERIFY(out->skipVerification);
    QVERIFY(out->tomlContent.contains("skip_verification = true"));
}

void TestConfigImport::fileNameFromServerName() {
    auto out = prepareDeepLinkImport(makeLink("My Server"));
    QVERIFY(out.has_value());
    // the name as typed, .toml appended
    QCOMPARE(out->fileName, QStringLiteral("My Server.toml"));
}

void TestConfigImport::fileNameSanitized() {
    // no display name -> falls back to hostname
    auto out = prepareDeepLinkImport(makeLink());
    QVERIFY(out.has_value());
    QCOMPARE(out->fileName, QStringLiteral("vpn.example.com.toml"));
}

// A link can carry a name of any length, and one too long to be a file name
// failed to import with "Could not write config". It is cut instead.
void TestConfigImport::aLongNameIsCutToTheLimit() {
    const QString longName = QString(130, QChar(0x0416)); // 260 bytes as a file name
    auto out = prepareDeepLinkImport(makeLink(longName));
    QVERIFY(out.has_value());
    QCOMPARE(out->fileName, longName.left(freetunnel::kMaxConfigNameLength) + QStringLiteral(".toml"));
    // The whole name too, where a config imported before the cut still is.
    QCOMPARE(out->unclippedFileName, longName + QStringLiteral(".toml"));
    QVERIFY(prepareDeepLinkImport(makeLink(QStringLiteral("Work")))->unclippedFileName.isEmpty());
}

void TestConfigImport::rejectsInvalidLink() {
    QString err;
    QVERIFY(!prepareDeepLinkImport("https://nope", &err).has_value());
    QVERIFY(!err.isEmpty());
}

void TestConfigImport::importsAVersion2Link() {
    QString err;
    LinkRefusal why = LinkRefusal::NeedsNewerCore; // anything but what it will say
    const auto out = prepareDeepLinkImport(v2::kPlain, &err, &why);
    QVERIFY2(out.has_value(), qPrintable(err));
    QCOMPARE(why, LinkRefusal::Damaged); // set even on success, so it is never stale
    QCOMPARE(out->fileName, QStringLiteral("vpn.example.com.toml"));
    QVERIFY(out->tomlContent.contains(QStringLiteral("hostname = \"vpn.example.com\"")));
    QVERIFY(!out->subscriptionNotKept);

    // An empty auth key is no key.
    QVERIFY(prepareDeepLinkImport(v2::kEmptyAuthKey, &err).has_value());
}

// A link with its server and a subscription address imports the server. The
// address holds the VPN password in plain text and FreeTunnel cannot follow it
// yet, so it is not kept, and the import says so (subscriptionNotKept).
void TestConfigImport::importsTheServerOfASubscriptionLinkWithoutTheAddress() {
    QString err;
    const auto out = prepareDeepLinkImport(v2::kFull, &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->subscriptionNotKept);
    QCOMPARE(out->fileName, QStringLiteral("Example.toml"));
    QVERIFY(out->tomlContent.contains(QStringLiteral("hostname = \"vpn.example.com\"")));
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("subscription")), qPrintable(out->tomlContent));
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("https://alice")), qPrintable(out->tomlContent));
}

void TestConfigImport::refusesASubscriptionOnlyLink() {
    QString err;
    LinkRefusal why = LinkRefusal::Damaged;
    QVERIFY(!prepareDeepLinkImport(v2::kSubscriptionOnly, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::SubscriptionOnly);
    QCOMPARE(err, subscriptionOnlyRefusal());
    QVERIFY2(err.contains(QStringLiteral("Subscription links aren't supported yet")), qPrintable(err));
    QVERIFY2(!err.contains(QStringLiteral("s3cret")) && !err.contains(QStringLiteral("example.com")),
             qPrintable(err));

    // A subscription link whose server is only half there falls back on nothing
    // either: here, the credentials are empty.
    DeepLinkConfig half;
    half.hostname = QStringLiteral("vpn.example.com");
    half.addresses = {QStringLiteral("1.2.3.4:443")};
    QByteArray p = QByteArray::fromBase64(encodeDeepLink(half).mid(6).toLatin1(),
                                          QByteArray::Base64UrlEncoding);
    p += char(0x0E);
    p += char(v2::kSubscription.size());
    p += v2::kSubscription.toUtf8();
    const QString link = QStringLiteral("tt://?")
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QVERIFY(!prepareDeepLinkImport(link, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::SubscriptionOnly);
}

// The core FreeTunnel runs cannot compute the client random an auth-key server
// checks, and is turned away at the handshake with no way to say why.
void TestConfigImport::refusesAServerThatNeedsAnAuthKey() {
    QString err;
    LinkRefusal why = LinkRefusal::Damaged;
    QVERIFY(!prepareDeepLinkImport(v2::kAuthKey, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::NeedsNewerCore);
    QCOMPARE(err, newerCoreRefusal());
    QVERIFY2(err.contains(QStringLiteral("newer TrustTunnel core")), qPrintable(err));
    QVERIFY2(!err.contains(QStringLiteral("aabbccdd")), qPrintable(err));

    // A key with the first and last digit of each range, which the parser takes.
    why = LinkRefusal::Damaged;
    const QString edges = v2::linkFromRecords({{0x00, QByteArray(1, 2)}, {0x01, "vpn.example.com"},
                                               {0x02, "1.2.3.4:443"}, {0x05, "alice"}, {0x06, "secret"},
                                               {0x0F, "09afAF"}});
    QVERIFY(!prepareDeepLinkImport(edges, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::NeedsNewerCore);
    QCOMPARE(err, newerCoreRefusal());
}

// What the parser refuses keeps its own message, which the caller puts after
// "Link error:": a version too new, an address that is not https://.
void TestConfigImport::aDamagedLinkIsSaidToBeDamaged() {
    QString err;
    LinkRefusal why = LinkRefusal::SubscriptionOnly;
    QVERIFY(!prepareDeepLinkImport(v2::kVersion3, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::Damaged);
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));

    why = LinkRefusal::SubscriptionOnly;
    QVERIFY(!prepareDeepLinkImport(v2::kHttpSubscription, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::Damaged);
    QVERIFY2(err.contains(QStringLiteral("https://")), qPrintable(err));
}

// A subscription link imports its server only when the server is all there:
// hostname, an address, username and password, none of them empty, as
// TrustTunnel's decoder reads "required". Each one missing, then each one empty,
// leaves a link that names no server to connect to.
void TestConfigImport::aSubscriptionLinkNeedsEveryServerField_data() {
    QTest::addColumn<int>("tag");
    QTest::addColumn<bool>("present");
    const QList<QPair<const char *, int>> fields{
            {"hostname", 0x01}, {"address", 0x02}, {"username", 0x05}, {"password", 0x06}};
    for (const auto &[field, tag] : fields) {
        QTest::addRow("%s missing", field) << tag << false;
        QTest::addRow("%s empty", field) << tag << true;
    }
}

void TestConfigImport::aSubscriptionLinkNeedsEveryServerField() {
    QFETCH(int, tag);
    QFETCH(bool, present);
    const QList<QPair<char, QByteArray>> server{{0x01, "vpn.example.com"}, {0x02, "1.2.3.4:443"},
                                                {0x05, "alice"}, {0x06, "s3cret"}};
    const QPair<char, QByteArray> subscription{0x0E, v2::kSubscription.toUtf8()};
    QList<QPair<char, QByteArray>> records{{0x00, QByteArray(1, 2)}};
    for (const auto &record : server) {
        if (record.first != static_cast<char>(tag))
            records << record;
        else if (present)
            records << qMakePair(record.first, QByteArray());
    }
    records << subscription;
    QString err;
    LinkRefusal why = LinkRefusal::Damaged;
    QVERIFY(!prepareDeepLinkImport(v2::linkFromRecords(records), &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::SubscriptionOnly);
    QCOMPARE(err, subscriptionOnlyRefusal());

    // That field is the whole difference: with it as well, the server is imported.
    QList<QPair<char, QByteArray>> whole{{0x00, QByteArray(1, 2)}};
    whole << server << subscription;
    const auto out = prepareDeepLinkImport(v2::linkFromRecords(whole), &err, &why);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->subscriptionNotKept);
}

// The auth key is the last word on a link: a server that checks one turns this
// core away, and a subscription would hand out the same server. So a link with
// an auth key and no server says the core is too old, not that subscriptions
// are missing, which would send its user to wait for the wrong thing.
void TestConfigImport::anAuthKeyIsRefusedBeforeAMissingServer() {
    QString err;
    LinkRefusal why = LinkRefusal::Damaged;
    const QString link = v2::linkFromRecords(
            {{0x00, QByteArray(1, 2)}, {0x0E, v2::kSubscription.toUtf8()}, {0x0F, "aabbccdd"}});
    QVERIFY(!prepareDeepLinkImport(link, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::NeedsNewerCore);
    QCOMPARE(err, newerCoreRefusal());
}

// The common export of a self-signed server with subscriptions on.
void TestConfigImport::importsASelfSignedSubscriptionLink() {
    QString err;
    const auto out = prepareDeepLinkImport(v2::kSelfSigned, &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->subscriptionNotKept);
    QVERIFY(!out->skipVerification);
    QVERIFY(!out->certificateExtraIgnored);
    QVERIFY2(out->tomlContent.contains(QStringLiteral("-----BEGIN CERTIFICATE-----")),
             qPrintable(out->tomlContent));
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("subscription")), qPrintable(out->tomlContent));
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("https://alice")), qPrintable(out->tomlContent));
}

void TestConfigImport::importsALinkWithTagsItDoesNotKnow() {
    QString err;
    const auto out = prepareDeepLinkImport(v2::kUnknownTags, &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->subscriptionNotKept);
    QVERIFY(out->tomlContent.contains(QStringLiteral("hostname = \"vpn.example.com\"")));
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("https://alice")), qPrintable(out->tomlContent));
}

void TestConfigImport::aTwoVersionLinkIsRefusedForTheNewer() {
    QString err;
    LinkRefusal why = LinkRefusal::SubscriptionOnly;
    QVERIFY(!prepareDeepLinkImport(v2::kTwoVersions, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::Damaged);
    QCOMPARE(err, QStringLiteral("unsupported deep link version 3"));
}

// A certificate field with a key beside the certificate, as a link made by hand
// from a server's combined file can have: the config gets the certificate, and
// the import says something was left out.
void TestConfigImport::aLinkCarryingAKeyImportsTheCertificateAlone() {
    const QByteArray leaf = certs::derOf(certs::kLeafPem);
    QString err;
    const auto out = prepareDeepLinkImport(linkWithCertificate(certs::kPkcs8Der + leaf), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->certificateExtraIgnored);
    QCOMPARE(out->tomlContent.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
    QVERIFY2(!out->tomlContent.contains(certs::keyLabel()), qPrintable(out->tomlContent));
    QVERIFY2(!out->tomlContent.contains(QString::fromLatin1(certs::kPkcs8Der.toBase64())),
             qPrintable(out->tomlContent));
    QCOMPARE(certificatesDer(parseConfigToml(out->tomlContent).certificate), leaf);

    // A certificate and its chain, with nothing else, is said nothing about.
    const auto chain = prepareDeepLinkImport(linkWithCertificate(leaf + certs::derOf(certs::kCaPem)), &err);
    QVERIFY2(chain.has_value(), qPrintable(err));
    QVERIFY(!chain->certificateExtraIgnored);
    QCOMPARE(chain->tomlContent.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 2);
    // Nor is a link without one.
    const auto plain = prepareDeepLinkImport(makeLink(), &err);
    QVERIFY2(plain.has_value(), qPrintable(err));
    QVERIFY(!plain->certificateExtraIgnored);
}

// The link meant to pin its server, and holds no certificate to pin it with:
// a key alone, or bytes that are not DER at all. Imported without the pin, a
// self-signed server would fail later, at the handshake, with nothing to say why.
void TestConfigImport::aLinkWhoseCertificateHoldsNoCertificateIsDamaged() {
    for (const QByteArray &field : {certs::kPkcs8Der, QByteArray::fromHex("3003020101"), QByteArray("junk")}) {
        QString err;
        LinkRefusal why = LinkRefusal::SubscriptionOnly;
        QVERIFY(!prepareDeepLinkImport(linkWithCertificate(field), &err, &why).has_value());
        QCOMPARE(why, LinkRefusal::Damaged);
        QCOMPARE(err, QStringLiteral("malformed certificate"));
    }

    // A subscription with no server is refused for that first, whatever its
    // certificate holds: there is no config to pin.
    QString err;
    LinkRefusal why = LinkRefusal::Damaged;
    const QString subscriptionOnly = v2::linkFromRecords(
            {{0x00, QByteArray(1, 2)}, {0x0E, v2::kSubscription.toUtf8()}, {0x08, certs::kPkcs8Der}});
    QVERIFY(!prepareDeepLinkImport(subscriptionOnly, &err, &why).has_value());
    QCOMPARE(why, LinkRefusal::SubscriptionOnly);
    QCOMPARE(err, subscriptionOnlyRefusal());
}

// ---- the endpoint's export ----------------------------------------------------

// The server endpoint_export::text() names, as a link carries it.
static DeepLinkConfig serverOfTheExport()
{
    DeepLinkConfig c;
    c.hostname = QStringLiteral("vpn.example.com");
    c.addresses = {QStringLiteral("203.0.113.5:443"), QStringLiteral("[2001:db8::5]:443")};
    c.username = QStringLiteral("alice");
    c.password = QStringLiteral("s3cret");
    c.clientRandomPrefix = QStringLiteral("aabbcc/ff00ff");
    c.upstreamProtocol = UpstreamProtocol::Http3;
    c.antiDpi = true;
    c.name = QStringLiteral("Work VPN");
    c.dnsUpstreams = {QStringLiteral("1.1.1.1"), QStringLiteral("tls://dns.example.com")};
    c.certificate = certs::derOf(certs::kLeafPem);
    return c;
}

static std::optional<PreparedImport> importExport(const endpoint_export::Fields &f,
                                                  const QString &fileName = QStringLiteral("exported"),
                                                  QString *err = nullptr, LinkRefusal *why = nullptr)
{
    return prepareEndpointExportImport(endpoint_export::text(f), fileName, err, why);
}

// The export becomes the config the link with its server gives, and the link it
// is answered with is that link.
void TestConfigImport::anEndpointExportImportsAsTheLinkWithItsServerDoes() {
    endpoint_export::Fields f;
    f.subscriptionUrl.clear();
    QString err;
    LinkRefusal why = LinkRefusal::NeedsNewerCore; // anything but what it will say
    auto out = importExport(f, QStringLiteral("exported"), &err, &why);
    QVERIFY2(out.has_value(), qPrintable(err));
    QCOMPARE(why, LinkRefusal::Damaged); // set even on success, so it is never stale
    QVERIFY(out->fromFile);
    QCOMPARE(out->fileName, QStringLiteral("Work VPN.toml"));
    QVERIFY(!out->skipVerification);
    QVERIFY(!out->subscriptionNotKept);
    QVERIFY(!out->certificateExtraIgnored);

    DeepLinkConfig c = serverOfTheExport();
    const QString link = encodeDeepLink(c);
    auto fromLink = prepareDeepLinkImport(link, &err);
    QVERIFY2(fromLink.has_value(), qPrintable(err));
    QCOMPARE(out->tomlContent, fromLink->tomlContent);
    QCOMPARE(out->link, link);
    // A link is answered with itself, as it came.
    QVERIFY(!fromLink->fromFile);
    QCOMPARE(fromLink->link, link);
    QCOMPARE(prepareDeepLinkImport(QStringLiteral(" ") + link)->link, QStringLiteral(" ") + link);

    // And every flag the other way.
    f.hasIpv6 = false;
    f.skipVerification = true;
    f.antiDpi = false;
    f.customSni = QStringLiteral("cdn.example.org");
    f.upstreamProtocol = QStringLiteral("http2");
    c.hasIpv6 = false;
    c.skipVerification = true;
    c.antiDpi = false;
    c.customSni = QStringLiteral("cdn.example.org");
    c.upstreamProtocol = UpstreamProtocol::Http2;
    out = importExport(f, QStringLiteral("exported"), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->skipVerification);
    fromLink = prepareDeepLinkImport(encodeDeepLink(c), &err);
    QVERIFY2(fromLink.has_value(), qPrintable(err));
    QCOMPARE(out->tomlContent, fromLink->tomlContent);
    QCOMPARE(out->link, encodeDeepLink(c));

    // An empty address is no address, and is not carried over.
    f.addresses = QStringLiteral("\"\", \"203.0.113.5:443\", \" \"");
    c.addresses = {QStringLiteral("203.0.113.5:443")};
    out = importExport(f, QStringLiteral("exported"), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QCOMPARE(out->link, encodeDeepLink(c));
}

// The endpoint writes `name = ""` unless the server was given a name, and the
// config is then named after the file, as any config file is.
void TestConfigImport::anEndpointExportIsNamedByItsFileWhenItHasNoName() {
    endpoint_export::Fields f;
    QCOMPARE(importExport(f, QStringLiteral("branch-office"))->fileName, QStringLiteral("Work VPN.toml"));
    for (const QString &none : {QString(), QStringLiteral("   ")}) {
        f.name = none;
        const auto out = importExport(f, QStringLiteral(" branch-office "));
        QVERIFY(out.has_value());
        QCOMPARE(out->fileName, QStringLiteral("branch-office.toml"));
        QCOMPARE(parseDeepLink(out->link)->name, QStringLiteral("branch-office"));
    }
    // With no file name either, the hostname, as a link without a name has.
    QCOMPARE(importExport(f, QString())->fileName, QStringLiteral("vpn.example.com.toml"));
}

// The address holds the VPN password in plain text: it is in neither the config
// nor the link the question is answered with, and the import says so.
void TestConfigImport::anEndpointExportLeavesItsSubscriptionAddressOut() {
    QString err;
    const auto out = importExport(endpoint_export::Fields(), QStringLiteral("exported"), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->subscriptionNotKept);
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("subscription")), qPrintable(out->tomlContent));
    QVERIFY2(!out->tomlContent.contains(QStringLiteral("s3cret@")), qPrintable(out->tomlContent));
    QVERIFY2(out->tomlContent.contains(QStringLiteral("password = \"s3cret\"")), qPrintable(out->tomlContent));
    const auto decoded = parseDeepLink(out->link, &err);
    QVERIFY2(decoded.has_value(), qPrintable(err));
    QCOMPARE(decoded->version, 1);
    QVERIFY(decoded->subscriptionUrl.isEmpty());
    QCOMPARE(out->link, encodeDeepLink(serverOfTheExport()));
}

// The endpoint pastes in the file its certificate came from, and that file can
// hold the server's private key beside it. Only the certificates go on, into the
// config and the link, and the import says something was left out.
void TestConfigImport::anEndpointExportKeepsOnlyItsCertificates() {
    using endpoint_export::multiline;
    const QByteArray leaf = certs::derOf(certs::kLeafPem);
    endpoint_export::Fields f;
    f.certificate = multiline(certs::kBagAttributes + certs::keyPem() + certs::kBagAttributes + certs::kLeafPem);
    QString err;
    auto out = importExport(f, QStringLiteral("exported"), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY(out->certificateExtraIgnored);
    QCOMPARE(out->tomlContent.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
    QVERIFY2(!out->tomlContent.contains(certs::keyLabel()), qPrintable(out->tomlContent));
    QCOMPARE(certificatesDer(parseConfigToml(out->tomlContent).certificate), leaf);
    QCOMPARE(parseDeepLink(out->link)->certificate, leaf);

    // A chain and nothing else, in either spelling, is kept whole and said
    // nothing about.
    for (const QString &value : {multiline(certs::kChainPem), endpoint_export::singleLineCrlf(certs::kChainPem)}) {
        f.certificate = value;
        out = importExport(f, QStringLiteral("exported"), &err);
        QVERIFY2(out.has_value(), qPrintable(err));
        QVERIFY(!out->certificateExtraIgnored);
        QCOMPARE(certificatesDer(parseConfigToml(out->tomlContent).certificate), leaf + certs::derOf(certs::kCaPem));
    }
    // A server whose certificate the system trusts is exported with none, and
    // pinned with none; so is one that is blank.
    for (const QString &none : {QStringLiteral("\"\""), QStringLiteral("\" \\n \"")}) {
        f.certificate = none;
        out = importExport(f, QStringLiteral("exported"), &err);
        QVERIFY2(out.has_value(), qPrintable(err));
        QVERIFY(!out->certificateExtraIgnored);
        QVERIFY2(out->tomlContent.contains(QStringLiteral("certificate = \"\"\n")), qPrintable(out->tomlContent));
        QVERIFY(parseDeepLink(out->link)->certificate.isEmpty());
    }
}

// Each refusal a link meets, with the link's message, in the link's order, and
// one of a config file's own: a server it does not name.
void TestConfigImport::anEndpointExportIsRefusedAsALinkIs() {
    struct Case {
        const char *what;
        endpoint_export::Fields fields;
        LinkRefusal why;
        QString message;
    };
    const QString keyOnly = endpoint_export::multiline(certs::keyPem());
    QList<Case> cases;
    endpoint_export::Fields f;
    f.clientRandomAuthKey = QStringLiteral("aabbccdd");
    cases.append({"an auth key", f, LinkRefusal::NeedsNewerCore, newerCoreRefusal()});
    f.certificate = keyOnly;
    cases.append({"an auth key before a key alone", f, LinkRefusal::NeedsNewerCore, newerCoreRefusal()});
    f = endpoint_export::Fields();
    f.password.clear();
    cases.append({"a subscription and no password", f, LinkRefusal::SubscriptionOnly, subscriptionOnlyRefusal()});
    f.certificate = keyOnly;
    cases.append({"a subscription before a key alone", f, LinkRefusal::SubscriptionOnly,
                  subscriptionOnlyRefusal()});
    f = endpoint_export::Fields();
    f.addresses.clear();
    cases.append({"a subscription and no address", f, LinkRefusal::SubscriptionOnly, subscriptionOnlyRefusal()});
    f.subscriptionUrl.clear();
    cases.append({"no address", f, LinkRefusal::Damaged, QStringLiteral("missing address or credentials")});
    // Empty entries are no addresses either. The link the question is answered
    // with leaves them out, so a server named by them alone would be asked about
    // and then refused once the user said yes.
    f.addresses = QStringLiteral("\"\", \" \"");
    cases.append({"empty addresses only", f, LinkRefusal::Damaged, QStringLiteral("missing address or credentials")});
    f.subscriptionUrl = endpoint_export::Fields().subscriptionUrl;
    cases.append({"a subscription and empty addresses only", f, LinkRefusal::SubscriptionOnly,
                  subscriptionOnlyRefusal()});
    f = endpoint_export::Fields();
    f.subscriptionUrl.clear();
    f.username = QStringLiteral(" ");
    cases.append({"no username", f, LinkRefusal::Damaged, QStringLiteral("missing address or credentials")});
    f = endpoint_export::Fields();
    f.clientRandomPrefix = QStringLiteral("abc");
    cases.append({"an odd client random", f, LinkRefusal::Damaged, QStringLiteral("malformed client_random value")});
    f = endpoint_export::Fields();
    f.certificate = keyOnly;
    cases.append({"a key alone", f, LinkRefusal::Damaged, QStringLiteral("malformed certificate")});
    f.certificate = endpoint_export::multiline(QStringLiteral("not a certificate\n"));
    cases.append({"text alone", f, LinkRefusal::Damaged, QStringLiteral("malformed certificate")});
    for (const Case &c : cases) {
        QString err;
        LinkRefusal why = c.why == LinkRefusal::Damaged ? LinkRefusal::NeedsNewerCore : LinkRefusal::Damaged;
        QVERIFY2(!importExport(c.fields, QStringLiteral("exported"), &err, &why).has_value(), c.what);
        QVERIFY2(why == c.why, c.what);
        QVERIFY2(err == c.message, qPrintable(QString::fromLatin1(c.what) + QStringLiteral(": ") + err));
        QVERIFY2(!err.contains(QStringLiteral("s3cret")) && !err.contains(QStringLiteral("aabbccdd")), c.what);
    }

    // What is not an export at all names no server it can read.
    QString err;
    LinkRefusal why = LinkRefusal::NeedsNewerCore;
    QVERIFY(!prepareEndpointExportImport(QStringLiteral("[endpoint]\n") + endpoint_export::text(),
                                         QStringLiteral("exported"), &err, &why)
                     .has_value());
    QCOMPARE(why, LinkRefusal::Damaged);
    QCOMPARE(err, QStringLiteral("missing address or credentials"));

    // A client random the core reads as it is repaired, as a link's is; and a
    // file without a password is imported, as any config file is.
    f = endpoint_export::Fields();
    f.clientRandomPrefix = QStringLiteral("aabb/");
    auto out = importExport(f, QStringLiteral("exported"), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
    QVERIFY2(out->tomlContent.contains(QStringLiteral("client_random = \"aabb\"\n")), qPrintable(out->tomlContent));
    f = endpoint_export::Fields();
    f.subscriptionUrl.clear();
    f.password.clear();
    out = importExport(f, QStringLiteral("exported"), &err);
    QVERIFY2(out.has_value(), qPrintable(err));
}

// The link a file's question is answered with makes the same config again, under
// the same name, in every case the file has.
void TestConfigImport::anEndpointExportsLinkGivesTheSameConfig() {
    QList<endpoint_export::Fields> variants{endpoint_export::Fields()};
    endpoint_export::Fields f;
    f.name.clear();
    variants << f;
    f = endpoint_export::Fields();
    f.name = QString(60, QChar(0x0416)); // cut to the limit, and found under its whole name
    variants << f;
    f = endpoint_export::Fields();
    f.certificate = endpoint_export::multiline(certs::keyPem() + certs::kChainPem);
    f.skipVerification = true;
    f.hasIpv6 = false;
    f.customSni = QStringLiteral("cdn.example.org");
    f.upstreamProtocol = QStringLiteral("http2");
    f.clientRandomPrefix = QStringLiteral("aabb/");
    f.dnsUpstreams.clear();
    f.subscriptionUrl.clear();
    variants << f;
    // What a config file may leave out, as this one does, and an empty address
    // beside a real one.
    f = endpoint_export::Fields();
    f.subscriptionUrl.clear();
    f.password.clear();
    f.hostname.clear();
    f.addresses = QStringLiteral("\"\", \"203.0.113.5:443\"");
    variants << f;
    for (const endpoint_export::Fields &variant : variants) {
        QString err;
        const auto out = importExport(variant, QStringLiteral("round-trip"), &err);
        QVERIFY2(out.has_value(), qPrintable(err));
        const auto again = prepareDeepLinkImport(out->link, &err);
        QVERIFY2(again.has_value(), qPrintable(err));
        QCOMPARE(again->fileName, out->fileName);
        QCOMPARE(again->unclippedFileName, out->unclippedFileName);
        QCOMPARE(again->legacyFileName, out->legacyFileName);
        QCOMPARE(again->tomlContent, out->tomlContent);
        QCOMPARE(again->skipVerification, out->skipVerification);
        // The link carries neither what was left out nor the address.
        QVERIFY(!again->certificateExtraIgnored);
        QVERIFY(!again->subscriptionNotKept);
    }
}

// Whatever an export holds, one the user is asked about is one the answer can
// add: the link the question carries makes the same config, and one that would
// be refused is refused before anyone is asked. Every combination of the fields
// that decide whether it is refused, each empty, blank or wrong.
void TestConfigImport::everyExportAskedAboutCanBeAnswered() {
    using endpoint_export::multiline;
    const QStringList hostnames{QStringLiteral("vpn.example.com"), QString()};
    const QStringList addresses{endpoint_export::Fields().addresses, QString(), QStringLiteral("\"\""),
                                QStringLiteral("\"\", \" \""), QStringLiteral("\" 203.0.113.5:443 \"")};
    const QStringList usernames{QStringLiteral("alice"), QString(), QStringLiteral(" ")};
    const QStringList passwords{QStringLiteral("s3cret"), QString()};
    const QStringList subscriptions{endpoint_export::Fields().subscriptionUrl, QString()};
    const QStringList clientRandoms{QStringLiteral("aabbcc/ff00ff"), QStringLiteral("aabb/"), QStringLiteral("abc")};
    const QStringList certificates{multiline(certs::kLeafPem), QStringLiteral("\"\""), multiline(certs::keyPem()),
                                   multiline(certs::keyPem() + certs::kLeafPem)};
    const QList<const QStringList *> choices{&hostnames, &addresses, &usernames, &passwords,
                                             &subscriptions, &clientRandoms, &certificates};
    qsizetype combinations = 1;
    for (const QStringList *choice : choices)
        combinations *= choice->size();
    int asked = 0;
    int refused = 0;
    for (qsizetype i = 0; i < combinations; ++i) {
        // The i-th combination, one digit per field.
        QStringList pick;
        qsizetype rest = i;
        for (const QStringList *choice : choices) {
            pick << choice->at(rest % choice->size());
            rest /= choice->size();
        }
        endpoint_export::Fields f;
        f.hostname = pick.at(0);
        f.addresses = pick.at(1);
        f.username = pick.at(2);
        f.password = pick.at(3);
        f.subscriptionUrl = pick.at(4);
        f.clientRandomPrefix = pick.at(5);
        f.certificate = pick.at(6);
        const QString text = endpoint_export::text(f);
        QString err;
        const auto out = prepareEndpointExportImport(text, QStringLiteral("file"), &err);
        if (!out) {
            QVERIFY2(!err.isEmpty(), qPrintable(text));
            ++refused;
            continue;
        }
        ++asked;
        const auto again = prepareDeepLinkImport(out->link, &err);
        QVERIFY2(again.has_value(), qPrintable(err + QStringLiteral("\n") + text));
        QVERIFY2(again->tomlContent == out->tomlContent, qPrintable(text));
        QVERIFY2(again->fileName == out->fileName, qPrintable(text));
        QVERIFY2(!out->tomlContent.contains(certs::keyLabel()), qPrintable(text));
        QVERIFY2(!out->tomlContent.contains(QStringLiteral("s3cret@")), qPrintable(text));
    }
    // Both kinds are among them, so neither half of the check is empty.
    QVERIFY(asked > 0);
    QVERIFY(refused > 0);
}

QTEST_MAIN(TestConfigImport)
#include "test_configimport.moc"
