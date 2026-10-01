// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTranslator>
#include <QScopeGuard>

#include "app/Backend.h"
#include "app/LogModel.h"
#include "core/AppSettings.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/ControlCommand.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "helper_ipc_mock_server.h"

class TestIntegrationBackendVpn : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void backendConnectsThroughMockHelper();
    void connectPushesTheSecuritySettingsToTheHelper();
    void connectWithNoRulesAsksForTheFullTunnelNotSelective();
    void togglingTheKillSwitchWhileConnectedReachesTheCore();
    void theConfigsKillSwitchPortsSettingReachesTheHelperAndTheLiveSession();
    void configSwitchSuppressesCoreDisconnectToast();
    void aSwitchToAFailingServerSaysSoAndTheNextPickStillSwitches();
    void aSecondPickWhileTheFirstReadsItsPasswordWins();
    void anEditMadeWhileConnectingReachesTheSession();
    void aRuleEditReachesTheRunningSessionWithoutARebuild();
    void aModeChangeReachesTheRunningSessionWithItsRules();
    void anExcludedRouteEditRebuildsTheSession();
    void anEarlierRebuildsSafetyNetLeavesTheNextOneAlone();
    void savingAFixedPasswordWhileConnectingStartsAgain();
    void aConnectRefusedWithoutAStateEndsConnecting();
    void aFailureBeforeTheSessionCameUpIsSaidInTheUsersTerms_data();
    void aFailureBeforeTheSessionCameUpIsSaidInTheUsersTerms();
    void aBackendDestroyedMidSessionWithoutPrepareQuitIsSafe();
    void theHelperGoingAwayWhileConnectedLeavesTheWindowOff();
    void aMessageTheHelperWordsIsShownInTheUsersLanguage_data();
    void aMessageTheHelperWordsIsShownInTheUsersLanguage();
    void aChangeWhileTheHelperStartsDoesNotStartItAgain();
    void deletingActiveConfigWhileConnectedTearsDownTunnel();
    void aDisconnectAtLaunchWinsOverConnectOnStartup();
    void aLinkAsksBeforeTurningTheVpnOffACommandDoesNot();
    void exportRoundTrips();
    void domainRulesAcceptTldWildcardsAndIdn();
};

static AppSettings hermeticSettings(const QString &logPath)
{
    AppSettings settings;
    settings.hotkeys_enabled = false;
    settings.auto_connect_on_start = false;
    settings.killswitch_enabled = false;
    settings.domain_bypass_enabled = false;
    settings.domain_bypass_rules.clear();
    settings.profiles.clear();
    settings.profiles.insert(QStringLiteral("Default"), {});
    settings.profile_order = {QStringLiteral("Default")};
    settings.active_profile = QStringLiteral("Default");
    settings.log_path = logPath;
    settings.excluded_routes = defaultExcludedRoutes();
    return settings;
}

void TestIntegrationBackendVpn::initTestCase()
{
    QSettings().clear();
    saveAppSettings(hermeticSettings(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/backend-vpn-test.log")));
}

void TestIntegrationBackendVpn::init()
{
    freetunnel::sweepStaleMaterializedConfigs();
    saveStoredConfigs({});
}

void TestIntegrationBackendVpn::backendConnectsThroughMockHelper()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);

    QTemporaryFile configFile(QDir(base).filePath(QStringLiteral("backend-vpn-XXXXXX.toml")));
    configFile.setAutoRemove(true);
    QVERIFY(configFile.open());

    freetunnel::ConfigToml cfg;
    cfg.hostname = QStringLiteral("backend.example.com");
    cfg.addresses = QStringLiteral("203.0.113.50:443");
    cfg.username = QStringLiteral("user");
    configFile.write(freetunnel::buildConfigToml(cfg).toUtf8());
    configFile.close();
    const QString configPath = configFile.fileName();

    QVERIFY(freetunnel::CredentialStore::storePassword(configPath, QStringLiteral("secret")));

    saveStoredConfigs({configPath});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configPath;
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-integration-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    QSignalSpy stateSpy(&backend, &Backend::stateChanged);

    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QVERIFY(stateSpy.count() > 0);

    // Regression: connect must not write password-bearing .connect-*.toml in the GUI
    // process — config is sent in-memory to the helper over loopback IPC.
    auto connectTemps = [&]() {
        return QDir(base).entryList({QStringLiteral(".connect-*.toml")},
                                    QDir::Files | QDir::Hidden).size();
    };
    QVERIFY(connectTemps() == 0);

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    QVERIFY(connectTemps() == 0);

    backend.prepareQuit();
    QVERIFY(connectTemps() == 0);
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");

    // The keychain service name is hardcoded and not affected by test mode, so
    // drop the entry we created to avoid leaving it in the real OS credential store.
    freetunnel::CredentialStore::deletePassword(configPath);
}

static bool writeTestConfig(const QString &base, const QString &hostname, QString *outPath);

// The first link of the kill-switch chain: Backend must push the PERSISTED
// settings to the helper on every connect. Deleting that push leaves the tunnel
// coming up perfectly — connected, no error, session timer running — with the
// kill switch simply never armed and split tunnelling never applied, which no
// other test notices because they all assert on connectivity.
void TestIntegrationBackendVpn::connectPushesTheSecuritySettingsToTheHelper()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString configPath;
    QVERIFY(writeTestConfig(base, QStringLiteral("kill.example"), &configPath));
    QVERIFY(freetunnel::CredentialStore::storePassword(configPath, QStringLiteral("secret")));

    saveStoredConfigs({configPath});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configPath;
    settings.killswitch_enabled = true;
    settings.domain_bypass_enabled = true;
    settings.vpn_mode = QStringLiteral("selective");
    settings.profiles[QStringLiteral("Default")] = {QStringLiteral("example.com")};
    settings.domain_bypass_rules = settings.profiles.value(QStringLiteral("Default"));
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-settings-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));

    // The VALUE, not merely the command name: a key read under the wrong spelling
    // decodes to false through QJsonValue::toBool() with nothing failing anywhere.
    const QJsonObject killSwitch = server.lastMessageFor(QStringLiteral("setKillSwitch"));
    QVERIFY2(!killSwitch.isEmpty(), "connect never sent setKillSwitch to the helper");
    QVERIFY2(killSwitch.contains(QStringLiteral("enabled")),
             "setKillSwitch carried no 'enabled' key — a renamed key decodes to false silently");
    QCOMPARE(killSwitch.value(QStringLiteral("enabled")).toBool(), true);

    // Selective mode is only requested when the rule list is non-empty; this
    // profile has one, so it must arrive as on.
    const QJsonObject mode = server.lastMessageFor(QStringLiteral("setMode"));
    QVERIFY2(!mode.isEmpty(), "connect never sent setMode to the helper");
    QCOMPARE(mode.value(QStringLiteral("selective")).toBool(), true);

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
    freetunnel::CredentialStore::deletePassword(configPath);
}

