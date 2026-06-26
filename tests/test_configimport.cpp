// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/ConfigImport.h"
#include "core/DeepLink.h"

using namespace freetunnel;

class TestConfigImport : public QObject {
    Q_OBJECT

private slots:
    void importsValidLink();
    void skipVerificationFlagPropagates();
    void fileNameFromServerName();
    void fileNameSanitized();
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
    // spaces sanitized to underscores, .toml appended
    QCOMPARE(out->fileName, QStringLiteral("My_Server.toml"));
}

void TestConfigImport::fileNameSanitized() {
    // no display name -> falls back to hostname
    auto out = prepareDeepLinkImport(makeLink());
    QVERIFY(out.has_value());
    QCOMPARE(out->fileName, QStringLiteral("vpn.example.com.toml"));
}

void TestConfigImport::rejectsInvalidLink() {
    QString err;
    QVERIFY(!prepareDeepLinkImport("https://nope", &err).has_value());
    QVERIFY(!err.isEmpty());
}

QTEST_MAIN(TestConfigImport)
#include "test_configimport.moc"
