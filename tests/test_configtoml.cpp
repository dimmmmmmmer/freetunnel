// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>
#include <QRandomGenerator>

#include "certificate_vectors.h"
#include "core/ConfigToml.h"

using namespace freetunnel;

// Round-trip + key-field coverage for config TOML generation/parsing (the
// create/edit path).
class TestConfigToml : public QObject {
    Q_OBJECT

private slots:
    void aCommentAfterATableHeaderKeepsTheTable();
    void endpointKeysLeftAtTheRootAreClearedOut();
    void anotherTablesPasswordIsNotTheServers();
    void indentedKeysAreRead();
    void aQuotedKeyIsTheSameKey();
    void aQuotedTableNameIsTheSameTable();
    void aValueInTripleQuotesIsRead();
    void aCertificatesTextIsNotReadAsKeys();
    void theDnsListIsReadWhereTheCoreReadsIt();
    void postQuantumStaysOffWhenTheConfigTurnsItOff();
    void aClientRandomGoesToTheCoreInAFormItTakes();
    void anOldSplitClientRandomIsJoinedBack();
    void dnsServersAreOneEntryEachHoweverSeparated();
    void roundTripKeepsWhatTheEditorDoesNotUnderstand();
    void aCertificateCannotForgeSections();
    void roundTripOfOurOwnOutputIsStable();
    void roundTrip();
    void escapesQuotes();
    void emptyCertificate();
    void defaultsProtocol();
    void boolFlagsAreLineAnchored();
    void securityFlagsDefaultClosed();
    void fieldValuesCannotInjectTomlKeys();
    void multiLineValuesTheEditorDoesNotOwnSurviveARoundTrip();
    void literalStringsAndMultiLineArraysAreRead();
    void aCertificateIsReadInEverySpelling();

    // TrustTunnel 1.3 configs.
    void theSubscriptionTableIsLeftOut();
    void aSubscriptionUrlKeyIsLeftOutWhereverTheExportPutsIt();
    void aSubscriptionSpelledAsAKeyIsLeftOut();
    void aConfigWithoutASubscriptionComesBackAsItWas();
    void aRewriteNeverPutsASubscriptionBack();
    void theAuthKeyIsReadFromTheEndpoint();

    // A certificate keeps only its certificates.
    void derElementEndReadsOneElementOfTheTagAsked();
    void isCertificateDer_data();
    void isCertificateDer();
    void certificatesOnly_data();
    void certificatesOnly();
    void certificatesDerMatchesTheKeptBlocks();
    void withCertificatesOnlyChangesOnlyTheCertificate();
    void withCertificatesOnlyReadsEverySpelling();
    void withCertificatesOnlyLeavesACleanConfigAsItWas();
    void randomCertificatesKeepTheRules();
};

// A config as TrustTunnel's endpoint exports it from 1.3.0 on, pasted under
// [endpoint] the way the app's own files are laid out, with a subscription in each
// of the places one turns up.
static QString configWithSubscriptions()
{
    return QStringLiteral(
            "# Exported by the endpoint\n"
            "loglevel = \"info\"\n"
            "subscription_url = \"https://alice:s3cret@vpn.example.com/subscription\"\n"
            "provider_quirk = 42\n"
            "\n[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"alice\"\n"
            "password = \"s3cret\"\n"
            "client_random_auth_key = \"\"\n"
            "subscription_url = \"https://alice:s3cret@vpn.example.com/subscription\"\n"
            "provider_tag = \"gold\"\n"
            "\n[endpoint.subscription]\n"
            "# kept up to date by the client\n"
            "url = \"https://alice:s3cret@vpn.example.com/subscription\"\n"
            "\n[listener.tun]\n"
            "mtu_size = 1280\n"
            "\n[provider]\n"
            "port = 1080\n");
}

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
    in.skipVerification = true;
    in.antiDpi = true;
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
    QCOMPARE(out.skipVerification, true);
    QCOMPARE(out.antiDpi, true);
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

void TestConfigToml::boolFlagsAreLineAnchored() {
    // A `skip_verification = true` substring sitting inside another value (e.g. a
    // pasted certificate body) must NOT flip the real flag — it's only honored
    // as the key of a line of its own.
    const QString toml = QStringLiteral(
            "[endpoint]\n"
            "skip_verification = false\n"
            "has_ipv6 = true\n"
            "anti_dpi = false\n"
            "certificate = \"\"\"\n"
            "note: skip_verification = true anti_dpi = true has_ipv6 = false\n"
            "\"\"\"\n");
    const ConfigToml c = parseConfigToml(toml);
    QCOMPARE(c.skipVerification, false);
    QCOMPARE(c.antiDpi, false);
    QCOMPARE(c.allowIpv6, true);

    // And a genuine line-anchored flag is still read.
    ConfigToml on = parseConfigToml(QStringLiteral("[endpoint]\nskip_verification = true\n"));
    QCOMPARE(on.skipVerification, true);

    // From [endpoint] only, which is where the core reads it. Anywhere else the
    // core does not see it, and reading it there would hand the next rewrite a
    // switched-off certificate check to write into [endpoint].
    const ConfigToml elsewhere = parseConfigToml(QStringLiteral(
            "skip_verification = true\n"
            "[endpoint]\n"
            "hostname = \"h\"\n"
            "[listener.socks]\n"
            "anti_dpi = true\n"));
    QVERIFY(!elsewhere.skipVerification);
    QVERIFY(!elsewhere.antiDpi);
    QVERIFY2(!buildConfigToml(elsewhere).contains(QStringLiteral("skip_verification = true")),
             qPrintable(buildConfigToml(elsewhere)));
}

// The test above always spells the flags out. What it never covers is a config
// that simply omits them, which is the common case: a minimal TOML, or one
// written by an older client. The defaults then decide, and one of them is
// security-significant — skip_verification turns off server certificate
// checking, so it has to default closed rather than open.
//
// Found by mutation: flipping that default to `true` broke nothing in the suite.
void TestConfigToml::securityFlagsDefaultClosed() {
    const ConfigToml c = parseConfigToml(QStringLiteral(
            "[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "username = \"u\"\n"));
    QVERIFY2(!c.skipVerification,
             "a config that does not mention skip_verification must still verify "
             "the server certificate");
    QVERIFY2(!c.antiDpi, "anti_dpi must default off");
    // Not a security flag, but the same class of silent default: IPv6 is allowed
    // unless a config says otherwise, and flipping it would black-hole v6 traffic.
    QVERIFY2(c.allowIpv6, "has_ipv6 must default on");
}

// A config is rewritten far more often than it looks: migrateConfigPassword()
// parses and rebuilds every config on import, so anything this editor has no
// field for used to be dropped the moment a config entered the app — silently,
// with the file still parsing and still connecting, just routing differently.
void TestConfigToml::roundTripKeepsWhatTheEditorDoesNotUnderstand()
{
    const QString original = QStringLiteral(
            "loglevel = \"info\"\n"
            "vpn_mode = \"general\"\n"
            "provider_quirk = 42\n"           // unknown root key
            "dns_upstreams = [\"1.1.1.1\"]\n"
            "\n[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n"
            "provider_tag = \"gold\"\n"      // unknown endpoint key
            "\n[listener.tun]\n"
            "bound_if = \"en0\"\n"
            "mtu_size = 1280\n"
            "excluded_routes = [\"10.9.0.0/16\"]\n"
            "\n[listener.socks]\n"            // a listener FreeTunnel does not run
            "address = \"127.0.0.1:1080\"\n"
            "\n[provider]\n"                  // a table this editor never writes
            "port = 1080\n");

    const freetunnel::ConfigToml parsed = freetunnel::parseConfigToml(original);
    QCOMPARE(parsed.hostname, QStringLiteral("vpn.example.com"));

    const QString rebuilt = freetunnel::buildConfigToml(parsed);

    // The user's routing, not our defaults.
    QVERIFY2(rebuilt.contains(QStringLiteral("mtu_size = 1280")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("bound_if = \"en0\"")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("excluded_routes = [\"10.9.0.0/16\"]")),
             qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains(QStringLiteral("192.168.0.0/16")),
             "our default routes were written over the config's own");

    // Sections and keys we have no field for.
    QVERIFY2(rebuilt.contains(QStringLiteral("[provider]")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("port = 1080")), qPrintable(rebuilt));
    // But not a second listener: the core refuses a config that names two.
    QVERIFY2(!rebuilt.contains(QStringLiteral("[listener.socks]")), qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains(QStringLiteral("127.0.0.1:1080")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("provider_quirk = 42")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("provider_tag = \"gold\"")), qPrintable(rebuilt));

    // Stable: a second trip changes nothing more.
    QCOMPARE(freetunnel::buildConfigToml(freetunnel::parseConfigToml(rebuilt)), rebuilt);
}

