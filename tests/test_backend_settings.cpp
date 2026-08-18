// cppcheck-suppress-file missingIncludeSystem
// Everything on the Settings page: the preference setters, the log path, launch
// at login, and the hotkey fields.
//
// BackendSettings.cpp sat at 9.8% line coverage and PlatformAutoStart.cpp at
// exactly 0% — the switch that decides whether the app starts with the machine
// had never been executed by a test. These are all small functions, which is
// precisely why they rot unnoticed: a setter that forgets persistSettings()
// looks correct in review and silently drops the preference on restart.
#include <QtTest>

#include <QScopeGuard>

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "app/Backend.h"
#include "app/PlatformAutoStart.h"

class TestBackendSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void preferencesSurviveARestart();
    void preferencesSurviveARestart_data();
    void settingTheSameValueIsSilent();
    void languageChangeAnnouncesTheNewLanguage();
    void logPathFallsBackToTheAppDataDirectory();

    void hotkeyFieldsPersistAndAnnounce();
    void hotkeyFieldsPersistAndAnnounce_data();
    void physicalKeyPositionsMapToLetters();
    void unknownScanCodesMapToNothing();
    void hotkeysAreUnsupportedOnAWaylandSessionEvenUnderXWayland();

    void autoStartWritesAnAutostartEntry();
    void autoStartRemovalTakesTheEntryAway();

private:
    QTemporaryDir m_home;
};

void TestBackendSettings::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BackendSettingsTest"));
}

void TestBackendSettings::init()
{
    QSettings s;
    s.clear();
    s.sync();
}

// A preference the app forgets is worse than one it never offered.
//
// One setter per case, on its own Backend. Exercising several together hides the
// bug this is here to catch: persistSettings() writes the WHOLE settings struct,
// so the next setter in the sequence saves the previous one's value for it and a
// setter that forgot to persist still looks like it works.
void TestBackendSettings::preferencesSurviveARestart_data()
{
    QTest::addColumn<QString>("key");
    QTest::newRow("language") << QStringLiteral("language");
    QTest::newRow("themeMode") << QStringLiteral("themeMode");
    QTest::newRow("autoConnect") << QStringLiteral("autoConnect");
    QTest::newRow("killSwitch") << QStringLiteral("killSwitch");
    QTest::newRow("verboseLogs") << QStringLiteral("verboseLogs");
    QTest::newRow("loggingEnabled") << QStringLiteral("loggingEnabled");
}

void TestBackendSettings::preferencesSurviveARestart()
{
    QFETCH(QString, key);
    QSettings().clear();

    // Each preference is set to the opposite of its default, alone.
    {
        Backend backend;
        if (key == QLatin1String("language"))
            backend.setLanguage(QStringLiteral("ru"));
        else if (key == QLatin1String("themeMode"))
            backend.setThemeMode(QStringLiteral("dark"));
        else if (key == QLatin1String("autoConnect"))
            backend.setAutoConnect(true);
        else if (key == QLatin1String("killSwitch"))
            backend.setKillSwitch(false);
        else if (key == QLatin1String("verboseLogs"))
            backend.setVerboseLogs(true);
        else if (key == QLatin1String("loggingEnabled"))
            backend.setLoggingEnabled(false);
        else
            QFAIL("unknown preference");
    }

    Backend restarted;
    if (key == QLatin1String("language"))
        QCOMPARE(restarted.language(), QStringLiteral("ru"));
    else if (key == QLatin1String("themeMode"))
        QCOMPARE(restarted.themeMode(), QStringLiteral("dark"));
    else if (key == QLatin1String("autoConnect"))
        QCOMPARE(restarted.autoConnect(), true);
    else if (key == QLatin1String("killSwitch"))
        QCOMPARE(restarted.killSwitch(), false);
    else if (key == QLatin1String("verboseLogs"))
        QCOMPARE(restarted.verboseLogs(), true);
    else if (key == QLatin1String("loggingEnabled"))
        QCOMPARE(restarted.loggingEnabled(), false);
}

// Every setter guards on "did this actually change". QML binds two ways, so a
// setter that signals unconditionally can bounce a property back and forth
// between the view and the model — and setKillSwitch would rebuild a live tunnel
// for a value that did not move.
void TestBackendSettings::settingTheSameValueIsSilent()
{
    Backend backend;
    backend.setThemeMode(QStringLiteral("dark"));
    backend.setAutoConnect(true);
    backend.setKillSwitch(true);
    backend.setVerboseLogs(true);
    backend.setLoggingEnabled(true);

    QSignalSpy changed(&backend, &Backend::settingsChanged);
    backend.setThemeMode(QStringLiteral("dark"));
    backend.setAutoConnect(true);
    backend.setKillSwitch(true);
    backend.setVerboseLogs(true);
    backend.setLoggingEnabled(true);
    QCOMPARE(changed.count(), 0);

    // ...and a real change still gets through.
    backend.setThemeMode(QStringLiteral("light"));
    QCOMPARE(changed.count(), 1);
}

