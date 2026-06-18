#include <QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryFile>

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
    void backendConnectsThroughMockHelper();
};

void TestIntegrationBackendVpn::initTestCase()
{
    AppSettings settings = loadAppSettings();
    settings.hotkeys_enabled = false;
    settings.auto_connect_on_start = false;
    settings.killswitch_enabled = false;
    settings.domain_bypass_enabled = false;
    settings.domain_bypass_rules.clear();
    settings.profiles[QStringLiteral("Default")] = {};
    saveAppSettings(settings);
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

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");

    // The keychain service name is hardcoded and not affected by test mode, so
    // drop the entry we created to avoid leaving it in the real OS credential store.
    freetunnel::CredentialStore::deletePassword(configPath);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Keep the test fully hermetic so ctest never touches the real "FreeTunnel"
    // config store (configs.json) or settings scope. A unique org/app name plus
    // IniFormat (macOS NativeFormat goes through cfprefsd, which ignores the test
    // paths) plus test-mode standard paths isolate both configs.json and QSettings.
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BackendVpnTest"));
    app.setOrganizationName(QStringLiteral("FreeTunnelTest"));
    TestIntegrationBackendVpn tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_integration_backend_vpn.moc"
