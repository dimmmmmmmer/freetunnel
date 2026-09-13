// cppcheck-suppress-file missingIncludeSystem
// The picker exists so nobody has to know a path. That only works if the list is
// the list a person recognises: the programs they launch, each named once, and
// none of the invisible entries the desktop keeps for its own bookkeeping.
#include <QtTest>

#include "core/InstalledApps.h"

#include <QDir>
#include <QSet>

class TestInstalledApps : public QObject {
    Q_OBJECT

private slots:
    void offersOnlyEntriesMeantToBeSeen();
    void offersOnlyEntriesMeantToBeSeen_data();
    void readsTheDisplayName();
    void theScanIsSaneOnThisMachine();
    void looksWhereThisSystemActuallyKeepsApplications();
};

void TestInstalledApps::offersOnlyEntriesMeantToBeSeen_data()
{
    QTest::addColumn<QString>("contents");
    QTest::addColumn<bool>("visible");

    QTest::newRow("ordinary application")
            << QStringLiteral("[Desktop Entry]\nType=Application\nName=Thing\nExec=/usr/bin/thing\n") << true;
    // NoDisplay is how a MIME handler or a session helper says "I am not a thing
    // in the menu". Listing those would bury the programs someone is looking for.
    QTest::newRow("NoDisplay")
            << QStringLiteral("[Desktop Entry]\nType=Application\nNoDisplay=true\nName=Helper\n") << false;
    QTest::newRow("NoDisplay capitalised")
            << QStringLiteral("[Desktop Entry]\nType=Application\nNoDisplay=True\nName=Helper\n") << false;
    QTest::newRow("Hidden")
            << QStringLiteral("[Desktop Entry]\nType=Application\nHidden=true\nName=Gone\n") << false;
    // A link or a directory entry is not a program and cannot be a rule.
    QTest::newRow("Type=Link")
            << QStringLiteral("[Desktop Entry]\nType=Link\nName=Somewhere\nURL=https://x\n") << false;
    QTest::newRow("no Type at all") << QStringLiteral("[Desktop Entry]\nName=Mystery\n") << false;
    QTest::newRow("empty") << QString() << false;
}

void TestInstalledApps::offersOnlyEntriesMeantToBeSeen()
{
    QFETCH(QString, contents);
    QFETCH(bool, visible);
    QCOMPARE(freetunnel::desktopEntryIsVisibleApplication(contents), visible);
}

void TestInstalledApps::readsTheDisplayName()
{
    QCOMPARE(freetunnel::displayNameFromDesktopEntry(
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Firefox Web Browser\n")),
             QStringLiteral("Firefox Web Browser"));
    // A name from a desktop action is not this entry's name.
    QCOMPARE(freetunnel::displayNameFromDesktopEntry(QStringLiteral(
                     "[Desktop Entry]\nName=Firefox\n\n[Desktop Action new-window]\nName=New Window\n")),
             QStringLiteral("Firefox"));
    QVERIFY(freetunnel::displayNameFromDesktopEntry(QStringLiteral("[Desktop Entry]\n")).isEmpty());
}

// Not an assertion about this machine's software, which the test cannot know:
// an assertion about the shape of whatever it finds. A scan that returned rows
// with an empty name, an empty target, or the same program twice would make the
// picker useless in a way no amount of parser testing would catch.
void TestInstalledApps::theScanIsSaneOnThisMachine()
{
    QElapsedTimer timer;
    timer.start();
    const QList<freetunnel::InstalledApp> apps = freetunnel::installedApplications();
    const qint64 elapsed = timer.elapsed();

    QSet<QString> targets;
    for (const freetunnel::InstalledApp &app : apps) {
        QVERIFY2(!app.name.isEmpty(), "a row with no name is a row nobody can pick");
        QVERIFY2(!app.executablePath.isEmpty(), "a row with no target cannot become a rule");
        QVERIFY2(!targets.contains(app.executablePath),
                 qPrintable(QStringLiteral("listed twice: %1").arg(app.executablePath)));
        targets.insert(app.executablePath);
    }

    // The picker opens on this call, so it has to be quick enough not to be felt.
    // Generous, because a CI runner is not a desktop and Windows resolves every
    // Start Menu shortcut through the shell.
    QVERIFY2(elapsed < 10000, qPrintable(QStringLiteral("scan took %1 ms").arg(elapsed)));
}

// Qt's idea of where applications live is incomplete on two of the three
// platforms, in a way that is invisible until somebody looks for a program that
// is not in the list — which is how both of these were reported. Read out of
// Qt's own source rather than guessed at, and pinned here on the platform each
// one is about, because that is the only place the fact can be checked.
void TestInstalledApps::looksWhereThisSystemActuallyKeepsApplications()
{
    const QStringList dirs = freetunnel::applicationDirectories();
    QVERIFY2(!dirs.isEmpty(), "a system keeps its applications somewhere");
    for (const QString &dir : dirs)
        QVERIFY2(QDir::isAbsolutePath(dir), qPrintable(QStringLiteral("not absolute: %1").arg(dir)));
    QCOMPARE(dirs.size(), QSet<QString>(dirs.cbegin(), dirs.cend()).size()); // no duplicates

#if defined(Q_OS_MACOS)
    // standardLocations() adds NSSystemDomainMask for fonts and caches only, so
    // /System/Applications — Safari, Mail, Messages, and everything in
    // Utilities — was missing from the picker entirely.
    QVERIFY2(dirs.contains(QStringLiteral("/System/Applications")),
             "the system's own applications have to be offered too");
    QVERIFY(dirs.contains(QStringLiteral("/Applications")));
#elif defined(Q_OS_WIN)
    // standardLocations() returns writableLocation() alone here, which is the
    // CURRENT USER's Start Menu. Most installers write to the machine-wide one,
    // so the list was missing the majority of what is installed.
    QVERIFY2(dirs.size() >= 2,
             "both the per-user and the machine-wide Start Menu, not just one of them");
    int startMenus = 0;
    for (const QString &dir : dirs) {
        if (dir.contains(QLatin1String("Start Menu"), Qt::CaseInsensitive))
            ++startMenus;
    }
    QVERIFY2(startMenus >= 2, "one of them is the machine-wide Start Menu");
#else
    // Nothing to add here: the list comes from XDG_DATA_DIRS, which is how a
    // desktop says where Flatpak and Snap put their entries. Measured on a real
    // one: it covers /usr/share/applications, the user's own, and the flatpak
    // exports.
    QVERIFY(!dirs.isEmpty());
#endif
}

QTEST_MAIN(TestInstalledApps)
#include "test_installedapps.moc"