// The whole UI re-translates on this signal, so it has to carry the language
// that was actually applied.
void TestBackendSettings::languageChangeAnnouncesTheNewLanguage()
{
    Backend backend;
    QSignalSpy language(&backend, &Backend::languageChanged);
    backend.setLanguage(QStringLiteral("ru"));
    QCOMPARE(language.count(), 1);
    QCOMPARE(language.at(0).at(0).toString(), QStringLiteral("ru"));
    QCOMPARE(backend.language(), QStringLiteral("ru"));

    backend.setLanguage(QStringLiteral("ru"));
    QCOMPARE(language.count(), 1); // unchanged: no re-translation storm
}

// "Open log folder" and the log writer both go through logPath(). With no
// configured path it must resolve into the app's own data directory — an empty
// setting once sent it to a world-writable /tmp default.
void TestBackendSettings::logPathFallsBackToTheAppDataDirectory()
{
    QSettings s;
    s.setValue(QStringLiteral("logs/path"), QString());
    s.sync();

    Backend backend;
    const QString p = backend.logPath();
    QVERIFY2(!p.isEmpty(), "logPath() is empty — the log would go nowhere");
    QVERIFY2(p.endsWith(QStringLiteral(".log")), qPrintable(p));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY2(p.startsWith(appData), qPrintable(p + QStringLiteral(" not under ") + appData));
}

// Same isolation as the preferences above, and for the same reason.
void TestBackendSettings::hotkeyFieldsPersistAndAnnounce_data()
{
    QTest::addColumn<QString>("field");
    QTest::newRow("toggle") << QStringLiteral("toggle");
    QTest::newRow("connect") << QStringLiteral("connect");
    QTest::newRow("disconnect") << QStringLiteral("disconnect");
    QTest::newRow("enabled") << QStringLiteral("enabled");
}

void TestBackendSettings::hotkeyFieldsPersistAndAnnounce()
{
    QFETCH(QString, field);
    QSettings().clear();
    const QString seq = QStringLiteral("Ctrl+Alt+1");

    {
        Backend backend;
        QSignalSpy hotkeys(&backend, &Backend::hotkeysChanged);
        if (field == QLatin1String("toggle"))
            backend.setHotkeyToggle(seq);
        else if (field == QLatin1String("connect"))
            backend.setHotkeyConnect(seq);
        else if (field == QLatin1String("disconnect"))
            backend.setHotkeyDisconnect(seq);
        else
            backend.setHotkeysEnabled(false);
        QCOMPARE(hotkeys.count(), 1);

        // Re-assigning the same value must not re-register the grabs.
        if (field == QLatin1String("toggle"))
            backend.setHotkeyToggle(seq);
        else if (field == QLatin1String("connect"))
            backend.setHotkeyConnect(seq);
        else if (field == QLatin1String("disconnect"))
            backend.setHotkeyDisconnect(seq);
        else
            backend.setHotkeysEnabled(false);
        QCOMPARE(hotkeys.count(), 1);
    }

    Backend restarted;
    if (field == QLatin1String("toggle"))
        QCOMPARE(restarted.hotkeyToggle(), seq);
    else if (field == QLatin1String("connect"))
        QCOMPARE(restarted.hotkeyConnect(), seq);
    else if (field == QLatin1String("disconnect"))
        QCOMPARE(restarted.hotkeyDisconnect(), seq);
    else
        QCOMPARE(restarted.hotkeysEnabled(), false);
}

// The hotkey field records the physical key position, not the character the
// current layout produces, so a shortcut captured on a Russian layout still
// fires on a US one. The map is per-platform; check this platform's.
void TestBackendSettings::physicalKeyPositionsMapToLetters()
{
    Backend backend;
#if defined(Q_OS_MACOS)
    QCOMPARE(backend.physicalLetterForScanCode(12), QStringLiteral("Q"));
    QCOMPARE(backend.physicalLetterForScanCode(0), QStringLiteral("A"));
    QCOMPARE(backend.physicalLetterForScanCode(6), QStringLiteral("Z"));
#elif defined(Q_OS_WIN)
    QCOMPARE(backend.physicalLetterForScanCode(0x10), QStringLiteral("Q"));
    QCOMPARE(backend.physicalLetterForScanCode(0x1E), QStringLiteral("A"));
    QCOMPARE(backend.physicalLetterForScanCode(0x2C), QStringLiteral("Z"));
#else
    QCOMPARE(backend.physicalLetterForScanCode(24), QStringLiteral("Q"));
    QCOMPARE(backend.physicalLetterForScanCode(38), QStringLiteral("A"));
    QCOMPARE(backend.physicalLetterForScanCode(52), QStringLiteral("Z"));
#endif
}

