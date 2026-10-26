// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/DeepLink.h"
#include "deeplink_v2_vectors.h"

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
};

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

QTEST_MAIN(TestConfigImport)
#include "test_configimport.moc"
