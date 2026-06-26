// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/ConfigToml.h"

using namespace freetunnel;

// Round-trip + key-field coverage for config TOML generation/parsing (the
// create/edit path).
class TestConfigToml : public QObject {
    Q_OBJECT

private slots:
    void roundTrip();
    void escapesQuotes();
    void emptyCertificate();
    void defaultsProtocol();
};

void TestConfigToml::roundTrip() {
    ConfigToml in;
    in.hostname = "vpn.example.com";
    in.addresses = "1.2.3.4:443, [2001:db8::1]:443";
    in.username = "premium";
    in.password = "s3cret";
    in.protocol = "http3";
    in.dns = "1.1.1.1, tls://8.8.8.8";
    in.customSni = "example.org";
    in.clientRandom = "deadbeef";
    in.allowIpv6 = false;
    in.certificate = "-----BEGIN-----\nabc\n-----END-----";

    const ConfigToml out = parseConfigToml(buildConfigToml(in));
    QCOMPARE(out.hostname, in.hostname);
    QCOMPARE(out.addresses, in.addresses);
    QCOMPARE(out.username, in.username);
    QCOMPARE(out.password, in.password);
    QCOMPARE(out.protocol, QStringLiteral("http3"));
    QCOMPARE(out.dns, in.dns);
    QCOMPARE(out.customSni, in.customSni);
    QCOMPARE(out.clientRandom, in.clientRandom);
    QCOMPARE(out.allowIpv6, false);
    QCOMPARE(out.certificate, in.certificate);
}

void TestConfigToml::escapesQuotes() {
    ConfigToml in;
    in.hostname = "h.example";
    in.addresses = "1.2.3.4:443";
    in.username = "u";
    in.password = "p\"a\\ss"; // quote + backslash
    const QString toml = buildConfigToml(in);
    QVERIFY(toml.contains("password = \"p\\\"a\\\\ss\""));
    QCOMPARE(parseConfigToml(toml).password, in.password);
}

void TestConfigToml::emptyCertificate() {
    ConfigToml in;
    in.hostname = "h"; in.addresses = "1.2.3.4:443"; in.username = "u"; in.password = "p";
    const QString toml = buildConfigToml(in);
    QVERIFY(toml.contains("certificate = \"\""));
    QVERIFY(parseConfigToml(toml).certificate.isEmpty());
}

void TestConfigToml::defaultsProtocol() {
    ConfigToml in;
    in.hostname = "h"; in.addresses = "1.2.3.4:443"; in.username = "u"; in.password = "p";
    in.protocol = "http2";
    QVERIFY(buildConfigToml(in).contains("upstream_protocol = \"http2\""));
    // Unknown protocol falls back to http2.
    in.protocol = "weird";
    QVERIFY(buildConfigToml(in).contains("upstream_protocol = \"http2\""));
}

QTEST_MAIN(TestConfigToml)
#include "test_configtoml.moc"