// A certificate is the one value that can contain anything, and it is attacker
// influenced on the import path. The splitter must step over it rather than read
// its contents as structure.
void TestConfigToml::aCertificateCannotForgeSections()
{
    freetunnel::ConfigToml c;
    c.hostname = QStringLiteral("h");
    c.certificate = QStringLiteral("-----BEGIN CERTIFICATE-----\n"
                                   "[listener.tun]\n"
                                   "excluded_routes = [\"203.0.113.7/32\"]\n"
                                   "-----END CERTIFICATE-----");
    const QString built = freetunnel::buildConfigToml(c);
    const freetunnel::ConfigToml back = freetunnel::parseConfigToml(built);

    // The forged table stayed inside the certificate: it was not lifted out into
    // tunSection, and it did not become an "extra" section that we would then
    // re-emit as real structure on the next save.
    QVERIFY2(back.tunSection.contains(QStringLiteral("mtu_size")), qPrintable(back.tunSection));
    QVERIFY2(!back.tunSection.contains(QStringLiteral("203.0.113.7")), qPrintable(back.tunSection));
    QVERIFY2(!back.extraSections.contains(QStringLiteral("203.0.113.7")),
             qPrintable(back.extraSections));
    QCOMPARE(back.certificate, c.certificate);
}

// Nothing to preserve is the common case: a config this editor wrote itself must
// come back byte for byte, or every save would grow the file.
void TestConfigToml::roundTripOfOurOwnOutputIsStable()
{
    freetunnel::ConfigToml c;
    c.hostname = QStringLiteral("vpn.example.com");
    c.addresses = QStringLiteral("1.2.3.4:443");
    c.username = QStringLiteral("u");
    const QString once = freetunnel::buildConfigToml(c);
    QCOMPARE(freetunnel::buildConfigToml(freetunnel::parseConfigToml(once)), once);
}

// There are two TOML writers in this codebase with two separate escapers.
// test_deeplink's tomlInjectionStripped covers the import path
// (deepLinkConfigToToml). This is the other one — buildConfigToml, which rewrites
// the user's own config on every edit and on migrateConfigPassword — and nothing
// exercised its escaping at all. Disabling the control-character strip in
// tomlEsc() left the whole suite green.
//
// It matters because of who reads the result: the config produced here is handed
// to the elevated helper as an inline document. A newline surviving a field value
// does not corrupt a file, it adds a key — and skip_verification is one key away.
void TestConfigToml::fieldValuesCannotInjectTomlKeys() {
    ConfigToml c;
    c.hostname = QStringLiteral("h.example");
    c.addresses = QStringLiteral("1.2.3.4:443");
    c.username = QStringLiteral("u\nskip_verification = true\nx = \"");
    c.password = QStringLiteral("p\"\nanti_dpi = true\ny = \"");

    const QString toml = buildConfigToml(c, QStringLiteral("info"));

    QVERIFY2(!toml.contains(QStringLiteral("\nskip_verification = true")),
             "a newline in a field value must not open a new TOML key");
    QVERIFY2(!toml.contains(QStringLiteral("\nanti_dpi = true")),
             "a quote in a field value must not close its string and start a key");

    // The real proof is what the parser makes of it: both security flags must come
    // back at their safe defaults, not at what the injected text asked for.
    const ConfigToml back = parseConfigToml(toml);
    QVERIFY2(!back.skipVerification, "injected skip_verification survived the round trip");
    QVERIFY2(!back.antiDpi, "injected anti_dpi survived the round trip");
    QCOMPARE(back.hostname, c.hostname);
}

// A value the editor does not own can span lines: an array, an inline table, a
// ''' block. Only """ was stepped over, so the continuation lines were taken for
// the end of the value and dropped along with the bracket that closed it. What
// came back was `exclusions = [` and nothing after it - a file that no longer
// parses, written over the original by migrateConfigPassword() and handed to the
// root helper on the next connect.
void TestConfigToml::multiLineValuesTheEditorDoesNotOwnSurviveARoundTrip() {
    const QString src = QStringLiteral(
            "loglevel = \"info\"\n"
            "exclusions = [\n"
            "  \"*.example.com\",\n"
            "  \"*.corp\"\n"
            "]\n"
            "\n"
            "[endpoint]\n"
            "hostname = \"vpn.example.org\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n"
            "provider_meta = {\n"
            "  tier = \"gold\"\n"
            "}\n"
            "provider_note = '''\n"
            "line one\n"
            "line two\n"
            "'''\n"
            "\n"
            "[listener.tun]\n"
            "mtu = 1400\n");

    const QString out = buildConfigToml(parseConfigToml(src));
    QVERIFY2(out.contains(QStringLiteral("\"*.example.com\"")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("\"*.corp\"")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("tier = \"gold\"")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("line two")), qPrintable(out));
    // Every opener is closed again. Counting is what catches the actual failure:
    // the old output kept the `[` and lost the `]`.
    QCOMPARE(out.count(QLatin1Char('[')) , out.count(QLatin1Char(']')));
    QCOMPARE(out.count(QStringLiteral("'''")) % 2, 0);

    // And it is stable: a second pass over our own output changes nothing, which
    // is what says the recovered text is understood and not merely copied once.
    QCOMPARE(buildConfigToml(parseConfigToml(out)), out);
}

// TOML has more than one way to write a string, and a provider's file is under
// no obligation to pick ours. These are keys the rebuild writes itself, so a
// spelling the reader did not know was not skipped - it was replaced with an
// empty value, and the config was emptied in place.
void TestConfigToml::literalStringsAndMultiLineArraysAreRead() {
    const QString src = QStringLiteral(
            "loglevel = 'info'\n"
            "\n"
            "[endpoint]\n"
            "hostname = 'vpn.example.org'\n"
            "addresses = [\n"
            "  '1.2.3.4:443',\n"
            "  \"[2001:db8::1]:443\"\n"
            "]\n"
            "username = 'bob'\n"
            "password = 'secret'\n"
            "\n"
            "[listener.tun]\n");

    const ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.org"));
    QCOMPARE(c.username, QStringLiteral("bob"));
    QCOMPARE(c.password, QStringLiteral("secret"));
    QCOMPARE(c.addresses, QStringLiteral("1.2.3.4:443, [2001:db8::1]:443"));
}

// The certificate is the one field where losing the read costs something that
// cannot be typed again from memory: it is the trust anchor the connection is
// pinned to.
void TestConfigToml::aCertificateIsReadInEverySpelling() {
    const QString pem = QStringLiteral("-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----");

    const QString singleLine = QStringLiteral(
            "[endpoint]\nhostname = \"h\"\naddresses = [\"1.2.3.4:443\"]\nusername = \"u\"\n"
            "certificate = \"-----BEGIN CERTIFICATE-----\\nMIIB\\n-----END CERTIFICATE-----\"\n");
    QCOMPARE(parseConfigToml(singleLine).certificate, pem);

    const QString literalBlock = QStringLiteral(
            "[endpoint]\nhostname = \"h\"\naddresses = [\"1.2.3.4:443\"]\nusername = \"u\"\n"
            "certificate = '''\n-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n'''\n");
    QCOMPARE(parseConfigToml(literalBlock).certificate, pem);

    // And it comes back out, rather than being written over with an empty value.
    QVERIFY(buildConfigToml(parseConfigToml(singleLine)).contains(QStringLiteral("MIIB")));
    QVERIFY(buildConfigToml(parseConfigToml(literalBlock)).contains(QStringLiteral("MIIB")));
}

// The editor takes DNS servers separated by spaces or semicolons as well as by
// commas, and its check let them through, but the config was written split on
// commas alone: "1.1.1.1 8.8.8.8" went to the core as one server. Core 1.1.5
// refused to connect with it; 1.1.7 takes it, and the tunnel came up with no DNS.
void TestConfigToml::dnsServersAreOneEntryEachHoweverSeparated()
{
    ConfigToml in;
    in.hostname = "vpn.example.com";
    in.addresses = "1.2.3.4:443";
    in.dns = "1.1.1.1 8.8.8.8; tls://9.9.9.9,\tquic://dns.example.com";
    const QString toml = buildConfigToml(in);
    QVERIFY2(toml.contains(QStringLiteral(
                     "dns_upstreams = [\"1.1.1.1\", \"8.8.8.8\", \"tls://9.9.9.9\", \"quic://dns.example.com\"]")),
             qPrintable(toml));
    QCOMPARE(parseConfigToml(toml).dns, QStringLiteral("1.1.1.1, 8.8.8.8, tls://9.9.9.9, quic://dns.example.com"));
    QCOMPARE(splitDnsList(QStringLiteral(" , ; ")), QStringList());
}

