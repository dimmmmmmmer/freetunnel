// cppcheck-suppress-file missingIncludeSystem
// Settings that cross the privilege boundary and are acted on only inside the
// REAL helper (tests/real_helper.h): sent by the real GUI client, parsed by the
// real server, and read back from the core the helper built.
//
// test_helper_server's killSwitchAndSplitSettingsReachTheCoreInsideTheHelper
// does this for the kill switch, the mode, routes and exclusions. The ones here
// had no test on either side of the wire: MockHelperServer ignores them, and the
// in-process tests never serialise them at all. The endpoint's TLS settings
// could not have had one: the mock core read nothing from [endpoint].
#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>

#include <string>
#include <vector>

#include "core/ConfigToml.h"
#include "real_helper.h"
#include "vpn/vpn_helper_client.h"

namespace {

QString dumpValue(const QString &path, const QString &key)
{
    for (const QString &line : realhelper::fileLines(path)) {
        if (line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1);
    }
    return QString();
}

// A self-signed certificate's PEM, as a provider pins it; its content is never
// parsed, only carried.
const QString kPinnedPem = QStringLiteral(
        "-----BEGIN CERTIFICATE-----\n"
        "MIIBszCCAVmgAwIBAgIUQ2VydGlmaWNhdGVQaW5uZWRGb3JUZXN0czAKBggqhkjO\n"
        "PQQDAjAXMRUwEwYDVQQDDAx2cG4uZXhhbXBsZTAeFw0yNjAxMDEwMDAwMDBaFw0z\n"
        "-----END CERTIFICATE-----");

} // namespace

class TestHelperServerSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void appRulesAndTheLogLevelReachTheCoreInsideTheHelper();
    void theEndpointsTlsSettingsReachTheCoreInsideTheHelper_data();
    void theEndpointsTlsSettingsReachTheCoreInsideTheHelper();

private:
    QTemporaryDir m_dir;
};

void TestHelperServerSettings::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(QFile::exists(QStringLiteral(FT_TEST_HELPER_BINARY)),
             "helper binary was not built next to this test");
    QVERIFY2(realhelper::ensureWintunPlaceholder(), "could not place the wintun.dll placeholder");
}

// Per-application rules and the log level cross the privilege boundary the same
// way the kill switch does, and fail the same quiet way: the helper reads
// c.value("rules").toArray() and c.value("level").toString(), so a key renamed on
// either side arrives as no rules and an empty level, with nothing reporting it.
// No rules in "Through VPN" sends nothing through the tunnel while the window
// says Connected — the very leak selectiveModeWouldLeak exists to prevent.
//
// The core inside the helper is asked about one connection this test owns: the
// helper's answer is the decision line the wrapper logs for it, which arrives
// here as connection info. The level is read off the config the core was built
// with.
void TestHelperServerSettings::appRulesAndTheLogLevelReachTheCoreInsideTheHelper()
{
    QTcpServer ownSocket; // the connection the core will ask about is this one
    QVERIFY(ownSocket.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    const QString token = QStringLiteral("token-for-app-rules");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token, realhelper::envWith(QStringLiteral("FT_TEST_CORE_PROBE_TCP_PORT"),
                                                    QString::number(ownSocket.serverPort()))));
    const auto pointed = realhelper::pointClientsAt(helper.port(), token);

    const QString self = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    VpnHelperClient client;
    QSignalSpy info(&client, &VpnHelperClient::connectionInfo);
    // As Backend does: set before the handshake, pushed when the helper is ready.
    client.setVpnMode(false); // bypass: a listed program leaves the tunnel
    client.setAppRules(std::vector<std::string>{self.toStdString()});
    client.setLogLevel(QStringLiteral("debug"));
    // No loglevel in the file, so the level the core gets is the one sent.
    client.loadConfigFromToml(realhelper::minimalConfigToml());
    client.connectVpn();

    const auto decided = [&info, &self]() {
        for (const QList<QVariant> &args : info) {
            const QString line = args.at(0).toString();
            if (line.contains(self) && line.contains(QStringLiteral("bypass")))
                return true;
        }
        return false;
    };
    QTRY_VERIFY2_WITH_TIMEOUT(decided(),
                              "the helper's core never sent this program out of the tunnel: "
                              "the app rule did not reach it",
                              20000);

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(helper.coreConfigDump()), 20000);
    // 3 is ag::LOG_LEVEL_DEBUG in tests/mock_core; an empty level reads as info.
    QCOMPARE(dumpValue(helper.coreConfigDump(), QStringLiteral("loglevel")), QStringLiteral("3"));
}

void TestHelperServerSettings::theEndpointsTlsSettingsReachTheCoreInsideTheHelper_data()
{
    QTest::addColumn<bool>("skipVerification");
    QTest::newRow("pinned") << false;
    QTest::newRow("verification skipped") << true;
}

// Whom the tunnel trusts is decided inside the core, from three [endpoint] keys
// the app writes: a pinned certificate, skip_verification, and client_random
// with its mask. A config is written by the app's own TOML writer, sent by the
// real GUI client, and parsed by the real helper; what the core was built with
// must be what the config said. A pin lost on the way leaves the core checking
// the server against the system's CAs instead, and a mask lost leaves the TLS
// hello without the shape the user asked for, and nothing anywhere says so.
void TestHelperServerSettings::theEndpointsTlsSettingsReachTheCoreInsideTheHelper()
{
    QFETCH(bool, skipVerification);
    freetunnel::ConfigToml config;
    config.hostname = QStringLiteral("vpn.example");
    config.addresses = QStringLiteral("203.0.113.7:443");
    config.username = QStringLiteral("user");
    config.password = QStringLiteral("password");
    config.clientRandom = QStringLiteral("a1b2c3/ff00ff");
    config.certificate = kPinnedPem;
    config.skipVerification = skipVerification;

    const QString token = QStringLiteral("token-for-endpoint-tls");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token));
    const auto pointed = realhelper::pointClientsAt(helper.port(), token);
    VpnHelperClient client;
    QVERIFY(client.loadConfigFromToml(freetunnel::buildConfigToml(config)));
    client.connectVpn();

    const QString dump = helper.coreConfigDump();
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dump), 20000);
    QCOMPARE(dumpValue(dump, QStringLiteral("client_random")), QStringLiteral("a1b2c3"));
    QCOMPARE(dumpValue(dump, QStringLiteral("client_random_mask")), QStringLiteral("ff00ff"));
    QCOMPARE(dumpValue(dump, QStringLiteral("skip_verification")),
             skipVerification ? QStringLiteral("1") : QStringLiteral("0"));
    // The core loads a pin only when it verifies at all. The dump keeps a record
    // to a line, so the PEM's line breaks read as spaces there; the one after its
    // last line, which TOML keeps before the closing quotes, is no part of it.
    QCOMPARE(dumpValue(dump, QStringLiteral("certificate")).trimmed(),
             skipVerification ? QString() : QString(kPinnedPem).replace(QLatin1Char('\n'), QLatin1Char(' ')));
}

QTEST_MAIN(TestHelperServerSettings)
#include "test_helper_server_settings.moc"