static bool writeTestConfig(const QString &base, const QString &hostname, QString *outPath)
{
    QTemporaryFile configFile(QDir(base).filePath(hostname + QStringLiteral("-XXXXXX.toml")));
    configFile.setAutoRemove(false);
    if (!configFile.open())
        return false;

    freetunnel::ConfigToml cfg;
    cfg.hostname = hostname;
    cfg.addresses = QStringLiteral("203.0.113.50:443");
    cfg.username = QStringLiteral("user");
    configFile.write(freetunnel::buildConfigToml(cfg).toUtf8());
    configFile.close();
    if (!freetunnel::CredentialStore::storePassword(configFile.fileName(), QStringLiteral("secret")))
        return false;
    *outPath = configFile.fileName();
    return true;
}

// The sibling above proves selective mode is requested when a rule exists. This
// is the other half, and it is the half that leaks: "selective" routes ONLY the
// listed rules through the tunnel, so an empty list routes nothing and every
// byte leaves in the clear while the UI still says Connected.
//
// backend_split already checks selectiveModeActive() and selectiveModeWouldLeak(),
// but those are what the Backend *thinks*. Nothing checked what the core is
// actually told, and the wiring between the two is where this can break: a
// mutation that left both predicates correct and passed domain_bypass_enabled
// straight to setVpnMode() went unnoticed by the whole suite.
void TestIntegrationBackendVpn::connectWithNoRulesAsksForTheFullTunnelNotSelective()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString configPath;
    QVERIFY(writeTestConfig(base, QStringLiteral("norules.example"), &configPath));
    QVERIFY(freetunnel::CredentialStore::storePassword(configPath, QStringLiteral("secret")));

    saveStoredConfigs({configPath});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configPath;
    settings.domain_bypass_enabled = true;
    settings.vpn_mode = QStringLiteral("selective");
    // The reachable misconfiguration: a fresh install, "Clear all", or a newly
    // added profile all leave the active rule list empty.
    settings.profiles[QStringLiteral("Default")] = {};
    settings.domain_bypass_rules = {};
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-norules-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));

    const QJsonObject mode = server.lastMessageFor(QStringLiteral("setMode"));
    QVERIFY2(!mode.isEmpty(), "connect never sent setMode to the helper");
    QVERIFY2(!mode.value(QStringLiteral("selective")).toBool(),
             "selective mode with an empty rule list must NOT reach the core - it would "
             "route nothing through the tunnel while the UI says Connected");

    // The user's chosen setting is not silently rewritten; only what the core is
    // told differs.
    QCOMPARE(backend.vpnMode(), QStringLiteral("selective"));

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

// Everything the suite checked about the kill switch was checked at connect
// time. Nothing covered the toggle itself, so deleting the m_client.setKillSwitch(v)
// line from Backend::setKillSwitch left the whole suite green: the setting would
// persist, the GUI would read ON, and the running tunnel would never hear about
// it until the next connect. That is the worst shape for this particular switch —
// the user believes traffic is blocked on drop, and it is not.
void TestIntegrationBackendVpn::togglingTheKillSwitchWhileConnectedReachesTheCore()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString configPath;
    QVERIFY(writeTestConfig(base, QStringLiteral("killtoggle.example"), &configPath));
    QVERIFY(freetunnel::CredentialStore::storePassword(configPath, QStringLiteral("secret")));

    saveStoredConfigs({configPath});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configPath;
    settings.killswitch_enabled = false; // start off, so the toggle has somewhere to go
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-killtoggle-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(server.lastMessageFor(QStringLiteral("setKillSwitch"))
                     .value(QStringLiteral("enabled")).toBool(), false);

    backend.setKillSwitch(true);
    QVERIFY2(QTest::qWaitFor(
                     [&]() {
                         return server.lastMessageFor(QStringLiteral("setKillSwitch"))
                                 .value(QStringLiteral("enabled")).toBool();
                     },
                     5000),
             "flipping the kill switch on a live connection must reach the core, not just "
             "the settings file");
    QCOMPARE(backend.killSwitch(), true);
    // And reach it in force. The core reads the kill switch when a session is
    // built, so the running one has to be rebuilt; the message alone leaves the
    // session as it was. A rebuild is skipped only when nothing the session was
    // built with changed, and the kill switch is one of those things.
    QVERIFY2(QTest::qWaitFor([&]() { return server.connectCount() == 2 && backend.connected(); },
                             10000),
             "the kill switch is read when a session is built, so turning it on while "
             "connected has to rebuild the session");
    backend.setKillSwitch(false);
    QVERIFY2(QTest::qWaitFor([&]() { return server.connectCount() == 3 && backend.connected(); },
                             10000),
             "turning the kill switch off while connected has to rebuild the session too");

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

// The setting that lets a config's kill-switch ports through goes the kill
// switch's way: pushed from the persisted settings on connect, and, changed on a
// live connection, sent and applied by rebuilding the session, since the ports
// are fixed when the tunnel is. A toggle that only reached the settings file
// would leave Remote Desktop blocked, or open, until the next manual reconnect.
void TestIntegrationBackendVpn::theConfigsKillSwitchPortsSettingReachesTheHelperAndTheLiveSession()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString configPath;
    QVERIFY(writeTestConfig(base, QStringLiteral("killports.example"), &configPath));
    QVERIFY(freetunnel::CredentialStore::storePassword(configPath, QStringLiteral("secret")));

    saveStoredConfigs({configPath});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configPath;
    settings.killswitch_enabled = true;
    settings.killswitch_ports_from_config = true;
    saveAppSettings(settings);
    const auto restore = qScopeGuard([] {
        AppSettings s = loadAppSettings();
        s.killswitch_ports_from_config = false;
        saveAppSettings(s);
    });

    const QString token = QStringLiteral("backend-killports-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    const QJsonObject sent = server.lastMessageFor(QStringLiteral("setKillSwitchPortsFromConfig"));
    QVERIFY2(sent.contains(QStringLiteral("enabled")),
             "connect never told the helper whether the config's ports apply");
    QCOMPARE(sent.value(QStringLiteral("enabled")).toBool(), true);
    const int connectsBefore = server.connectCount();

    backend.setKillSwitchPortsFromConfig(false);
    QVERIFY2(QTest::qWaitFor(
                     [&]() {
                         return !server.lastMessageFor(QStringLiteral("setKillSwitchPortsFromConfig"))
                                         .value(QStringLiteral("enabled")).toBool()
                                 && server.connectCount() > connectsBefore
                                 && backend.connected();
                     },
                     10000),
             "turning the setting off on a live connection must reach the helper and rebuild "
             "the session, not just the settings file");
    QCOMPARE(backend.killSwitchPortsFromConfig(), false);

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

void TestIntegrationBackendVpn::configSwitchSuppressesCoreDisconnectToast()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);

    QString configA, configB;
    QVERIFY(writeTestConfig(base, QStringLiteral("server-a"), &configA));
    QVERIFY(writeTestConfig(base, QStringLiteral("server-b"), &configB));
    saveStoredConfigs({configA, configB});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configA;
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-switch-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    QSignalSpy errorSpy(&backend, &Backend::errorOccurred);

    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(backend.activeIndex(), 0);

    // The old session reporting its own end while the switch tears it down.
    server.setTeardownError(QStringLiteral("core disconnected"));
    backend.selectConfig(1);
    // Until the old session reports going down, connected() is still A's: wait
    // for the second connect, or the check below runs before the switch has.
    QVERIFY(QTest::qWaitFor([&]() { return server.connectCount() == 2 && backend.connected(); }, 10000));
    QCOMPARE(backend.activeIndex(), 1);

    // A QSignalSpy row is the argument LIST, not the argument. Binding it to a
    // const QVariant& built a temporary QVariant wrapping the whole list, so this
    // loop used to compare against something like "QVariantList(...)" and could
    // never see the message it names — the check passed no matter what was emitted.
    for (const QList<QVariant> &row : errorSpy) {
        QVERIFY(!row.isEmpty());
        const QString text = row.at(0).toString();
        const QString msg = text.toLower();
        QVERIFY2(!msg.contains(QStringLiteral("connection lost")),
                 qPrintable(QStringLiteral("unexpected toast: ") + text));
    }

    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
    freetunnel::CredentialStore::deletePassword(configA);
    freetunnel::CredentialStore::deletePassword(configB);
    QFile::remove(configA);
    QFile::remove(configB);
}