// 1.2.2 and earlier wrote a link's mask under client_random_mask, a key the core
// never read, so such a config connected without it. Loading it joins the mask back
// on, which is also what the connect path does, and saving drops the key.
void TestConfigToml::anOldSplitClientRandomIsJoinedBack()
{
    const auto config = [](const QString &prefix, const QString &mask) {
        return QStringLiteral("loglevel = \"info\"\n"
                              "[endpoint]\n"
                              "hostname = \"vpn.example.com\"\n"
                              "addresses = [\"1.2.3.4:443\"]\n"
                              "username = \"u\"\n"
                              "client_random = \"%1\"\n"
                              "client_random_mask = \"%2\"\n"
                              "custom_sni = \"\"\n"
                              "\n[listener.tun]\n"
                              "mtu_size = 1500\n").arg(prefix, mask);
    };
    const ConfigToml joined = parseConfigToml(config(QStringLiteral("deadbeef"), QStringLiteral("ffff0000")));
    QCOMPARE(joined.clientRandom, QStringLiteral("deadbeef/ffff0000"));
    const QString rebuilt = buildConfigToml(joined);
    QVERIFY2(rebuilt.contains(QStringLiteral("client_random = \"deadbeef/ffff0000\"\n")), qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains(QStringLiteral("client_random_mask")), qPrintable(rebuilt));

    // Already whole: kept as it is, and the stray key still goes.
    const ConfigToml whole = parseConfigToml(config(QStringLiteral("aa/bb"), QStringLiteral("cc")));
    QCOMPARE(whole.clientRandom, QStringLiteral("aa/bb"));
    QVERIFY(!buildConfigToml(whole).contains(QStringLiteral("client_random_mask")));

    // A mask with no prefix masked nothing, then or now.
    QCOMPARE(parseConfigToml(config(QString(), QStringLiteral("ff"))).clientRandom, QString());
}

// A table header may end in a comment, and one that did was not taken for a
// header: its keys stayed at the root, where `password` is not a key this editor
// writes, so the rewrite that moves the password into the credential store wrote
// it straight back in plain text - on every connect, for good. A commented
// [listener.tun] lost its routes the same way, to the defaults.
void TestConfigToml::aCommentAfterATableHeaderKeepsTheTable()
{
    const QString src = QStringLiteral(
            "loglevel = \"info\"\n"
            "[endpoint] # the provider's server\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n"
            "password = \"s3cret\"\n"
            "[listener.tun]   #routes, see #12\n"
            "mtu_size = 1280\n"
            "excluded_routes = [\"10.9.0.0/16\"]\n"
            "[ \"odd#name\" ] # a # inside quotes is not the comment\n"
            "x = 1\n");

    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.password, QStringLiteral("s3cret"));
    QVERIFY2(c.extraRootKeys.isEmpty(), qPrintable(c.extraRootKeys));
    QVERIFY2(c.tunSection.contains(QStringLiteral("mtu_size = 1280")), qPrintable(c.tunSection));
    QVERIFY2(c.extraSections.contains(QStringLiteral("[\"odd#name\"]\nx = 1\n")), qPrintable(c.extraSections));

    // What migrateConfigPassword() writes once the store has taken the password.
    c.password.clear();
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(!rebuilt.contains(QStringLiteral("s3cret")), qPrintable(rebuilt));
    QVERIFY2(parseConfigToml(rebuilt).password.isEmpty(), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("excluded_routes = [\"10.9.0.0/16\"]")), qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains(QStringLiteral("192.168.0.0/16")), qPrintable(rebuilt));
    QCOMPARE(buildConfigToml(parseConfigToml(rebuilt)), rebuilt);
}

// What 1.2.2 made of such a config, as it is on disk now: the endpoint's keys up
// at the root, password among them, then an [endpoint] of its own without one.
// The password there is still the one to store, and the next rewrite clears the
// root out, so it does not stay in the file.
void TestConfigToml::endpointKeysLeftAtTheRootAreClearedOut()
{
    const QString src = QStringLiteral(
            "loglevel = \"info\"\n"
            "vpn_mode = \"general\"\n"
            "killswitch_enabled = false\n"
            "post_quantum_group_enabled = true\n"
            "dns_upstreams = []\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n"
            "password = \"s3cret\"\n"
            "certificate = \"\"\"\n"
            "-----BEGIN CERTIFICATE-----\n"
            "MIIB\n"
            "-----END CERTIFICATE-----\n"
            "\"\"\"\n"
            "provider_quirk = 42\n"
            "\n[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n"
            "client_random = \"\"\n"
            "custom_sni = \"\"\n"
            "has_ipv6 = true\n"
            "skip_verification = false\n"
            "upstream_protocol = \"http2\"\n"
            "anti_dpi = false\n"
            "certificate = \"\"\"\n"
            "-----BEGIN CERTIFICATE-----\n"
            "MIIB\n"
            "-----END CERTIFICATE-----\n"
            "\"\"\"\n"
            "\n[listener.tun]\n"
            "mtu_size = 1280\n");

    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.password, QStringLiteral("s3cret"));
    QCOMPARE(c.extraRootKeys, QStringLiteral("provider_quirk = 42\n"));
    c.password.clear();
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(!rebuilt.contains(QStringLiteral("s3cret")), qPrintable(rebuilt));
    QCOMPARE(rebuilt.count(QStringLiteral("hostname = ")), 1);
    QCOMPARE(rebuilt.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
    QVERIFY2(rebuilt.contains(QStringLiteral("provider_quirk = 42")), qPrintable(rebuilt));
}

// Each key is read from the table the core reads it from. Read from anywhere, a
// SOCKS listener's password was the server's whenever [endpoint] had none - which
// is every time after the first, since the first migration moves the real one
// into the credential store. The next one then stored the listener's over it.
void TestConfigToml::anotherTablesPasswordIsNotTheServers()
{
    const QString src = QStringLiteral(
            "loglevel = \"info\"\n"
            "[listener.socks]\n"
            "address = \"127.0.0.1:1080\"\n"
            "username = \"socks-user\"\n"
            "password = \"socks-pass\"\n"
            "[provider]\n"
            "password = \"portal-pass\"\n"
            "[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n");
    const ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.username, QStringLiteral("u"));
    QVERIFY2(c.password.isEmpty(), qPrintable(c.password));
    // And another table keeps what is its own. The SOCKS listener is not kept at
    // all: FreeTunnel runs the tunnel, and the core refuses a config with both.
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(rebuilt.contains(QStringLiteral("password = \"portal-pass\"")), qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains(QStringLiteral("socks-pass")), qPrintable(rebuilt));
}

// TOML lets a key be indented, and a provider's file may well be. Only keys at the
// start of a line were read, and since these are keys the rebuild writes, an
// indented one was not carried over either: the certificate came back empty, the
// address list empty (so the import was refused), and the password was neither
// moved to the credential store nor used to connect.
void TestConfigToml::indentedKeysAreRead()
{
    const QString src = QStringLiteral(
            "loglevel = \"info\"\n"
            "  dns_upstreams = [\"9.9.9.9\"]\n"
            "[endpoint]\n"
            "    hostname = \"vpn.example.com\"\n"
            "    addresses = [\n"
            "        \"1.2.3.4:443\",\n"
            "    ]\n"
            "\tusername = 'u'\n"
            "    password = \"s3cret\"\n"
            "    skip_verification = false\n"
            "    anti_dpi = true\n"
            "    certificate = \"\"\"\n"
            "-----BEGIN CERTIFICATE-----\n"
            "MIIB\n"
            "-----END CERTIFICATE-----\"\"\"\n"
            "[listener.tun]\n"
            "  mtu_size = 1280\n");
    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(c.addresses, QStringLiteral("1.2.3.4:443"));
    QCOMPARE(c.username, QStringLiteral("u"));
    QCOMPARE(c.password, QStringLiteral("s3cret"));
    QCOMPARE(c.antiDpi, true);
    QCOMPARE(c.dns, QStringLiteral("9.9.9.9"));
    QCOMPARE(c.certificate, QStringLiteral("-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----"));

    // The rewrite after the password has gone to the store: everything else kept.
    c.password.clear();
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(!rebuilt.contains(QStringLiteral("s3cret")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("MIIB")), qPrintable(rebuilt));
    QCOMPARE(parseConfigToml(rebuilt).addresses, QStringLiteral("1.2.3.4:443"));
}

