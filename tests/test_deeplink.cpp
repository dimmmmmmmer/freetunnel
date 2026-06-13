#include <QtTest>

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
};

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

QTEST_MAIN(TestDeepLink)
#include "test_deeplink.moc"