namespace {

// Points the Backend at a mock helper for one test, and puts the environment
// back however the test ends. A QVERIFY that returns early otherwise leaves the
// next test talking to a port nothing listens on.
struct MockHelperEnv {
    explicit MockHelperEnv(const QString &token) : token(token), server(token) {}
    ~MockHelperEnv()
    {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
    }
    MockHelperEnv(const MockHelperEnv &) = delete;
    MockHelperEnv &operator=(const MockHelperEnv &) = delete;

    bool start()
    {
        if (!server.listen())
            return false;
        qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
        qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());
        return true;
    }
    QString lastConnectConfig() const
    {
        return server.lastMessageFor(QStringLiteral("connect"))
                .value(QStringLiteral("configToml"))
                .toString();
    }

    QString token;
    MockHelperServer server;
};

// Config files and their keychain entries, removed however the test ends.
struct TestConfigs {
    ~TestConfigs()
    {
        for (const QString &path : std::as_const(paths)) {
            freetunnel::CredentialStore::deletePassword(path);
            QFile::remove(path);
        }
    }
    bool add(const QString &hostname)
    {
        QString path;
        if (!writeTestConfig(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
                             hostname, &path))
            return false;
        paths << path;
        return true;
    }
    // Store the list and make the first one active, on settings no earlier test
    // has left its rules or modes in.
    void install() const
    {
        saveStoredConfigs(paths);
        AppSettings settings = hermeticSettings(loadAppSettings().log_path);
        settings.last_config_path = paths.value(0);
        saveAppSettings(settings);
    }

    QStringList paths;
};

bool anyErrorIs(const QSignalSpy &errors, const QString &text)
{
    return std::any_of(errors.cbegin(), errors.cend(), [&text](const QList<QVariant> &row) {
        return !row.isEmpty() && row.at(0).toString() == text;
    });
}

} // namespace

// A switch is a teardown and then a fresh connect, and only the teardown is the
// old session's. The guard that hid its errors used to stay up until the new
// session reached Connected, so a switch to a server that was down showed no
// error at all, sat on "Connecting…" for as long as the core retried, and made
// every later pick a no-op: the next config was highlighted while the helper
// kept retrying the dead one.
void TestIntegrationBackendVpn::aSwitchToAFailingServerSaysSoAndTheNextPickStillSwitches()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("switchfail-a")));
    QVERIFY(configs.add(QStringLiteral("switchfail-down")));
    QVERIFY(configs.add(QStringLiteral("switchfail-c")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-switchfail-token"));
    QVERIFY(env.start());
    env.server.failConnectsWith(QStringLiteral("switchfail-down"),
                                QStringLiteral("Connection failed: endpoint timed out (~30s)"));

    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));

    backend.selectConfig(1);
    QVERIFY2(QTest::qWaitFor(
                     [&]() { return anyErrorIs(errors, QStringLiteral("Server isn't responding (timed out).")); },
                     5000),
             "a switch to a server that does not answer has to say so");
    QVERIFY(backend.connecting()); // the core is still retrying, and the UI says it is

    backend.selectConfig(2);
    QVERIFY2(QTest::qWaitFor([&]() { return backend.connected(); }, 10000),
             "the pick after a failing switch was ignored");
    QCOMPARE(backend.activeIndex(), 2);
    QVERIFY2(env.lastConnectConfig().contains(QStringLiteral("\"switchfail-c\"")),
             "the tunnel has to be on the config the user picked last");

    backend.prepareQuit();
}

// The second half of a switch starts by reading the new config's password on a
// worker thread. A pick made while that read was in flight was dropped, because
// the switch was still "reapplying": the tunnel came up on the first pick while
// the list, the Home label and the tray all named the second.
void TestIntegrationBackendVpn::aSecondPickWhileTheFirstReadsItsPasswordWins()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("repick-a")));
    QVERIFY(configs.add(QStringLiteral("repick-b")));
    QVERIFY(configs.add(QStringLiteral("repick-c")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-repick-token"));
    QVERIFY(env.start());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(env.server.connectCount(), 1);

    // The connect to B is logged just before its password read starts. A queued
    // pick posted from there runs once the read is under way, and before its
    // result, which the worker thread can only post later.
    auto *logs = qobject_cast<QAbstractItemModel *>(backend.logModel());
    QVERIFY(logs);
    bool picked = false;
    QObject::connect(logs, &QAbstractItemModel::rowsInserted, &backend,
                     [&](const QModelIndex &, int, int last) {
                         const QString line = logs->index(last, 0).data(LogModel::MsgRole).toString();
                         if (picked || !line.contains(QStringLiteral("repick-b")))
                             return;
                         picked = true;
                         QMetaObject::invokeMethod(
                                 &backend, [&backend]() { backend.selectConfig(2); },
                                 Qt::QueuedConnection);
                     });

    backend.selectConfig(1);
    QVERIFY(QTest::qWaitFor([&]() { return picked; }, 5000));
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(backend.activeIndex(), 2);
    QVERIFY2(env.lastConnectConfig().contains(QStringLiteral("\"repick-c\"")),
             "the tunnel came up on the first pick, not the one the user made last");
    QCOMPARE(env.server.connectCount(), 2); // B never reached the helper at all

    backend.prepareQuit();
}

// The excluded routes and the kill switch go out with a connect, and a session
// that is already connecting or retrying keeps the copy it was given. An edit
// made then waited for a Connected that a failing server never reaches, so the
// toggle showed the new setting while the attempt kept the old one.
void TestIntegrationBackendVpn::anEditMadeWhileConnectingReachesTheSession()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("editwhile-down")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-editwhile-token"));
    QVERIFY(env.start());
    env.server.failConnectsWith(QStringLiteral("editwhile-down"),
                                QStringLiteral("Connection failed: endpoint timed out (~30s)"));

    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor(
            [&]() { return anyErrorIs(errors, QStringLiteral("Server isn't responding (timed out).")); },
            10000));
    QVERIFY(backend.connecting());
    QCOMPARE(env.server.connectCount(), 1);

    backend.setKillSwitch(!backend.killSwitch());
    QVERIFY2(QTest::qWaitFor([&]() { return env.server.connectCount() == 2; }, 5000),
             "an edit made while connecting has to restart the attempt with it");
    QVERIFY(backend.connecting());

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
}