// TOML lets a key be quoted, and "password" names the same key as password. A
// quoted key was compared as written, so it was neither read nor known: a quoted
// password stayed in the file as an unknown key, and a quoted key the rebuild
// writes itself came back twice, which the core refuses.
void TestConfigToml::aQuotedKeyIsTheSameKey()
{
    const QString src = QStringLiteral(
            "\"loglevel\" = \"debug\"\n"
            "[endpoint]\n"
            "\"hostname\" = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "'username' = 'u'\n"
            "  \"password\" = \"s3cret\"\n"
            "\"anti_dpi\" = true\n"
            "\"has.dot\" = 1\n");
    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(c.username, QStringLiteral("u"));
    QCOMPARE(c.password, QStringLiteral("s3cret"));
    QVERIFY(c.antiDpi);

    c.password.clear();
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(!rebuilt.contains(QStringLiteral("s3cret")), qPrintable(rebuilt));
    QCOMPARE(rebuilt.count(QStringLiteral("loglevel")), 1);
    QCOMPARE(rebuilt.count(QStringLiteral("hostname")), 1);
    QCOMPARE(rebuilt.count(QStringLiteral("anti_dpi")), 1);
    // A quoted name that is none of these keys is someone else's, kept as written.
    QVERIFY2(rebuilt.contains(QStringLiteral("\n\"has.dot\" = 1\n")), qPrintable(rebuilt));
}

// A table's name may be quoted as a key's may, part by part. Read as written,
// ["endpoint"] was not the endpoint: every setting in it read empty and the
// import failed. A part that needs its quotes names another table, though:
// ['listener.tun'] has a dot in its name and is not [listener.tun].
void TestConfigToml::aQuotedTableNameIsTheSameTable()
{
    const QString src = QStringLiteral(
            "[\"endpoint\"]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "password = \"s3cret\"\n"
            "[ \"listener\" . tun ]\n"
            "included_routes = [\"10.0.0.0/8\"]\n"
            "['listener.tun']\n"
            "dotted = 1\n"
            "[\"my server\"]\n"
            "spaced = 2\n");
    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(c.addresses, QStringLiteral("1.2.3.4:443"));
    QCOMPARE(c.password, QStringLiteral("s3cret"));
    QVERIFY2(c.tunSection.contains(QStringLiteral("10.0.0.0/8")), qPrintable(c.tunSection));
    QVERIFY2(!c.tunSection.contains(QStringLiteral("dotted")), qPrintable(c.tunSection));

    c.password.clear();
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(!rebuilt.contains(QStringLiteral("s3cret")), qPrintable(rebuilt));
    QCOMPARE(rebuilt.count(QStringLiteral("[endpoint]")), 1);
    QCOMPARE(rebuilt.count(QStringLiteral("hostname")), 1);
    QCOMPARE(rebuilt.count(QStringLiteral("[listener.tun]")), 1);
    QVERIFY2(rebuilt.contains(QStringLiteral("\n['listener.tun']\ndotted = 1\n")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("\n[\"my server\"]\nspaced = 2\n")), qPrintable(rebuilt));
}

// A value in triple quotes, on one line or several. The one-line spellings were
// tried alone, and matched the first two quotes of the three: an empty value,
// so the password was neither stored nor kept, and the rewrite dropped it.
void TestConfigToml::aValueInTripleQuotesIsRead()
{
    const QString src = QStringLiteral(
            "[endpoint]\n"
            "hostname = \"\"\"vpn.example.com\"\"\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = '''u'''\n"
            "password = \"\"\"s3\"cr\\\\et\"\"\"\n"
            "custom_sni = '''\n"
            "sni.example.com'''\n"
            "anti_dpi = true\n");
    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(c.username, QStringLiteral("u"));
    QCOMPARE(c.password, QStringLiteral("s3\"cr\\et"));
    QCOMPARE(c.customSni, QStringLiteral("sni.example.com"));
    QVERIFY(c.antiDpi);

    c.password.clear();
    const QString rebuilt = buildConfigToml(c);
    QVERIFY2(!rebuilt.contains(QStringLiteral("s3")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("hostname = \"vpn.example.com\"\n")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("custom_sni = \"sni.example.com\"\n")), qPrintable(rebuilt));
}

// A certificate is the one value that can hold anything, and now that a key may
// be indented, an indented line of its text must not be read as one.
void TestConfigToml::aCertificatesTextIsNotReadAsKeys()
{
    const QString src = QStringLiteral(
            "[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "certificate = \"\"\"\n"
            "-----BEGIN CERTIFICATE-----\n"
            "skip_verification = true\n"
            "  password = \"from-the-cert\"\n"
            "-----END CERTIFICATE-----\n"
            "\"\"\"\n"
            "anti_dpi = false\n");
    const ConfigToml c = parseConfigToml(src);
    QVERIFY(!c.skipVerification);
    QVERIFY2(c.password.isEmpty(), qPrintable(c.password));
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.com"));
}

// TrustTunnel's own template puts dns_upstreams in [endpoint], and the core takes
// that list over the one at the root. This editor writes the root's: it read
// whichever came first in the file, and kept the endpoint's as an unknown key, so
// a list changed here was not the one the core used.
void TestConfigToml::theDnsListIsReadWhereTheCoreReadsIt()
{
    const QString src = QStringLiteral(
            "dns_upstreams = [\"8.8.8.8\"]\n"
            "[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "dns_upstreams = [\"tls://1.1.1.1\"]\n");
    ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.dns, QStringLiteral("tls://1.1.1.1"));
    c.dns = QStringLiteral("9.9.9.9");
    const QString rebuilt = buildConfigToml(c);
    QCOMPARE(rebuilt.count(QStringLiteral("dns_upstreams")), 1);
    QCOMPARE(parseConfigToml(rebuilt).dns, QStringLiteral("9.9.9.9"));

    // An empty list in [endpoint] is still the endpoint's: the core uses it.
    const ConfigToml none = parseConfigToml(QStringLiteral(
            "dns_upstreams = [\"8.8.8.8\"]\n[endpoint]\ndns_upstreams = []\n"));
    QCOMPARE(none.dns, QString());
    // And with none there, the root's - as with something there that is not a list,
    // which the core passes over.
    QCOMPARE(parseConfigToml(QStringLiteral("dns_upstreams = [\"8.8.8.8\"]\n[endpoint]\n")).dns,
             QStringLiteral("8.8.8.8"));
    QCOMPARE(parseConfigToml(QStringLiteral(
                     "dns_upstreams = [\"8.8.8.8\"]\n[endpoint]\ndns_upstreams = \"1.1.1.1\"\n")).dns,
             QStringLiteral("8.8.8.8"));
}

// The editor has no switch for the post-quantum key exchange, so a config that
// turned it off had it turned on again by the next rewrite, and by the connect
// path every time: the core never saw the file's setting.
void TestConfigToml::postQuantumStaysOffWhenTheConfigTurnsItOff()
{
    const QString off = QStringLiteral(
            "loglevel = \"info\"\n"
            "post_quantum_group_enabled = false\n"
            "[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n");
    const ConfigToml c = parseConfigToml(off);
    QVERIFY(!c.postQuantum);
    const QString rebuilt = buildConfigToml(c, QStringLiteral("warn"));
    QVERIFY2(rebuilt.contains(QStringLiteral("\npost_quantum_group_enabled = false\n")), qPrintable(rebuilt));
    QCOMPARE(rebuilt.count(QStringLiteral("post_quantum_group_enabled")), 1);

    // Not mentioned, it is on, as it is in the core.
    QVERIFY(parseConfigToml(QStringLiteral("[endpoint]\nhostname = \"h\"\n")).postQuantum);
    QVERIFY(buildConfigToml(ConfigToml()).contains(QStringLiteral("post_quantum_group_enabled = true\n")));
}

