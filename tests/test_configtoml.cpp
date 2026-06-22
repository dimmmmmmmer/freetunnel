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
    void tunListenerByDefault();
    void socksRoundTrip();
    void socksDefaultsAndNoEndpointBleed();
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

void TestConfigToml::tunListenerByDefault() {
    ConfigToml in;
    in.hostname = "h"; in.addresses = "1.2.3.4:443"; in.username = "u"; in.password = "p";
    const QString toml = buildConfigToml(in);
    QVERIFY(toml.contains("[listener.tun]"));
    QVERIFY(!toml.contains("[listener.socks]"));
    QCOMPARE(parseConfigToml(toml).socks5, false);
}

void TestConfigToml::socksRoundTrip() {
    ConfigToml in;
    in.hostname = "vpn.example.com";
    in.addresses = "1.2.3.4:443";
    in.username = "premium";   // endpoint creds
    in.password = "s3cret";
    in.socks5 = true;
    in.socksListen = "127.0.0.1:9050";
    in.socksUser = "proxyuser"; // distinct from endpoint creds
    in.socksPass = "proxypass";

    const QString toml = buildConfigToml(in);
    QVERIFY(toml.contains("[listener.socks]"));
    QVERIFY(!toml.contains("[listener.tun]"));

    const ConfigToml out = parseConfigToml(toml);
    QCOMPARE(out.socks5, true);
    QCOMPARE(out.socksListen, QStringLiteral("127.0.0.1:9050"));
    QCOMPARE(out.socksUser, QStringLiteral("proxyuser"));
    QCOMPARE(out.socksPass, QStringLiteral("proxypass"));
    // Endpoint creds must still parse to the endpoint, not the socks section.
    QCOMPARE(out.username, QStringLiteral("premium"));
    QCOMPARE(out.password, QStringLiteral("s3cret"));
}

void TestConfigToml::socksDefaultsAndNoEndpointBleed() {
    // SOCKS with no explicit listen address / auth: address defaults, auth empty.
    ConfigToml in;
    in.hostname = "h"; in.addresses = "1.2.3.4:443";
    in.username = "u"; in.password = "p";
    in.socks5 = true;
    in.socksListen = "";
    const QString toml = buildConfigToml(in);
    QVERIFY(toml.contains("address = \"127.0.0.1:1080\""));
    QVERIFY(!toml.contains("[listener.socks]\nusername")); // no empty auth keys
    const ConfigToml out = parseConfigToml(toml);
    QCOMPARE(out.socksListen, QStringLiteral("127.0.0.1:1080"));
    QVERIFY(out.socksUser.isEmpty());
    QVERIFY(out.socksPass.isEmpty());
}

QTEST_MAIN(TestConfigToml)
#include "test_configtoml.moc"