// Split-tunnelling rules reach a running session as they change: the helper
// reads program rules on every connection, and hands address rules and the mode
// to the session itself (vendor patch 03). Each edit used to rebuild the tunnel
// as well: every open connection dropped and, with the kill switch on, nothing
// was blocked while the new session came up — for a change that needed neither.
void TestIntegrationBackendVpn::aRuleEditReachesTheRunningSessionWithoutARebuild()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("apprule-live")));
    configs.install();
    AppSettings settings = loadAppSettings();
    settings.killswitch_enabled = true;
    settings.domain_bypass_enabled = true;
    settings.vpn_mode = QStringLiteral("general");
    saveAppSettings(settings);

    MockHelperEnv env(QStringLiteral("backend-apprule-token"));
    QVERIFY(env.start());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(env.server.connectCount(), 1);

    const auto sent = [&env](const char *key) {
        return env.server.lastMessageFor(QStringLiteral("setSplitRouting"))
                .value(QLatin1String(key))
                .toArray();
    };
    QVERIFY(backend.addAppRule(QStringLiteral("firefox")));
    QVERIFY(QTest::qWaitFor(
            [&]() { return sent("rules").contains(QJsonValue(QStringLiteral("firefox"))); }, 5000));
    backend.removeAppRule(0);
    QVERIFY(QTest::qWaitFor([&]() { return sent("rules").isEmpty(); }, 5000));
    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    QVERIFY(QTest::qWaitFor(
            [&]() { return sent("domains").contains(QJsonValue(QStringLiteral("example.com"))); },
            5000));
    QTest::qWait(200); // a disconnect would follow the rules in the same burst
    QVERIFY2(env.server.lastMessageFor(QStringLiteral("disconnect")).isEmpty(),
             "a split-tunnelling rule tore the running tunnel down");
    QCOMPARE(env.server.connectCount(), 1);
    QVERIFY(backend.connected());

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
}

// The mode reaches a running session too, and always in the same command as the
// rules it is read with. In "Through VPN" with no address rules, the first
// program rule takes the core from the full tunnel to selective and removing the
// last takes it back; sent apart from the rule, the mode would route the session
// for a moment by the new mode with the old list. That way back is the one that
// leaks: a session left selective with nothing listed sends every connection
// around the tunnel while the window says Connected. The Mode switch and split
// tunnelling's own switch go the same way, and none of these builds a new session.
void TestIntegrationBackendVpn::aModeChangeReachesTheRunningSessionWithItsRules()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("apprule-mode")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-apprule-mode-token"));
    QVERIFY(env.start());

    Backend backend;
    // Set as the Split page sets them, so whatever decides the mode decides it here.
    backend.setSplitEnabled(true);
    backend.setVpnMode(QStringLiteral("selective"));
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    // Nothing listed yet, so the full tunnel; told with the rest when the helper
    // came up, before there was a session.
    QVERIFY(!env.server.lastMessageFor(QStringLiteral("setMode"))
                     .value(QStringLiteral("selective"))
                     .toBool());
    const int modeCommands = env.server.countFor(QStringLiteral("setMode"));
    const int domainCommands = env.server.countFor(QStringLiteral("setExclusions"));
    const int programCommands = env.server.countFor(QStringLiteral("setAppRules"));

    // One edit, one command, carrying the mode and both lists as they now are.
    const auto sentIs = [&env](bool selective, const QStringList &domains,
                               const QStringList &rules) {
        const QJsonObject m = env.server.lastMessageFor(QStringLiteral("setSplitRouting"));
        return !m.isEmpty() && m.value(QStringLiteral("selective")).toBool() == selective
                && m.value(QStringLiteral("domains")).toVariant().toStringList() == domains
                && m.value(QStringLiteral("rules")).toVariant().toStringList() == rules;
    };
    const QStringList none;
    const QStringList firefox{QStringLiteral("firefox")};
    const QStringList example{QStringLiteral("example.com")};

    QVERIFY(backend.addAppRule(QStringLiteral("firefox")));
    QVERIFY2(QTest::qWaitFor([&]() { return sentIs(true, none, firefox); }, 5000),
             "the first program rule in Through VPN has to make the session selective with it");
    backend.removeAppRule(0);
    QVERIFY2(QTest::qWaitFor([&]() { return sentIs(false, none, none); }, 5000),
             "removing the last program rule left the session selective with nothing listed");
    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    QVERIFY(QTest::qWaitFor([&]() { return sentIs(true, example, none); }, 5000));
    backend.setVpnMode(QStringLiteral("general"));
    QVERIFY(QTest::qWaitFor([&]() { return sentIs(false, example, none); }, 5000));
    backend.setSplitEnabled(false);
    QVERIFY(QTest::qWaitFor([&]() { return sentIs(false, none, none); }, 5000));

    QTest::qWait(200); // a disconnect would follow the last edit in the same burst
    QCOMPARE(env.server.countFor(QStringLiteral("setSplitRouting")), 5);
    QCOMPARE(env.server.countFor(QStringLiteral("setMode")), modeCommands);
    QCOMPARE(env.server.countFor(QStringLiteral("setExclusions")), domainCommands);
    QCOMPARE(env.server.countFor(QStringLiteral("setAppRules")), programCommands);
    QVERIFY2(env.server.lastMessageFor(QStringLiteral("disconnect")).isEmpty(),
             "a mode change tore the running tunnel down");
    QCOMPARE(env.server.connectCount(), 1);
    QVERIFY(backend.connected());

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
}

