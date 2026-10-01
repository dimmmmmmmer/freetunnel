// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/DeepLink.h"

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

QTEST_MAIN(TestConfigImport)
#include "test_configimport.moc"
