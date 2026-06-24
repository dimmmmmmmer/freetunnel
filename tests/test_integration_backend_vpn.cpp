#include <QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTemporaryDir>

#include "app/Backend.h"
#include "core/AppSettings.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "helper_ipc_mock_server.h"

class TestIntegrationBackendVpn : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void backendConnectsThroughMockHelper();
    void configSwitchSuppressesCoreDisconnectToast();
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

    // Regression: the materialized password temp file (.connect-*.toml) must
    // survive the whole session — including a disconnect — because the core
    // re-reads the config *path* during its own recovery after a dropped link.
    // Deleting it on a state change made those reconnects fail with "config could
    // not be opened for reading".
    auto connectTemps = [&]() {
        return QDir(base).entryList({QStringLiteral(".connect-*.toml")},
                                    QDir::Files | QDir::Hidden).size();
    };
    QVERIFY(connectTemps() >= 1);

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));
    // It is NOT removed on disconnect — a reconnect could still need it.
    QVERIFY(connectTemps() >= 1);

    backend.prepareQuit();
    // prepareQuit() is the deterministic cleanup point.
    QVERIFY(connectTemps() == 0);
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");

    // The keychain service name is hardcoded and not affected by test mode, so
    // drop the entry we created to avoid leaving it in the real OS credential store.
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

    backend.selectConfig(1);
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));
    QCOMPARE(backend.activeIndex(), 1);

    for (const QVariant &v : errorSpy) {
        const QString msg = v.toString().toLower();
        QVERIFY2(!msg.contains(QStringLiteral("connection lost")),
                 qPrintable(QStringLiteral("unexpected toast: ") + v.toString()));
    }

    backend.prepareQuit();
    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
    freetunnel::CredentialStore::deletePassword(configA);
    freetunnel::CredentialStore::deletePassword(configB);
    QFile::remove(configA);
    QFile::remove(configB);
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
    // TLD-level wildcards and IDN domains from the RU preset must be addable by
    // hand (addDomain returns true only when the rule passes validation).
    QVERIFY(backend.addDomain(QStringLiteral("*.ru")));
    QVERIFY(backend.addDomain(QStringLiteral(".su")));
    QVERIFY(backend.addDomain(QStringLiteral("*.рф")));   // *.рф
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