// Excluded routes go into the system's routing when a session is built, so an
// edit to them needs a new session as much as a domain rule does: a route added
// or removed would otherwise stay out of the tunnel's routing until a reconnect.
void TestIntegrationBackendVpn::anExcludedRouteEditRebuildsTheSession()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("route-edit")));
    configs.install(); // split tunnelling off: the routes are the only thing that changes

    MockHelperEnv env(QStringLiteral("backend-route-edit-token"));
    QVERIFY(env.start());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    const auto sentRoutes = [&env]() {
        return env.server.lastMessageFor(QStringLiteral("setRoutes"))
                .value(QStringLiteral("excluded"))
                .toArray();
    };
    const QJsonValue route(QStringLiteral("198.51.100.0/24"));

    QVERIFY(backend.addExcludedRoute(route.toString()));
    QVERIFY2(QTest::qWaitFor([&]() { return env.server.connectCount() == 2 && backend.connected(); },
                             10000),
             "an added route has to rebuild the session to reach it");
    QVERIFY(sentRoutes().contains(route));

    backend.removeExcludedRoute(static_cast<int>(backend.excludedRoutes().indexOf(route.toString())));
    QVERIFY2(QTest::qWaitFor([&]() { return env.server.connectCount() == 3 && backend.connected(); },
                             10000),
             "a removed route has to rebuild the session to leave it");
    QVERIFY(!sentRoutes().contains(route));

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
}

// Each rebuild arms a five-second net in case the old session's Disconnected
// never arrives. Nothing disarmed it, so an earlier rebuild's net, still counting
// down, found the flag a later rebuild had set and connected while that later
// teardown was still under way. The old session's Disconnected then landed on a
// switch that was no longer marked as one, and the UI read "Off" mid-switch.
void TestIntegrationBackendVpn::anEarlierRebuildsSafetyNetLeavesTheNextOneAlone()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("safetynet")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-safetynet-token"));
    QVERIFY(env.start());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));

    QElapsedTimer sinceFirst;
    sinceFirst.start();
    backend.setKillSwitch(!backend.killSwitch()); // the first rebuild, answered at once
    QVERIFY(QTest::qWaitFor([&]() { return env.server.connectCount() == 2 && backend.connected(); },
                            5000));
    // Three seconds into the first net, so it runs out while the second rebuild's
    // teardown is held, and three seconds before that rebuild's own net does.
    QTest::qWait(3000);

    bool readOff = false;
    bool released = false;
    bool oldSessionEnded = false;
    QObject::connect(&backend, &Backend::stateChanged, &backend, [&]() {
        if (released && !backend.connected())
            oldSessionEnded = true;
        if (!backend.connected() && !backend.connecting() && !backend.disconnecting())
            readOff = true;
    });
    // The two nets are told apart by WHEN a connect attempt starts during the held
    // teardown, not by whether one has by some fixed time: a stalled runner can
    // reach any fixed time after the second net's own attempt, which that net is
    // there to make. Timed by the Backend's log line for the attempt, which a net
    // writes as it fires, rather than by when the mock gets round to reading it.
    auto *logs = qobject_cast<QAbstractItemModel *>(backend.logModel());
    QVERIFY(logs);
    qint64 secondAt = -1;
    qint64 attemptAt = -1;
    QObject::connect(logs, &QAbstractItemModel::rowsInserted, &backend,
                     [&](const QModelIndex &, int, int last) {
                         if (secondAt < 0 || released || attemptAt >= 0)
                             return;
                         const QString line = logs->index(last, 0).data(LogModel::MsgRole).toString();
                         if (line.contains(QStringLiteral("safetynet")))
                             attemptAt = sinceFirst.elapsed();
                     });
    env.server.holdNextDisconnect();
    secondAt = sinceFirst.elapsed();
    backend.setKillSwitch(!backend.killSwitch()); // the second, whose teardown is slow

    // Five-second timers are coarse and may run up to 5 % early, so the second
    // rebuild's own net cannot fire before this.
    const qint64 secondNetEarliest = secondAt + 4500;
    // Well past the first net, which runs out by 5250 ms, and short of the second.
    QVERIFY(QTest::qWaitFor([&]() { return sinceFirst.elapsed() > secondNetEarliest - 1500; }, 10000));
    // qWaitFor looks before it handles events, so after a stall a net that is due
    // already has not fired yet. Let it, so that it is counted here rather than
    // after the release.
    QCoreApplication::processEvents();
    QVERIFY2(attemptAt < 0 || attemptAt >= secondNetEarliest,
             qPrintable(QStringLiteral("the first rebuild's safety net started a connect %1 ms "
                                       "into the second teardown, before that teardown was done")
                                .arg(attemptAt - secondAt)));
    // Only a stalled runner gets here with an attempt made, and it is the second
    // net's. That one is meant to connect mid-teardown, and the "Off" the end of
    // the teardown then shows is not what this test is about.
    const bool secondNetFired = attemptAt >= 0;

    released = true;
    env.server.releaseDisconnect();
    QVERIFY(QTest::qWaitFor(
            [&]() { return oldSessionEnded && env.server.connectCount() == 3 && backend.connected(); },
            10000));
    const bool readOffMidRebuild = readOff;

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
    if (secondNetFired)
        QSKIP("The runner stalled past the second rebuild's own safety net, so whether the UI "
              "read Off mid-rebuild cannot be told here.");
    QVERIFY2(!readOffMidRebuild, "the UI read Off in the middle of a rebuild");
}

// A wrong password makes the core retry, with the config it was handed, for as
// long as the attempt lasts. Saving the corrected config reapplied only when
// connected, so the attempt went on failing with the old password and the user
// concluded that the new one was wrong too.
void TestIntegrationBackendVpn::savingAFixedPasswordWhileConnectingStartsAgain()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    saveStoredConfigs({});
    saveAppSettings(hermeticSettings(loadAppSettings().log_path));
    MockHelperEnv env(QStringLiteral("backend-fixpass-token"));
    QVERIFY(env.start());
    env.server.failConnectsWith(QStringLiteral("wrong-secret"),
                                QStringLiteral("Connection failed: denied (AUTH_REQUIRED)"));

    Backend backend;
    QVariantMap f;
    f[QStringLiteral("name")] = QStringLiteral("Fix Password");
    f[QStringLiteral("hostname")] = QStringLiteral("fixpass.example");
    f[QStringLiteral("addresses")] = QStringLiteral("203.0.113.9:443");
    f[QStringLiteral("username")] = QStringLiteral("user");
    f[QStringLiteral("password")] = QStringLiteral("wrong-secret");
    f[QStringLiteral("protocol")] = QStringLiteral("http2");
    QVERIFY(backend.createConfig(f));
    TestConfigs configs;
    configs.paths = loadStoredConfigs();
    QCOMPARE(configs.paths.size(), 1);

    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor(
            [&]() {
                return anyErrorIs(errors, QStringLiteral(
                        "Authentication failed — check the username and password."));
            },
            10000));
    QVERIFY(backend.connecting());

    f[QStringLiteral("password")] = QStringLiteral("right-secret");
    f[QStringLiteral("editIndex")] = 0;
    f[QStringLiteral("editPath")] = configs.paths.first();
    QVERIFY(backend.createConfig(f));
    QVERIFY2(QTest::qWaitFor([&]() { return backend.connected(); }, 10000),
             "saving the fixed password has to start the attempt again with it");
    QVERIFY(env.lastConnectConfig().contains(QStringLiteral("\"right-secret\"")));

    backend.prepareQuit();
}

