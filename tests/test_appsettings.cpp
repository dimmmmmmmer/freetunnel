#include <QtTest>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/BypassRules.h"

// Round-trip coverage for the settings store, including the global-hotkey
// fields. QSettings is redirected to a temp dir so the test never touches the
// real user scope.
class TestAppSettings : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase();
    void defaultsWhenEmpty();
    void roundTrip();
    void profilesPreserveOrder();
};

void TestAppSettings::initTestCase() {
    // A unique org/app + IniFormat redirected to a temp dir keeps the test
    // fully hermetic and away from the real "FreeTunnel" store on every
    // platform (macOS NativeFormat goes through cfprefsd, which ignores setPath).
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("AppSettingsTest"));
    QVERIFY(m_dir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
}

void TestAppSettings::defaultsWhenEmpty() {
    // Fresh, empty scope yields the documented defaults.
    AppSettings s = loadAppSettings();
    QCOMPARE(s.language, QStringLiteral("en"));
    QCOMPARE(s.theme_mode, QStringLiteral("system"));
    QCOMPARE(s.auto_connect_on_start, false);
    QCOMPARE(s.killswitch_enabled, true);
    QCOMPARE(s.domain_bypass_enabled, true);
    QCOMPARE(s.profiles.value(QStringLiteral("Default")), recommendedRussiaDomains());
    QVERIFY(!recommendedRussiaDomains().contains(QStringLiteral("*.ru")));
    QVERIFY(!recommendedRussiaDomains().contains(QStringLiteral(".ru")));
    QCOMPARE(s.hotkeys_enabled, true);
    QCOMPARE(s.hotkey_toggle, QStringLiteral("Ctrl+Shift+T"));
    QCOMPARE(s.hotkey_connect, QStringLiteral("Ctrl+Shift+E"));
    QCOMPARE(s.hotkey_disconnect, QStringLiteral("Ctrl+Shift+D"));
    QVERIFY(s.last_config_path.isEmpty());
}

void TestAppSettings::roundTrip() {
    AppSettings in;
    in.language = QStringLiteral("ru");
    in.theme_mode = QStringLiteral("dark");
    in.auto_connect_on_start = true;
    in.killswitch_enabled = true;
    in.domain_bypass_enabled = true;
    in.domain_bypass_rules = {QStringLiteral("github.com"), QStringLiteral("*.gov.ru")};
    in.excluded_routes = {QStringLiteral("10.0.0.0/8"), QStringLiteral("192.168.1.0/24")};
    in.hotkeys_enabled = false;
    in.hotkey_toggle = QStringLiteral("Ctrl+Alt+T");
    in.hotkey_connect = QStringLiteral("Ctrl+Alt+C");
    in.hotkey_disconnect = QStringLiteral("Ctrl+Alt+D");
    in.last_config_path = QStringLiteral("/tmp/configs/germany.toml");
    saveAppSettings(in);

    AppSettings out = loadAppSettings();
    QCOMPARE(out.language, in.language);
    QCOMPARE(out.theme_mode, in.theme_mode);
    QCOMPARE(out.auto_connect_on_start, true);
    QCOMPARE(out.killswitch_enabled, true);
    QCOMPARE(out.domain_bypass_enabled, true);
    QCOMPARE(out.domain_bypass_rules, in.domain_bypass_rules);
    QCOMPARE(out.excluded_routes, in.excluded_routes);
    QCOMPARE(out.hotkeys_enabled, false);
    QCOMPARE(out.hotkey_toggle, in.hotkey_toggle);
    QCOMPARE(out.hotkey_connect, in.hotkey_connect);
    QCOMPARE(out.hotkey_disconnect, in.hotkey_disconnect);
    QCOMPARE(out.last_config_path, in.last_config_path);
}

void TestAppSettings::profilesPreserveOrder() {
    AppSettings in;
    // Names chosen so alphabetical order would differ from creation order.
    in.profiles = {{"Default", {}}, {"Work", {"intra.corp"}}, {"Alpha", {"a.com"}}};
    in.profile_order = {"Default", "Work", "Alpha"};
    in.active_profile = "Work";
    in.domain_bypass_rules = in.profiles.value("Work");
    saveAppSettings(in);

    AppSettings out = loadAppSettings();
    // Creation order preserved (not sorted to Alpha, Default, Work).
    QCOMPARE(out.profile_order, (QStringList{"Default", "Work", "Alpha"}));
    QCOMPARE(out.active_profile, QStringLiteral("Work"));
    QCOMPARE(out.profiles.value("Work"), (QStringList{"intra.corp"}));
    QCOMPARE(out.domain_bypass_rules, (QStringList{"intra.corp"}));
}

QTEST_MAIN(TestAppSettings)
#include "test_appsettings.moc"
