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
#include "helper_ipc_mock_server.h"

class TestIntegrationBackendVpn : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void backendConnectsThroughMockHelper();
    void configSwitchSuppressesCoreDisconnectToast();
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

    backend.disconnectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return !backend.connected() && !backend.connecting(); }, 5000));

    backend.prepareQuit();
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