// The core in Error, handed a config it cannot load, says so with an error and
// no state, because Error is where it already is. connectVpn()'s optimistic
// "Connecting…" had nothing to end it, so the logo pulsed until clicked, and the
// Connect hotkey and links were ignored meanwhile.
void TestIntegrationBackendVpn::aConnectRefusedWithoutAStateEndsConnecting()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("refused.example")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-refused-token"));
    QVERIFY(env.start());
    env.server.refuseConnectsWith(QStringLiteral("refused.example"),
                                  QStringLiteral("Invalid TrustTunnel config structure"));

    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor(
            [&]() { return anyErrorIs(errors, QStringLiteral("Invalid TrustTunnel config structure")); },
            10000));
    QVERIFY2(!backend.connecting(), "a refused connect has to leave \"Connecting…\"");
    QVERIFY(!backend.connected());

    // And the next Connect is not taken for one already in flight.
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return env.server.connectCount() == 2; }, 5000));

    backend.prepareQuit();
}

void TestIntegrationBackendVpn::aFailureBeforeTheSessionCameUpIsSaidInTheUsersTerms_data()
{
    QTest::addColumn<QString>("coreError");
    QTest::addColumn<QString>("shown");
    QTest::newRow("wrong login") << QStringLiteral("Connection failed: denied (AUTH_REQUIRED)")
                                 << QStringLiteral("Authentication failed — check the username and password.");
    QTest::newRow("no answer") << QStringLiteral("Connection failed: endpoint timed out (~30s)")
                               << QStringLiteral("Server isn't responding (timed out).");
    // Windows with no network at all, which is retried until there is one.
    QTest::newRow("no network")
            << QStringLiteral("set_system_dns() failed: Couldn't detect active network interface")
            << QStringLiteral("No network connection. FreeTunnel will connect as soon as there is one.");
    QTest::newRow("anything else keeps its reason")
            << QStringLiteral("Connection failed: tls handshake (CERTIFICATE_VERIFICATION_FAILED)")
            << QStringLiteral("Couldn't connect to the server: tls handshake "
                              "(CERTIFICATE_VERIFICATION_FAILED)");
}

// "Connection failed: …" is the most common error there is, and it used to be
// passed through as the core wrote it: in English in every language, with a raw
// error code, and without the friendly wording a wrong password gets later on.
void TestIntegrationBackendVpn::aFailureBeforeTheSessionCameUpIsSaidInTheUsersTerms()
{
    QFETCH(QString, coreError);
    QFETCH(QString, shown);
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("firstfail.example")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-firstfail-token"));
    QVERIFY(env.start());
    env.server.failConnectsWith(QStringLiteral("firstfail.example"), coreError);

    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connecting() && env.server.connectCount() == 1; },
                            10000));
    QVERIFY(QTest::qWaitFor([&]() { return !errors.isEmpty(); }, 10000));
    QCOMPARE(errors.constLast().at(0).toString(), shown);

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    backend.prepareQuit();
}

// The app always runs prepareQuit() first, but a test that stops at a failed
// check does not, and neither would any future owner that forgets. The helper
// client's own destructor then aborted the live socket and reported
// Disconnected into a Backend whose log was already destroyed: a crash that
// took the rest of the run with it and hid which check had failed.
void TestIntegrationBackendVpn::aBackendDestroyedMidSessionWithoutPrepareQuitIsSafe()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("teardown.example")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-teardown-token"));
    QVERIFY(env.start());
    {
        Backend backend;
        backend.connectVpn();
        QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    }
    QTest::qWait(50); // anything it posted on the way out is delivered here
}

// The elevated helper holds the session, and it can go away under a connected
// window: it crashes, or something kills it. The client sees its socket close
// and nothing else, so it is Backend that has to turn that into what the window
// shows. Nothing may be left saying Connected or Connecting over a tunnel that is
// gone, an edit that would rebuild a live session must not start a helper for a
// session there is not, and Connect has to work again, from a new handshake.
void TestIntegrationBackendVpn::theHelperGoingAwayWhileConnectedLeavesTheWindowOff()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("helpergone.example")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-helper-gone-token"));
    QVERIFY(env.start());
    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QVERIFY(!backend.sessionTime().isEmpty());
    const int connectionsBefore = env.server.connectionCount();

    QSignalSpy state(&backend, &Backend::stateChanged);
    env.server.dropClient();
    QVERIFY2(QTest::qWaitFor([&]() { return !backend.connected(); }, 5000),
             "the window still says Connected with the helper and its tunnel gone");
    QVERIFY(state.count() > 0);
    QVERIFY2(!backend.connecting(), "a helper that went away left the window Connecting");
    QVERIFY(!backend.disconnecting());
    QVERIFY(backend.sessionTime().isEmpty());

    // The kill switch is a setting a live session is rebuilt for; with none
    // there, changing it is a setting and nothing more.
    backend.setKillSwitch(!backend.killSwitch());
    QTest::qWait(300);
    QCOMPARE(env.server.connectionCount(), connectionsBefore);
    QVERIFY(!backend.connected() && !backend.connecting());

    backend.connectVpn();
    QVERIFY2(QTest::qWaitFor([&]() { return backend.connected(); }, 10000),
             "Connect did not work again after the helper went away");
    QCOMPARE(env.server.connectionCount(), connectionsBefore + 1);
    QCOMPARE(env.server.connectCount(), 2);
    backend.prepareQuit();
}