// A modifier, a function key or anything else that is not a letter position has
// to come back empty rather than as some arbitrary letter — the field falls back
// to Qt's own key text in that case.
void TestBackendSettings::unknownScanCodesMapToNothing()
{
    Backend backend;
    QVERIFY(backend.physicalLetterForScanCode(9999).isEmpty());
    QVERIFY(backend.physicalLetterForScanCode(0xFFFFFFFFu).isEmpty());
}

// Launch at login. Only exercised where the entry is a file under a directory
// this test controls: on macOS it is a LaunchAgent in the real ~/Library and on
// Windows a real HKCU\...\Run value, and a test has no business registering the
// test binary to start with the user's machine.
void TestBackendSettings::autoStartWritesAnAutostartEntry()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QSKIP("would write a real LaunchAgent / Run key outside the test scope");
#else
    Backend backend;
    QVERIFY(!backend.autoStart()); // nothing registered in a fresh scope

    QSignalSpy changed(&backend, &Backend::settingsChanged);
    backend.setAutoStart(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(backend.autoStart());

    const QString entry = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/autostart/freetunnel.desktop");
    QVERIFY2(QFileInfo::exists(entry), qPrintable(entry));

    QFile f(entry);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(f.readAll());
    QVERIFY2(body.contains(QStringLiteral("Type=Application")), qPrintable(body));
    QVERIFY2(body.contains(QStringLiteral("X-GNOME-Autostart-enabled=true")), qPrintable(body));
    // Exec= is word-split like a shell command line, so the program path must be
    // quoted or an install directory with a space starts the wrong thing.
    const QString exec = body.section(QStringLiteral("Exec="), 1).section(QLatin1Char('\n'), 0, 0);
    QVERIFY2(exec.startsWith(QLatin1Char('"')) && exec.endsWith(QLatin1Char('"')),
             qPrintable(exec));
    QVERIFY2(exec.contains(QCoreApplication::applicationFilePath()), qPrintable(exec));
#endif
}

void TestBackendSettings::autoStartRemovalTakesTheEntryAway()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QSKIP("would touch a real LaunchAgent / Run key outside the test scope");
#else
    Backend backend;
    backend.setAutoStart(true);
    QVERIFY(backend.autoStart());

    backend.setAutoStart(false);
    QVERIFY2(!backend.autoStart(), "the app would still start with the machine");
    const QString entry = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/autostart/freetunnel.desktop");
    QVERIFY2(!QFileInfo::exists(entry), qPrintable(entry));
#endif
}

// Global hotkeys are X11 grabs, and a Wayland compositor never feeds one. The
// check used to look only at the Qt platform name, which says "xcb" under
// XWayland — so on a Wayland session started that way the app reported hotkeys as
// available, bound them, and they silently never fired. The warning even used to
// send people down that road. What decides is the session, not the plugin.
void TestBackendSettings::hotkeysAreUnsupportedOnAWaylandSessionEvenUnderXWayland()
{
    const QByteArray savedDisplay = qgetenv("WAYLAND_DISPLAY");
    const QByteArray savedType = qgetenv("XDG_SESSION_TYPE");
    auto restore = qScopeGuard([&] {
        savedDisplay.isEmpty() ? qunsetenv("WAYLAND_DISPLAY")
                               : qputenv("WAYLAND_DISPLAY", savedDisplay);
        savedType.isEmpty() ? qunsetenv("XDG_SESSION_TYPE")
                            : qputenv("XDG_SESSION_TYPE", savedType);
    });

    Backend backend;
    // The test runs under the offscreen plugin, whose name contains no "wayland" —
    // exactly the blind spot XWayland exploited.
    qunsetenv("WAYLAND_DISPLAY");
    qunsetenv("XDG_SESSION_TYPE");
    QVERIFY2(backend.hotkeysSupported(), "no Wayland in sight, yet hotkeys were refused");

    qputenv("WAYLAND_DISPLAY", "wayland-0");
    QVERIFY2(!backend.hotkeysSupported(), "a running Wayland compositor was not noticed");

    qunsetenv("WAYLAND_DISPLAY");
    qputenv("XDG_SESSION_TYPE", "wayland");
    QVERIFY2(!backend.hotkeysSupported(), "a Wayland session type was not noticed");

    // Case is not the session's business to get right.
    qputenv("XDG_SESSION_TYPE", "Wayland");
    QVERIFY(!backend.hotkeysSupported());

    qputenv("XDG_SESSION_TYPE", "x11");
    QVERIFY(backend.hotkeysSupported());
}

QTEST_MAIN(TestBackendSettings)
#include "test_backend_settings.moc"