// The core splits client_random at the first slash and refuses the whole config
// when nothing follows it. Only one trailing slash was dropped, so "aa//" reached
// it as "aa/". And the editor and link import check a value against the one rule
// of what the core can use: whole bytes of hex, at most 32, before and after.
void TestConfigToml::aClientRandomGoesToTheCoreInAFormItTakes()
{
    QCOMPARE(clientRandomForCore(QStringLiteral("deadbeef/")), QStringLiteral("deadbeef"));
    QCOMPARE(clientRandomForCore(QStringLiteral("aa//")), QStringLiteral("aa"));
    QCOMPARE(clientRandomForCore(QStringLiteral(" aa/ff00/// ")), QStringLiteral("aa/ff00"));
    QCOMPARE(clientRandomForCore(QStringLiteral("/ffff")), QString());
    QCOMPARE(clientRandomForCore(QStringLiteral("aa/bb/cc")), QStringLiteral("aa/bb/cc"));

    // From the file too, whatever wrote it, and the 1.2.2 pair the same way.
    const auto endpoint = [](const QString &keys) {
        return QStringLiteral("[endpoint]\nhostname = \"h\"\n") + keys;
    };
    QCOMPARE(parseConfigToml(endpoint(QStringLiteral("client_random = \"aa////\"\n"))).clientRandom,
             QStringLiteral("aa"));
    QCOMPARE(parseConfigToml(endpoint(QStringLiteral("client_random = \"aa\"\nclient_random_mask = \"/\"\n")))
                     .clientRandom,
             QStringLiteral("aa"));
    QVERIFY(buildConfigToml(parseConfigToml(endpoint(QStringLiteral("client_random = \"/ff\"\n"))))
                    .contains(QStringLiteral("client_random = \"\"\n")));

    for (const QString &good : {QString(), QStringLiteral("aa"), QStringLiteral("DeadBeef"),
                                QStringLiteral("aa/ff00"), QString(64, QLatin1Char('f')),
                                QString(64, QLatin1Char('f')) + QStringLiteral("/") + QString(64, QLatin1Char('0'))})
        QVERIFY2(isValidClientRandom(good), qPrintable(good));
    for (const QString &bad : {QStringLiteral("abc"), QStringLiteral("zz"), QStringLiteral("aa/"),
                               QStringLiteral("/aa"), QStringLiteral("aa/bbb"), QStringLiteral("aa/bb/cc"),
                               QStringLiteral(" aa"), QString(66, QLatin1Char('f')),
                               QStringLiteral("aa/") + QString(66, QLatin1Char('0'))})
        QVERIFY2(!isValidClientRandom(bad), qPrintable(bad));
}

// TrustTunnel's client keeps the subscription address in [endpoint.subscription].
// It holds the VPN password in plain text, and every key of an unknown table used
// to be kept as written, so the password stayed on disk after the import had
// moved the other copy of it to the credential store.
void TestConfigToml::theSubscriptionTableIsLeftOut()
{
    bool removed = false;
    const QString out = withoutSubscription(configWithSubscriptions(), &removed);
    QVERIFY(removed);
    QVERIFY2(!out.contains(QStringLiteral("subscription")), qPrintable(out));
    QVERIFY2(!out.contains(QStringLiteral("https://alice")), qPrintable(out));
    QVERIFY2(!out.contains(QStringLiteral("kept up to date by the client")), qPrintable(out));

    // Everything else exactly as it was, comments and unknown keys included.
    QCOMPARE(out, QStringLiteral("# Exported by the endpoint\n"
                                 "loglevel = \"info\"\n"
                                 "provider_quirk = 42\n"
                                 "\n[endpoint]\n"
                                 "hostname = \"vpn.example.com\"\n"
                                 "addresses = [\"1.2.3.4:443\"]\n"
                                 "username = \"alice\"\n"
                                 "password = \"s3cret\"\n"
                                 "client_random_auth_key = \"\"\n"
                                 "provider_tag = \"gold\"\n"
                                 "\n[listener.tun]\n"
                                 "mtu_size = 1280\n"
                                 "\n[provider]\n"
                                 "port = 1080\n"));

    // A table inside it, an array of them, a quoted name: all the same table.
    for (const QString &header : {QStringLiteral("[endpoint.subscription.extra]"),
                                  QStringLiteral("[[endpoint.subscription]]"),
                                  QStringLiteral("[\"endpoint\".'subscription'] # the client's")}) {
        const QString in = QStringLiteral("[endpoint]\nhostname = \"h\"\n") + header
                + QStringLiteral("\nurl = \"https://u:p@h/s\"\n[provider]\nport = 1\n");
        const QString stripped = withoutSubscription(in, &removed);
        QVERIFY2(removed, qPrintable(header));
        QCOMPARE(stripped, QStringLiteral("[endpoint]\nhostname = \"h\"\n[provider]\nport = 1\n"));
    }
    // A table of that name somewhere else is someone else's.
    const QString other = QStringLiteral("[provider.subscription]\nurl = \"x\"\n");
    QCOMPARE(withoutSubscription(other, &removed), other);
    QVERIFY(!removed);
}

// The endpoint's export writes subscription_url beside the server's other keys,
// at the top of its file; pasted into a client config, that is under [endpoint].
// A value over several lines goes whole, and a key of another table is left alone.
void TestConfigToml::aSubscriptionUrlKeyIsLeftOutWhereverTheExportPutsIt()
{
    bool removed = false;
    QCOMPARE(withoutSubscription(QStringLiteral("subscription_url = \"https://u:p@h/s\"\nloglevel = \"info\"\n"),
                                 &removed),
             QStringLiteral("loglevel = \"info\"\n"));
    QVERIFY(removed);
    QCOMPARE(withoutSubscription(QStringLiteral("[endpoint]\n  \"subscription_url\" = 'https://u:p@h/s' # x\n"
                                                "hostname = \"h\"\n"),
                                 &removed),
             QStringLiteral("[endpoint]\nhostname = \"h\"\n"));
    QVERIFY(removed);
    QCOMPARE(withoutSubscription(QStringLiteral("[endpoint]\nsubscription_url = \"\"\"\n"
                                                "https://u:p@h/s\n\"\"\"\nhostname = \"h\"\n"),
                                 &removed),
             QStringLiteral("[endpoint]\nhostname = \"h\"\n"));
    QVERIFY(removed);

    const QString elsewhere = QStringLiteral("[provider]\nsubscription_url = \"https://u:p@h/s\"\n");
    QCOMPARE(withoutSubscription(elsewhere, &removed), elsewhere);
    QVERIFY(!removed);
    // A certificate's text is a value, however much it looks like a key.
    const QString inCertificate = QStringLiteral("[endpoint]\ncertificate = \"\"\"\n"
                                                 "subscription_url = \"https://u:p@h/s\"\n\"\"\"\n");
    QCOMPARE(withoutSubscription(inCertificate, &removed), inCertificate);
    QVERIFY(!removed);
}

// TOML lets a table be written as a key just as well: inline, or dotted.
void TestConfigToml::aSubscriptionSpelledAsAKeyIsLeftOut()
{
    bool removed = false;
    for (const QString &line : {QStringLiteral("subscription = { url = \"https://u:p@h/s\" }"),
                                QStringLiteral("subscription.url = \"https://u:p@h/s\""),
                                QStringLiteral("\"subscription\" . url = \"https://u:p@h/s\"")}) {
        QCOMPARE(withoutSubscription(QStringLiteral("[endpoint]\n") + line + QStringLiteral("\nhostname = \"h\"\n"),
                                     &removed),
                 QStringLiteral("[endpoint]\nhostname = \"h\"\n"));
        QVERIFY2(removed, qPrintable(line));
    }
    QCOMPARE(withoutSubscription(QStringLiteral("endpoint.subscription.url = \"https://u:p@h/s\"\nx = 1\n"),
                                 &removed),
             QStringLiteral("x = 1\n"));
    QVERIFY(removed);
}

// Byte for byte, line endings and a missing final newline included: the import
// strips every file it is given, and most have nothing to strip.
void TestConfigToml::aConfigWithoutASubscriptionComesBackAsItWas()
{
    bool removed = true;
    for (const QString &in : {QStringLiteral("[endpoint]\r\nhostname = \"h\"\r\n"),
                              QStringLiteral("[endpoint]\nhostname = \"h\""), QString(),
                              QStringLiteral("\n\n# only a comment\n")}) {
        QCOMPARE(withoutSubscription(in, &removed), in);
        QVERIFY(!removed);
    }
}