void TestIntegrationBackendVpn::aMessageTheHelperWordsIsShownInTheUsersLanguage_data()
{
    // What the helper sends, and the catalogue entry it should be shown as.
    QTest::addColumn<QString>("sent");
    QTest::addColumn<QString>("context");
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("value");
    const QString warning = QStringLiteral(
            "The connection is using an unusual number of system resources. The tunnel is being "
            "kept up because the kill switch is on — reconnect manually when convenient.");
    QTest::newRow("as worded") << warning << QStringLiteral("QObject") << warning << QString();
    const QString wintun = QStringLiteral("wintun.dll is missing next to FreeTunnel.exe (%1). "
                                          "Reinstall from the official installer.");
    const QString dir = QStringLiteral("C:/Program Files/FreeTunnel");
    QTest::newRow("with a path in it") << wintun.arg(dir) << QStringLiteral("QObject") << wintun << dir;
    const QString parse = QStringLiteral("Failed parsing config: %1");
    const QString reason = QStringLiteral("expected '='");
    QTest::newRow("from the core") << parse.arg(reason) << QStringLiteral("QtTrustTunnelClient") << parse
                                   << reason;
}

// The helper runs elevated, without the user's language, so what it words itself
// (the kill switch's resource warning, a missing wintun.dll, a config the core
// cannot read) arrived in English in a Russian window. The GUI's catalogue has
// those texts, the ones with a value filled in included.
void TestIntegrationBackendVpn::aMessageTheHelperWordsIsShownInTheUsersLanguage()
{
    QFETCH(QString, sent);
    QFETCH(QString, context);
    QFETCH(QString, source);
    QFETCH(QString, value);
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("helperwords.example")));
    configs.install();

    MockHelperEnv env(QStringLiteral("backend-helperwords-token"));
    QVERIFY(env.start());
    env.server.refuseConnectsWith(QStringLiteral("helperwords.example"), sent);

    QTranslator russian;
    QVERIFY(russian.load(QStringLiteral(":/i18n/freetunnel_ru.qm")));
    QCoreApplication::installTranslator(&russian);
    const auto remove = qScopeGuard([&russian] { QCoreApplication::removeTranslator(&russian); });
    QString shown = QCoreApplication::translate(context.toUtf8().constData(), source.toUtf8().constData());
    if (!value.isEmpty())
        shown = shown.arg(value);
    QVERIFY2(shown != sent, "the catalogue has no Russian for it");

    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !errors.isEmpty(); }, 10000));
    QCOMPARE(errors.constLast().at(0).toString(), shown);
    backend.prepareQuit();
}

// On a real system the helper starts behind a password prompt, and the connect
// waits in the client until it answers. Saving a changed config, or picking
// another, tore that start down and began a new one: the prompt closed and a
// second opened. A port that accepts and never answers stands in for the prompt.
void TestIntegrationBackendVpn::aChangeWhileTheHelperStartsDoesNotStartItAgain()
{
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost));
    QList<QTcpSocket *> held;
    QObject::connect(&silent, &QTcpServer::newConnection, &silent, [&]() {
        while (silent.hasPendingConnections())
            held << silent.nextPendingConnection();
    });
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(silent.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", "unused");
    qputenv("FT_TEST_HANDSHAKE_MS", "60000");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
        qunsetenv("FT_TEST_HANDSHAKE_MS");
    });
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    TestConfigs configs;
    QVERIFY(configs.add(QStringLiteral("starting-a")));
    QVERIFY(configs.add(QStringLiteral("starting-b")));
    configs.install();

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return held.size() == 1; }, 5000));
    QVERIFY(backend.connecting());

    QVariantMap edit = backend.configFields(0);
    edit[QStringLiteral("password")] = QStringLiteral("changed");
    edit[QStringLiteral("editIndex")] = 0;
    edit[QStringLiteral("editPath")] = configs.paths.first();
    QVERIFY(backend.createConfig(edit));
    backend.selectConfig(1);
    QTest::qWait(500);
    QCOMPARE(held.size(), 1); // the one start, still waiting
    QVERIFY(backend.connecting());
    backend.prepareQuit();
}

void TestIntegrationBackendVpn::deletingActiveConfigWhileConnectedTearsDownTunnel()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);

    QString configA, configB;
    QVERIFY(writeTestConfig(base, QStringLiteral("del-server-a"), &configA));
    QVERIFY(writeTestConfig(base, QStringLiteral("del-server-b"), &configB));
    saveStoredConfigs({configA, configB});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = configA;
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-delete-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(backend.activeIndex(), 0);

    // Deleting a NON-active config while connected must leave the tunnel up.
    backend.removeConfig(1);
    QTest::qWait(200);
    QVERIFY(backend.connected());
    QCOMPARE(backend.activeIndex(), 0); // configA still the active, still connected

    // Deleting the ACTIVE config must tear the tunnel down — not silently
    // relabel the "connected" state onto whatever config remains.
    backend.removeConfig(0);
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    QVERIFY(!backend.connected());

    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
    freetunnel::CredentialStore::deletePassword(configA);
    freetunnel::CredentialStore::deletePassword(configB);
    QFile::remove(configA);
    QFile::remove(configB);
}

// "Disconnect" handed over with the launch — a script or a Stream Deck running
// FreeTunnel with freetunnel://disconnect while it was not running — is acted on
// before "Connect on startup" has started anything. It found nothing to take
// down, and the auto-connect brought the tunnel up 600 ms later all the same.
void TestIntegrationBackendVpn::aDisconnectAtLaunchWinsOverConnectOnStartup()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString config;
    QVERIFY(writeTestConfig(base, QStringLiteral("autoconnect-server"), &config));
    saveStoredConfigs({config});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = config;
    settings.auto_connect_on_start = true;
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-autoconnect-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());
    auto cleanup = qScopeGuard([&] {
        AppSettings off = loadAppSettings();
        off.auto_connect_on_start = false;
        saveAppSettings(off);
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
        freetunnel::CredentialStore::deletePassword(config);
        QFile::remove(config);
    });

    // First that the setup does auto-connect: otherwise the second half would
    // pass for the wrong reason.
    {
        Backend backend;
        QVERIFY2(QTest::qWaitFor([&]() { return backend.connected(); }, 10000),
                 "connect on startup did not connect, so the check below proves nothing");
        backend.prepareQuit();
    }
    const int connectsBefore = server.connectCount();

    {
        Backend backend;
        backend.handleControl(QStringLiteral("freetunnel://disconnect"));
        QTest::qWait(1500); // well past the 600 ms the auto-connect waits
        QVERIFY2(!backend.connected() && !backend.connecting(),
                 "connect on startup overrode the disconnect given with the launch");
        QCOMPARE(server.connectCount(), connectsBefore);
        backend.prepareQuit();
    }

    // Opened as a link rather than run, the same disconnect would keep the VPN
    // off on a web page's say-so, so it is a question; until it is answered, the
    // connection on startup goes ahead. A helper of its own, as a new launch
    // has: the first Backend told the first one to quit.
    MockHelperServer second(token);
    QVERIFY(second.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(second.port()));
    Backend linked;
    QSignalSpy asked(&linked, &Backend::deepLinkDisconnectConfirmationRequired);
    QVERIFY(linked.handleControl(
            freetunnel::linkControlString(QStringLiteral("freetunnel://disconnect"))));
    QCOMPARE(asked.count(), 1);
    QVERIFY(QTest::qWaitFor([&]() { return linked.connected(); }, 10000));
    linked.prepareQuit();
}

