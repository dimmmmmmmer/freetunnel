// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

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
            "\n[listener.socks]\n"            // a table this editor never writes
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
    QVERIFY2(rebuilt.contains(QStringLiteral("[listener.socks]")), qPrintable(rebuilt));
    QVERIFY2(rebuilt.contains(QStringLiteral("port = 1080")), qPrintable(rebuilt));
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
            "[endpoint]\n"
            "hostname = \"vpn.example.com\"\n"
            "addresses = [\"1.2.3.4:443\"]\n"
            "username = \"u\"\n");
    const ConfigToml c = parseConfigToml(src);
    QCOMPARE(c.username, QStringLiteral("u"));
    QVERIFY2(c.password.isEmpty(), qPrintable(c.password));
    // And the listener keeps what is its own.
    QVERIFY2(buildConfigToml(c).contains(QStringLiteral("password = \"socks-pass\"")),
             qPrintable(buildConfigToml(c)));
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

QTEST_MAIN(TestConfigToml)
#include "test_configtoml.moc"