// A config imported by 1.2.3 or earlier can hold a subscription already. Every
// rewrite goes through parseConfigToml(), so the next one leaves it out, and
// everything else unknown is still carried over as before.
void TestConfigToml::aRewriteNeverPutsASubscriptionBack()
{
    const ConfigToml parsed = parseConfigToml(configWithSubscriptions());
    QCOMPARE(parsed.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(parsed.password, QStringLiteral("s3cret"));
    const QString rebuilt = buildConfigToml(parsed);
    QVERIFY2(!rebuilt.contains(QStringLiteral("subscription")), qPrintable(rebuilt));
    QVERIFY2(!rebuilt.contains(QStringLiteral("https://alice")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("provider_quirk = 42")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("provider_tag = \"gold\"")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("[provider]\nport = 1080")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("mtu_size = 1280")), qPrintable(rebuilt));
    // The empty auth key the export writes is a key this editor does not know,
    // and is carried over as written, as such keys always were.
    QVERIFY2(rebuilt.contains(QStringLiteral("client_random_auth_key = \"\"\n")), qPrintable(rebuilt));
    QCOMPARE(buildConfigToml(parseConfigToml(rebuilt)), rebuilt);
}

// The endpoint's export always writes client_random_auth_key, as "" for a server
// without one; only a key with something in it is one.
void TestConfigToml::theAuthKeyIsReadFromTheEndpoint()
{
    QVERIFY(endpointClientRandomAuthKey(configWithSubscriptions()).isEmpty());
    QVERIFY(endpointClientRandomAuthKey(QStringLiteral("[endpoint]\nhostname = \"h\"\n")).isEmpty());
    QVERIFY(endpointClientRandomAuthKey(QStringLiteral("[endpoint]\nclient_random_auth_key = \"  \"\n")).isEmpty());
    QCOMPARE(endpointClientRandomAuthKey(QStringLiteral("[endpoint]\nclient_random_auth_key = \"aabbccdd\"\n")),
             QStringLiteral("aabbccdd"));
    QCOMPARE(endpointClientRandomAuthKey(QStringLiteral("[endpoint]\n  'client_random_auth_key' = 'aabb' # k\n")),
             QStringLiteral("aabb"));
    // From [endpoint], where the core reads it, and nowhere else.
    QVERIFY(endpointClientRandomAuthKey(QStringLiteral("[provider]\nclient_random_auth_key = \"aabb\"\n"
                                                       "[endpoint]\nhostname = \"h\"\n"))
                    .isEmpty());
}

// ---- certificates -------------------------------------------------------------

// The one DER reader: a link's certificates are split with it, and the
// certificate check reads the fields inside one with it, by their tags.
void TestConfigToml::derElementEndReadsOneElementOfTheTagAsked()
{
    const QByteArray bitString = QByteArray::fromHex("030100");
    QCOMPARE(derElementEnd(bitString, 0, 0x03), 3);
    QCOMPARE(derElementEnd(bitString, 0), -1); // a SEQUENCE unless told otherwise
    int content = -1;
    QCOMPARE(derElementEnd(QByteArray::fromHex("3003020101"), 0, 0x30, &content), 5);
    QCOMPARE(content, 2);
    // A length of one byte past the count byte, and of two; and the element after.
    const QByteArray one = certs::derElement(0x30, QByteArray(200, 'A'));
    QCOMPARE(derElementEnd(one, 0, 0x30, &content), 203);
    QCOMPARE(content, 3);
    const QByteArray two = certs::derElement(0x30, QByteArray(300, 'A')) + bitString;
    QCOMPARE(derElementEnd(two, 0, 0x30, &content), 304);
    QCOMPARE(content, 4);
    QCOMPARE(derElementEnd(two, 304, 0x03, &content), 307);
    QCOMPARE(content, 306);
    QCOMPARE(derElementEnd(two, 306, 0x03), -1);

    // Nothing that is not a whole element, and the content start left alone.
    const QList<QByteArray> broken{
            QByteArray(),
            QByteArray::fromHex("30"),
            QByteArray::fromHex("3001"),                   // the content is missing
            QByteArray::fromHex("308005000000"),           // indefinite, which DER forbids
            QByteArray::fromHex("3085000000000105"),       // five length bytes
            QByteArray::fromHex("308201"),                 // the length cut short
            QByteArray::fromHex("30847fffffff00"),         // far past the end
            QByteArray::fromHex("3082010041") + QByteArray(254, 'A'), // one byte short
    };
    for (const QByteArray &bytes : broken) {
        content = -1;
        QCOMPARE(derElementEnd(bytes, 0, 0x30, &content), -1);
        QCOMPARE(content, -1);
    }
}

void TestConfigToml::isCertificateDer_data()
{
    QTest::addColumn<QByteArray>("der");
    QTest::addColumn<bool>("certificate");
    const QByteArray ca = certs::derOf(certs::kCaPem);
    QTest::newRow("a certificate authority") << ca << true;
    QTest::newRow("a server certificate") << certs::derOf(certs::kLeafPem) << true;
    QTest::newRow("the smallest shape") << QByteArray::fromHex("300b3002050030020500030100") << true;
    // Only the BIT STRING has to hold something; the two SEQUENCEs are the shape.
    QTest::newRow("the shape, empty SEQUENCEs") << QByteArray::fromHex("300730003000030100") << true;
    QTest::newRow("the shape, one length byte") << certs::certShaped(200) << true;
    QTest::newRow("the shape, two length bytes") << certs::certShaped(300) << true;
    // What the link tests stood in for a certificate with before: a SEQUENCE
    // holding an INTEGER, which is how a key starts.
    QTest::newRow("a SEQUENCE of an INTEGER") << QByteArray::fromHex("3003020101") << false;
    QTest::newRow("a PKCS#8 key") << certs::kPkcs8Der << false;
    QTest::newRow("a PKCS#1 key") << certs::kPkcs1Der << false;
    QTest::newRow("a SEC1 key") << certs::kSec1Der << false;
    QTest::newRow("an encrypted PKCS#8 key") << certs::kEncryptedPkcs8Der << false;
    QTest::newRow("a byte after it") << ca + '\0' << false;
    QTest::newRow("two certificates") << ca + certs::derOf(certs::kLeafPem) << false;
    QTest::newRow("cut short") << ca.chopped(1) << false;
    QTest::newRow("nothing") << QByteArray() << false;
    QTest::newRow("a SET") << QByteArray::fromHex("310b3002050030020500030100") << false;
    QTest::newRow("an indefinite length") << QByteArray::fromHex("308030020500300205000301000000") << false;
    QTest::newRow("an empty BIT STRING") << QByteArray::fromHex("300a30020500300205000300") << false;
    QTest::newRow("no BIT STRING") << QByteArray::fromHex("30083002050030020500") << false;
    QTest::newRow("a field after the BIT STRING") << QByteArray::fromHex("300d30020500300205000301000500") << false;
    QTest::newRow("the fields out of order") << QByteArray::fromHex("300b0301003002050030020500") << false;
    QTest::newRow("a field past the end") << QByteArray::fromHex("300b3002050030020500030200") << false;
    QTest::newRow("a field past its SEQUENCE") << QByteArray::fromHex("300a3002050030020500030100") << false;
    QTest::newRow("a tag in the high form") << QByteArray::fromHex("300c30020500300205001f030100") << false;
}

void TestConfigToml::isCertificateDer()
{
    QFETCH(QByteArray, der);
    QFETCH(bool, certificate);
    QCOMPARE(freetunnel::isCertificateDer(der), certificate);
}

void TestConfigToml::certificatesOnly_data()
{
    using namespace certs;
    QTest::addColumn<QString>("pem");
    QTest::addColumn<QString>("kept");
    QTest::addColumn<bool>("dropped");
    const auto wrapped = [](const QByteArray &der, int columns) {
        const QByteArray b64 = der.toBase64();
        QString out = QStringLiteral("-----BEGIN CERTIFICATE-----\n");
        for (qsizetype i = 0; i < b64.size(); i += columns)
            out += QString::fromLatin1(b64.mid(i, columns)) + QLatin1Char('\n');
        return out + QStringLiteral("-----END CERTIFICATE-----\n");
    };

    // Nothing to leave out: the text comes back exactly as it was.
    QString crlf = kChainPem;
    crlf.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    QString oneLine = kCaPem;
    oneLine.remove(QLatin1Char('\n'));
    const QString legacy = pemBlock(QStringLiteral("X509 CERTIFICATE"), derOf(kCaPem));
    // FreeTunnel 1.1.9 and earlier wrote a link's chain as one block, of which
    // OpenSSL reads the first certificate. It holds nothing but certificates.
    const QString oneBlock = pemBlock(QStringLiteral("CERTIFICATE"), derOf(kLeafPem) + derOf(kCaPem));
    QTest::newRow("a certificate") << kCaPem << kCaPem << false;
    QTest::newRow("a chain") << kChainPem << kChainPem << false;
    QTest::newRow("a chain in one block, as 1.1.9 wrote it") << oneBlock << oneBlock << false;
    QTest::newRow("CRLF") << crlf << crlf << false;
    QTest::newRow("76 columns") << wrapped(derOf(kCaPem), 76) << wrapped(derOf(kCaPem), 76) << false;
    QTest::newRow("on one line") << oneLine << oneLine << false;
    QTest::newRow("X509 CERTIFICATE, which OpenSSL reads") << legacy << legacy << false;
    QTest::newRow("blank lines around") << QStringLiteral("\n\n") + kCaPem + QStringLiteral(" \n")
                                        << QStringLiteral("\n\n") + kCaPem + QStringLiteral(" \n") << false;
    QTest::newRow("empty") << QString() << QString() << false;

    // The certificates of what holds more, in order, and nothing else.
    QTest::newRow("a certificate and its key") << kLeafPem + keyPem() << kLeafPem << true;
    QTest::newRow("the key first") << keyPem() + kLeafPem << kLeafPem << true;
    QTest::newRow("a key inside a chain") << kLeafPem + keyPem() + kCaPem << kChainPem << true;
    QTest::newRow("an export of a PKCS#12 file")
            << kBagAttributes + kLeafPem + kBagAttributes + keyPem() << kLeafPem << true;
    QTest::newRow("an RSA key") << pemBlock(keyLabel(QStringLiteral("RSA ")), kPkcs1Der) + kCaPem << kCaPem << true;
    QTest::newRow("an EC key") << pemBlock(keyLabel(QStringLiteral("EC ")), kSec1Der) + kCaPem << kCaPem << true;
    QTest::newRow("an encrypted key")
            << pemBlock(keyLabel(QStringLiteral("ENCRYPTED ")), kEncryptedPkcs8Der) + kCaPem << kCaPem << true;
    QTest::newRow("a certificate request")
            << pemBlock(QStringLiteral("CERTIFICATE REQUEST"), certShaped(4)) + kCaPem << kCaPem << true;
    QTest::newRow("a key labelled as a certificate")
            << pemBlock(QStringLiteral("CERTIFICATE"), kPkcs8Der) + kCaPem << kCaPem << true;
    QTest::newRow("a key after a certificate in one block")
            << pemBlock(QStringLiteral("CERTIFICATE"), derOf(kLeafPem) + kPkcs8Der) + kCaPem << kCaPem << true;
    QTest::newRow("a byte after a chain in one block")
            << pemBlock(QStringLiteral("CERTIFICATE"), derOf(kLeafPem) + derOf(kCaPem) + '\0') + kCaPem << kCaPem
            << true;
    QTest::newRow("an empty block") << pemBlock(QStringLiteral("CERTIFICATE"), QByteArray()) + kCaPem << kCaPem
                                    << true;
    QTest::newRow("a key inside a certificate block")
            << QStringLiteral("-----BEGIN CERTIFICATE-----\n") + keyPem()
                       + QStringLiteral("-----END CERTIFICATE-----\n") + kCaPem
            << kCaPem << true;
    // Decoded leniently, the stray padding is skipped and the certificate is there.
    QString padded = kLeafPem;
    padded.replace(QLatin1String("MIIB1z"), QLatin1String("MIIB=1z"));
    QTest::newRow("base64 that is not") << padded + kCaPem << kCaPem << true;
    QString mismatched = kLeafPem;
    mismatched.replace(QLatin1String("END CERTIFICATE"), QLatin1String("END X509 CERTIFICATE"));
    QTest::newRow("an END of another label") << mismatched + kCaPem << kCaPem << true;
    QTest::newRow("no END") << kCaPem + kLeafPem.left(kLeafPem.indexOf(QLatin1String("-----END")))
                            << kCaPem << true;
    QString lower = kCaPem;
    lower.replace(QLatin1String("CERTIFICATE"), QLatin1String("certificate"));
    QTest::newRow("a label in lower case") << lower << QString() << true;
    QTest::newRow("text after the certificate") << kCaPem + QStringLiteral("see the README\n") << kCaPem << true;
    QTest::newRow("a character after the END line") << kCaPem.chopped(1) + QStringLiteral("x") << kCaPem << true;
    QTest::newRow("text alone") << QStringLiteral("see the README") << QString() << true;

    // And nothing at all when there is no certificate.
    QTest::newRow("a key alone") << keyPem() << QString() << true;
    QTest::newRow("a trusted certificate")
            << pemBlock(QStringLiteral("TRUSTED CERTIFICATE"), derOf(kCaPem)) << QString() << true;
}

void TestConfigToml::certificatesOnly()
{
    QFETCH(QString, pem);
    QFETCH(QString, kept);
    QFETCH(bool, dropped);
    bool lost = !dropped;
    const QString out = freetunnel::certificatesOnly(pem, &lost);
    QCOMPARE(out, kept);
    QCOMPARE(lost, dropped);
    // Whatever went in, no key comes out, and what comes out stays as it is.
    QVERIFY2(!out.contains(certs::keyLabel()), qPrintable(out));
    QCOMPARE(freetunnel::certificatesOnly(out, &lost), out);
    QVERIFY(!lost);
    // The DER is the kept blocks', and says the same about what was left out.
    QCOMPARE(certificatesDer(pem, &lost), certificatesDer(out));
    QCOMPARE(lost, dropped);
}

void TestConfigToml::certificatesDerMatchesTheKeptBlocks()
{
    using namespace certs;
    const QByteArray chain = derOf(kLeafPem) + derOf(kCaPem);
    bool dropped = true;
    QCOMPARE(certificatesDer(kChainPem, &dropped), chain);
    QVERIFY(!dropped);
    QCOMPARE(certificatesDer(kLeafPem + keyPem() + kCaPem, &dropped), chain);
    QVERIFY(dropped);
    QVERIFY(certificatesDer(keyPem(), &dropped).isEmpty());
    QVERIFY(dropped);
    QVERIFY(certificatesDer(QString(), &dropped).isEmpty());
    QVERIFY(!dropped);
    QCOMPARE(certificatesDer(kCaPem), derOf(kCaPem));
    // A chain in one block, as 1.1.9 wrote it, is the whole chain.
    QCOMPARE(certificatesDer(pemBlock(QStringLiteral("CERTIFICATE"), chain), &dropped), chain);
    QVERIFY(!dropped);

    // Strict base64. Decoded leniently, as derOf() does, a stray padding
    // character is skipped and the certificate comes out whole; it is a damaged
    // block all the same.
    QString padded = kLeafPem;
    padded.replace(QLatin1String("MIIB1z"), QLatin1String("MIIB=1z"));
    QCOMPARE(derOf(padded), derOf(kLeafPem));
    QVERIFY(certificatesDer(padded, &dropped).isEmpty());
    QVERIFY(dropped);
}

// The certificate's lines and nothing else: a config is often a file the user
// wrote, and the import stores it as written.
void TestConfigToml::withCertificatesOnlyChangesOnlyTheCertificate()
{
    using namespace certs;
    const QString rest = QStringLiteral("custom_sni = \"x\" # after it\n"
                                        "\n[listener.tun]\n"
                                        "mtu_size = 1280\n");
    const QString top = QStringLiteral("# my server\n"
                                       "loglevel = \"info\"\n"
                                       "\n[provider]\n"
                                       "certificate = \"\"\"\n")
            + keyPem()
            + QStringLiteral("\"\"\"\n"
                             "\n[\"endpoint\"]  # the server\n"
                             "hostname = \"vpn.fixture.invalid\"\n"
                             "note = \"\"\"\n"
                             "certificate = \"not this one\"\n"
                             "\"\"\"\n");
    const QString before = top + QStringLiteral("certificate = \"\"\"\n") + kBagAttributes + kLeafPem + keyPem()
            + QStringLiteral("\"\"\"\n") + rest;
    bool trimmed = false;
    const QString after = withCertificatesOnly(before, &trimmed);
    QVERIFY(trimmed);
    // [provider]'s certificate is not the core's, and stays as it was.
    QCOMPARE(after, top + QStringLiteral("certificate = \"\"\"\n") + kLeafPem + QStringLiteral("\"\"\"\n") + rest);
    QCOMPARE(parseConfigToml(after).certificate, kLeafPem.trimmed());
    QCOMPARE(withCertificatesOnly(after, &trimmed), after);
    QVERIFY(!trimmed);

    // Nothing left: no certificate, as the core makes of one with none in it.
    const QString keyOnly = QStringLiteral("[endpoint]\nhostname = \"h\"\ncertificate = \"\"\"\n") + keyPem()
            + QStringLiteral("\"\"\"\nusername = \"u\"");
    QCOMPARE(withCertificatesOnly(keyOnly, &trimmed),
             QStringLiteral("[endpoint]\nhostname = \"h\"\ncertificate = \"\"\nusername = \"u\""));
    QVERIFY(trimmed);

    // Every certificate the file sets, each with its own, and not only the one
    // read: the core refuses a file that sets it twice, or that has two
    // [endpoint] tables, but the copy kept here would still hold the key.
    const auto value = [](const QString &pem) {
        return QStringLiteral("certificate = \"\"\"\n") + pem + QStringLiteral("\"\"\"\n");
    };
    const QString head = QStringLiteral("[endpoint]\nhostname = \"h\"\n");
    const QString tail = QStringLiteral("username = \"u\"\n");
    for (const QString &between : {QString(), QStringLiteral("\n[endpoint]\n")}) {
        const QString secondKeyed = head + value(kLeafPem) + between + value(kCaPem + keyPem()) + tail;
        QCOMPARE(withCertificatesOnly(secondKeyed, &trimmed), head + value(kLeafPem) + between + value(kCaPem) + tail);
        QVERIFY(trimmed);
        const QString firstKeyed = head + value(kLeafPem + keyPem()) + between + value(kCaPem) + tail;
        QCOMPARE(withCertificatesOnly(firstKeyed, &trimmed), head + value(kLeafPem) + between + value(kCaPem) + tail);
        QVERIFY(trimmed);
    }
    // One still open where the file ends is not lost, any line of it.
    const QString unclosed = QStringLiteral("certificate = '''\n") + kCaPem;
    QCOMPARE(withCertificatesOnly(head + value(kLeafPem + keyPem()) + unclosed, &trimmed),
             head + value(kLeafPem) + unclosed);
    QVERIFY(trimmed);
}

// Every spelling readString() reads.
void TestConfigToml::withCertificatesOnlyReadsEverySpelling()
{
    using namespace certs;
    const QString combined = kLeafPem + keyPem();
    QString escaped = combined;
    escaped.replace(QLatin1String("\n"), QLatin1String("\\n"));
    QString escapedCrlf = combined;
    escapedCrlf.replace(QLatin1String("\n"), QLatin1String("\\r\\n"));
    QString oneLine = combined;
    oneLine.remove(QLatin1Char('\n'));
    QString oneLineLeaf = kLeafPem;
    oneLineLeaf.remove(QLatin1Char('\n'));
    const QList<QPair<QString, QString>> spellings{
            {QStringLiteral("certificate = \"\"\"\n") + combined + QStringLiteral("\"\"\""), kLeafPem},
            {QStringLiteral("certificate = '''\n") + combined + QStringLiteral("'''"), kLeafPem},
            {QStringLiteral("certificate = \"") + escaped + QStringLiteral("\""), kLeafPem},
            {QStringLiteral("certificate = \"") + escapedCrlf + QStringLiteral("\""), kLeafPem},
            {QStringLiteral("  \"certificate\" = '") + oneLine + QStringLiteral("' # pasted"), oneLineLeaf},
    };
    for (const auto &[value, leaf] : spellings) {
        const QString toml = QStringLiteral("[endpoint]\nhostname = \"h\"\n") + value
                + QStringLiteral("\nusername = \"u\"\n");
        bool trimmed = false;
        const QString out = withCertificatesOnly(toml, &trimmed);
        QVERIFY2(trimmed, qPrintable(value));
        QCOMPARE(out, QStringLiteral("[endpoint]\nhostname = \"h\"\ncertificate = \"\"\"\n") + leaf.trimmed()
                              + QStringLiteral("\n\"\"\"\nusername = \"u\"\n"));
    }
}

// Byte for byte, line endings and a missing final newline included: the import
// and the start of the app run every config through this, and most have nothing
// to leave out.
void TestConfigToml::withCertificatesOnlyLeavesACleanConfigAsItWas()
{
    using namespace certs;
    QString crlfChain = kChainPem;
    crlfChain.replace(QLatin1String("\n"), QLatin1String("\r\n"));
    const QStringList clean{
            QStringLiteral("[endpoint]\r\nhostname = \"h\"\r\ncertificate = \"\"\"\r\n") + crlfChain
                    + QStringLiteral("\"\"\"\r\n"),
            QStringLiteral("[endpoint]\ncertificate = \"\"\"\n") + kChainPem + QStringLiteral("\"\"\""),
            // A chain in one block, as 1.1.9 wrote it.
            QStringLiteral("[endpoint]\ncertificate = \"\"\"\n")
                    + pemBlock(QStringLiteral("CERTIFICATE"), derOf(kLeafPem) + derOf(kCaPem))
                    + QStringLiteral("\"\"\"\n"),
            QStringLiteral("[endpoint]\ncertificate = \"\"\n"),
            QStringLiteral("[endpoint]\nhostname = \"h\"\n"),
            QString(),
            QStringLiteral("# only a comment\n"),
            // A key outside [endpoint], or text inside another value, is not the
            // certificate the core reads.
            QStringLiteral("[provider]\ncertificate = \"\"\"\n") + keyPem()
                    + QStringLiteral("\"\"\"\n[endpoint]\nnote = '''\ncertificate = \"x\"\n'''\n"),
    };
    for (const QString &in : clean) {
        bool trimmed = true;
        QCOMPARE(withCertificatesOnly(in, &trimmed), in);
        QVERIFY2(!trimmed, qPrintable(in));
    }
}

// Pieces of what a certificate field holds, in any order and now and then
// damaged: no key comes out, what comes out stays as it is, and every element
// of the DER is a certificate. Random bytes as DER are read no further than
// they go.
void TestConfigToml::randomCertificatesKeepTheRules()
{
    using namespace certs;
    bool fromEnv = false;
    const quint32 env = qEnvironmentVariable("FT_FUZZ_SEED").toUInt(&fromEnv);
    const quint32 seed = fromEnv ? env : 20261026u;
    qInfo("randomCertificatesKeepTheRules: seed %u (FT_FUZZ_SEED=%u repeats it)", seed, seed);
    QRandomGenerator rng(seed);
    const QStringList pieces{kCaPem,
                             kLeafPem,
                             keyPem(),
                             kBagAttributes,
                             pemBlock(keyLabel(QStringLiteral("RSA ")), kPkcs1Der),
                             pemBlock(QStringLiteral("CERTIFICATE"), kSec1Der),
                             pemBlock(QStringLiteral("CERTIFICATE"), derOf(kLeafPem) + derOf(kCaPem)),
                             pemBlock(QStringLiteral("CERTIFICATE"), derOf(kLeafPem) + kPkcs8Der),
                             QStringLiteral("-----BEGIN CERTIFICATE-----\n"),
                             QStringLiteral("-----END CERTIFICATE-----\n"),
                             QStringLiteral("\r\n"),
                             QStringLiteral(" "),
                             QStringLiteral("text")};
    for (int i = 0; i < 3000; ++i) {
        QString pem;
        const int count = rng.bounded(1, 6);
        for (int j = 0; j < count; ++j)
            pem += pieces.at(rng.bounded(static_cast<int>(pieces.size())));
        if (rng.bounded(3) == 0)
            pem[rng.bounded(static_cast<int>(pem.size()))] = QChar(static_cast<char16_t>(rng.bounded(0x20, 0x7F)));
        bool dropped = false;
        const QString out = freetunnel::certificatesOnly(pem, &dropped);
        QVERIFY2(!out.contains(keyLabel()), qPrintable(pem));
        QVERIFY2(dropped || out == pem, qPrintable(pem));
        bool again = true;
        QVERIFY2(freetunnel::certificatesOnly(out, &again) == out && !again, qPrintable(pem));
        const QByteArray der = certificatesDer(pem);
        QVERIFY2(der == certificatesDer(out), qPrintable(pem));
        for (int pos = 0; pos < der.size();) {
            const int end = derElementEnd(der, pos);
            QVERIFY2(end > pos && freetunnel::isCertificateDer(der.mid(pos, end - pos)), qPrintable(pem));
            pos = end;
        }
    }
    for (int i = 0; i < 3000; ++i) {
        QByteArray der(rng.bounded(0, 48), '\0');
        for (char &byte : der)
            byte = static_cast<char>(rng.generate() & 0xFF);
        if (!der.isEmpty() && rng.bounded(2) == 0)
            der[0] = 0x30; // a SEQUENCE far more often than by chance
        int content = -1;
        const int end = derElementEnd(der, 0, 0x30, &content);
        QVERIFY(end == -1 || (content >= 2 && content <= end && end <= der.size()));
        if (freetunnel::isCertificateDer(der))
            QCOMPARE(end, der.size());
    }
}

QTEST_MAIN(TestConfigToml)
#include "test_configtoml.moc"