// freetunnel:// links are registered with the system, so any web page can open
// one, and a disconnect or a toggle used to take the tunnel down on the spot.
// Opened as a link, anything that would turn the VPN off is now a question; the
// same URL run as a command, as a Stream Deck button does, acts at once.
void TestIntegrationBackendVpn::aLinkAsksBeforeTurningTheVpnOffACommandDoesNot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString config;
    QVERIFY(writeTestConfig(base, QStringLiteral("link-server"), &config));
    saveStoredConfigs({config});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = config;
    saveAppSettings(settings);

    const QString token = QStringLiteral("backend-link-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());
    auto cleanup = qScopeGuard([&] {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
        freetunnel::CredentialStore::deletePassword(config);
        QFile::remove(config);
    });
    const auto link = [](const char *url) {
        return freetunnel::linkControlString(QString::fromLatin1(url));
    };

    Backend backend;
    QSignalSpy asked(&backend, &Backend::deepLinkDisconnectConfirmationRequired);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));

    QVERIFY(backend.handleControl(link("freetunnel://disconnect")));
    QVERIFY(backend.handleControl(link("freetunnel://toggle")));
    QVERIFY(!backend.handleControl(link("freetunnel://connect")));
    QCOMPARE(asked.count(), 2);
    QTest::qWait(300);
    QVERIFY2(backend.connected() && !backend.disconnecting(),
             "a link turned the VPN off without asking");

    // The command someone runs is not asked about.
    QVERIFY(!backend.handleControl(QStringLiteral("freetunnel://disconnect")));
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    QCOMPARE(asked.count(), 2);

    // With the VPN off there is nothing to ask about: a link disconnect does
    // nothing, and a link toggle connects, as connecting is never asked about.
    QVERIFY(!backend.handleControl(link("freetunnel://disconnect")));
    QVERIFY(!backend.handleControl(link("freetunnel://toggle")));
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(asked.count(), 2);

    // While an attempt is still going, a link that would call it off is asked
    // about as well: calling it off takes down the kill switch armed for it.
    QVERIFY(!backend.handleControl(QStringLiteral("freetunnel://disconnect")));
    QVERIFY(QTest::qWaitFor(
            [&]() { return !backend.connected() && !backend.connecting() && !backend.disconnecting(); },
            5000));
    server.failConnectsWith(QStringLiteral("link-server"),
                            QStringLiteral("Connection failed: endpoint timed out (~30s)"));
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor(
            [&]() { return anyErrorIs(errors, QStringLiteral("Server isn't responding (timed out).")); },
            10000));
    QVERIFY(backend.connecting()); // the core keeps retrying
    QVERIFY(backend.handleControl(link("freetunnel://disconnect")));
    QVERIFY(backend.handleControl(link("freetunnel://toggle")));
    QCOMPARE(asked.count(), 4);
    QTest::qWait(300);
    QVERIFY2(backend.connecting() && !backend.disconnecting(),
             "a link called off a connection attempt without asking");
    backend.prepareQuit();
}

void TestIntegrationBackendVpn::exportRoundTrips()
{
    Backend backend;
    QVariantMap f;
    f[QStringLiteral("name")] = QStringLiteral("Export Test");
    f[QStringLiteral("hostname")] = QStringLiteral("vpn.export.test");
    f[QStringLiteral("addresses")] = QStringLiteral("203.0.113.7:443");
    f[QStringLiteral("username")] = QStringLiteral("exp-user");
    f[QStringLiteral("password")] = QStringLiteral("exp-pass");
    f[QStringLiteral("protocol")] = QStringLiteral("http2");
    QVERIFY(backend.createConfig(f));
    QCOMPARE(backend.configs().size(), 1);

    // Deep link round-trips through the parser with credentials intact.
    const QString link = backend.configDeepLink(0);
    QVERIFY(link.startsWith(QLatin1String("tt://")));
    QString err;
    const auto dl = freetunnel::parseDeepLink(link, &err);
    QVERIFY2(dl.has_value(), qPrintable(err));
    QCOMPARE(dl->hostname, QStringLiteral("vpn.export.test"));
    QVERIFY(dl->addresses.contains(QStringLiteral("203.0.113.7:443")));
    QCOMPARE(dl->username, QStringLiteral("exp-user"));
    QCOMPARE(dl->password, QStringLiteral("exp-pass"));

    // TOML export writes a usable file (password injected from the keychain).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("exported.toml"));
    QVERIFY(backend.exportConfigToml(0, out));
    QFile g(out);
    QVERIFY(g.open(QIODevice::ReadOnly | QIODevice::Text));
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(g.readAll()));
    QCOMPARE(c.hostname, QStringLiteral("vpn.export.test"));
    QCOMPARE(c.username, QStringLiteral("exp-user"));
    QCOMPARE(c.password, QStringLiteral("exp-pass"));

    // Clean up the keychain entry + file the config created.
    backend.prepareQuit();
    for (const QString &path : loadStoredConfigs()) {
        freetunnel::CredentialStore::deletePassword(
                freetunnel::CredentialStore::keyForConfigPath(path));
        QFile::remove(path);
    }
}

void TestIntegrationBackendVpn::domainRulesAcceptTldWildcardsAndIdn()
{
    Backend backend;
    // TLD-only wildcards are rejected — TrustTunnel's DOMAIN_FILTER cannot use them.
    QVERIFY(!backend.addDomain(QStringLiteral("*.ru")));
    QVERIFY(!backend.addDomain(QStringLiteral(".su")));
    QVERIFY(!backend.addDomain(QStringLiteral("*.рф")));   // *.рф
    QVERIFY(backend.addDomain(QStringLiteral("мвд.рф"))); // мвд.рф
    QVERIFY(backend.addDomain(QStringLiteral("yandex.ru")));
    QVERIFY(backend.addDomain(QStringLiteral("192.168.0.0/16")));
    // A bare TLD without a wildcard, and malformed labels, stay rejected.
    QVERIFY(!backend.addDomain(QStringLiteral("ru")));
    QVERIFY(!backend.addDomain(QStringLiteral("foo_bar.com")));
    QVERIFY(!backend.addDomain(QStringLiteral("-bad.com")));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir iniDir;
    if (!iniDir.isValid())
        return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, iniDir.path());

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BackendVpnTest"));
    app.setOrganizationName(QStringLiteral("FreeTunnelTest"));
    TestIntegrationBackendVpn tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_integration_backend_vpn.moc"
