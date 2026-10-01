// cppcheck-suppress-file missingIncludeSystem
// Split-tunnel behaviour: excluded routes, profiles, and which edits are
// supposed to disturb a live tunnel.
//
// Only addDomain() validation was covered before, so route add/remove/restore,
// the whole profile model, and the "don't churn the tunnel for a no-op" rules
// were free to regress silently — including the setVpnMode fix, which is exactly
// the kind of change that looks right and does nothing.
#include <QtTest>

#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include "app/Backend.h"
#include "core/AppRules.h"
#include "core/AppSettings.h"
#include "core/AppShortcut.h"
#include "core/InstalledApps.h"

class TestBackendSplit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void addExcludedRouteAcceptsValidAndRejectsInvalid();
    void addExcludedRouteAcceptsAPastedList();
    void addExcludedRouteIgnoresDuplicates();
    void anExcludedRouteOfEveryAddressIsRefused();
    void removeAndClearExcludedRoutes();
    void restoreDefaultsIsANoOpWhenAlreadyDefault();
    void profileCreateSelectAndRemove();
    void removingAProfileFallsConfigsBackToDefault();
    void defaultProfileCannotBeRemoved();
    void vpnModeIsPersistedAndNormalized();
    void selectiveModeWithNoRulesKeepsTheFullTunnel();
    void selectiveModeIsInactiveWhileSplitIsOff();
    void appRulesAreRulesToo();
    void aWildcardOnAnAddressIsRefusedWithTheReason();
    void aSavedWildcardOnAnAddressIsNotARule();
    void anAddressRuleOfEveryAddressIsRefused();
    void appRulesThatMatchNothingAreNotRules();
    void appRulesAreValidatedDedupedAndPersisted();
    void aRuleStoredForASquirrelUpdaterBecomesItsProgram();
    void aRuleStoredForASquirrelUpdaterMatchesTheProgramOnWindows();
    void aDroppedShortcutBecomesARuleForTheProgramItNames();
    void turningSplitTunnellingOffDoesNotInvertTheAppRules();
    void appRulesBelongToTheProfile();
    void theOneOldApplicationListSeedsEveryProfileOnce();
    void theLeakWarningPopsUpOnlyOverTheProfileItConcerns();
    void deletingTheActiveConfigTellsTheSplitPage();
    void thePickersListComesFromABackgroundScan();

private:
    QTemporaryDir m_home;
};

void TestBackendSplit::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    // Test mode + *Test names: never touch the real configs.json / settings.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BackendSplitTest"));
    // The Backend persists through a default-constructed QSettings, so the
    // *default* format is what has to be redirected. Clearing
    // QSettings(IniFormat, ...) by hand did nothing: on Unix NativeFormat writes
    // BackendSplitTest.conf while IniFormat addresses BackendSplitTest.ini, so the
    // store the Backend actually reads survived every "clean" init(). Any case that
    // aborted mid-way then poisoned every later run of the suite — and the writes
    // landed in the real ~/.qttest, not in this temp dir.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_home.path());
}

void TestBackendSplit::init()
{
    // Each case starts from a clean settings store.
    QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("FreeTunnelTest"),
              QStringLiteral("BackendSplitTest"))
            .clear();
}

void TestBackendSplit::addExcludedRouteAcceptsValidAndRejectsInvalid()
{
    Backend backend;
    backend.clearExcludedRoutes();
    QSignalSpy errors(&backend, &Backend::errorOccurred);

    QVERIFY(backend.addExcludedRoute(QStringLiteral("10.0.0.0/8")));
    QVERIFY(backend.excludedRoutes().contains(QStringLiteral("10.0.0.0/8")));
    QCOMPARE(errors.count(), 0);

    QVERIFY(!backend.addExcludedRoute(QStringLiteral("not-an-address")));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!backend.excludedRoutes().contains(QStringLiteral("not-an-address")));

    // Out-of-range prefix length must be refused too — an over-broad route here
    // would silently push traffic outside the tunnel.
    QVERIFY(!backend.addExcludedRoute(QStringLiteral("10.0.0.0/33")));
    QVERIFY(!backend.excludedRoutes().contains(QStringLiteral("10.0.0.0/33")));

    QVERIFY(backend.addExcludedRoute(QStringLiteral("2001:db8::/32")));
    QVERIFY(backend.excludedRoutes().contains(QStringLiteral("2001:db8::/32")));
}

void TestBackendSplit::addExcludedRouteAcceptsAPastedList()
{
    Backend backend;
    backend.clearExcludedRoutes();

    QVERIFY(backend.addExcludedRoute(QStringLiteral("10.0.0.0/8, 192.168.0.0/16\n172.16.0.0/12")));
    QCOMPARE(backend.excludedRoutes().size(), 3);
    QVERIFY(backend.excludedRoutes().contains(QStringLiteral("192.168.0.0/16")));

    // A list with one bad entry keeps the good ones and still reports the error.
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    QVERIFY(backend.addExcludedRoute(QStringLiteral("8.8.8.8 nonsense")));
    QCOMPARE(errors.count(), 1);
    QVERIFY(backend.excludedRoutes().contains(QStringLiteral("8.8.8.8")));
    QVERIFY(!backend.excludedRoutes().contains(QStringLiteral("nonsense")));
    // And it says which entry it refused. Paste a dozen subnets with one typo and
    // a message that only restates the format leaves you to find the typo yourself.
    QVERIFY(errors.at(0).at(0).toString().contains(QStringLiteral("nonsense")));
}

void TestBackendSplit::addExcludedRouteIgnoresDuplicates()
{
    Backend backend;
    backend.clearExcludedRoutes();
    QVERIFY(backend.addExcludedRoute(QStringLiteral("10.0.0.0/8")));
    const int before = backend.excludedRoutes().size();

    QVERIFY(!backend.addExcludedRoute(QStringLiteral("10.0.0.0/8")));
    QCOMPARE(backend.excludedRoutes().size(), before);
    // Duplicates within one pasted list collapse as well.
    QVERIFY(backend.addExcludedRoute(QStringLiteral("7.7.7.0/24 7.7.7.0/24")));
    QCOMPARE(backend.excludedRoutes().count(QStringLiteral("7.7.7.0/24")), 1);
}

// A /0 is a well-formed subnet, and as an exclusion it is all of that traffic
// leaving the tunnel while the window says Connected. Refused, saying why.
void TestBackendSplit::anExcludedRouteOfEveryAddressIsRefused()
{
    Backend backend;
    backend.clearExcludedRoutes();
    QSignalSpy errors(&backend, &Backend::errorOccurred);

    for (const QString &route : {QStringLiteral("0.0.0.0/0"), QStringLiteral("::/0"),
                                 QStringLiteral("10.0.0.0/0")}) {
        QVERIFY2(!backend.addExcludedRoute(route), qPrintable(route));
        QVERIFY(backend.excludedRoutes().isEmpty());
        QVERIFY2(errors.last().at(0).toString().contains(QStringLiteral("every address")),
                 qPrintable(errors.last().at(0).toString()));
    }
    // One in a pasted list leaves the rest of it to be added.
    QVERIFY(backend.addExcludedRoute(QStringLiteral("0.0.0.0/0 192.168.0.0/16")));
    QCOMPARE(backend.excludedRoutes(), QStringList{QStringLiteral("192.168.0.0/16")});
    // The shortest prefix that is not everything is still a subnet.
    QVERIFY(backend.addExcludedRoute(QStringLiteral("0.0.0.0/1")));
    // And an ordinary mistake keeps the ordinary message.
    for (const QString &route : {QStringLiteral("10.0.0.0/33"), QStringLiteral("10.0.0.0/-1")}) {
        QVERIFY2(!backend.addExcludedRoute(route), qPrintable(route));
        QVERIFY(!errors.last().at(0).toString().contains(QStringLiteral("every address")));
    }
}

void TestBackendSplit::removeAndClearExcludedRoutes()
{
    Backend backend;
    backend.clearExcludedRoutes();
    QVERIFY(backend.addExcludedRoute(QStringLiteral("10.0.0.0/8 192.168.0.0/16")));
    QCOMPARE(backend.excludedRoutes().size(), 2);

    backend.removeExcludedRoute(0);
    QCOMPARE(backend.excludedRoutes().size(), 1);

    // Out-of-range indices must be inert, not crash or clear the list.
    backend.removeExcludedRoute(-1);
    backend.removeExcludedRoute(99);
    QCOMPARE(backend.excludedRoutes().size(), 1);

    backend.clearExcludedRoutes();
    QVERIFY(backend.excludedRoutes().isEmpty());
}

void TestBackendSplit::restoreDefaultsIsANoOpWhenAlreadyDefault()
{
    Backend backend;
    backend.restoreDefaultExcludedRoutes();
    const QStringList defaults = backend.excludedRoutes();
    QVERIFY(!defaults.isEmpty());

    // Restoring again must not emit splitChanged: that signal drives a live
    // re-apply, and reconnecting the tunnel for a no-op is the bug this guards.
    QSignalSpy changed(&backend, &Backend::splitChanged);
    backend.restoreDefaultExcludedRoutes();
    QCOMPARE(changed.count(), 0);
    QCOMPARE(backend.excludedRoutes(), defaults);

    // But a real deviation is restored, and does signal.
    backend.clearExcludedRoutes();
    changed.clear();
    backend.restoreDefaultExcludedRoutes();
    QCOMPARE(backend.excludedRoutes(), defaults);
    QVERIFY(changed.count() > 0);
}

void TestBackendSplit::profileCreateSelectAndRemove()
{
    Backend backend;
    const int baseCount = backend.profiles().size();
    QVERIFY(backend.profiles().contains(QStringLiteral("Default")));

    backend.addProfile(QStringLiteral("Work"));
    QCOMPARE(backend.profiles().size(), baseCount + 1);
    QCOMPARE(backend.activeProfile(), QStringLiteral("Work")); // edit what you just made
    QVERIFY(backend.domains().isEmpty());

    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    QVERIFY(backend.domains().contains(QStringLiteral("example.com")));

    // Switching profiles swaps the edited rule set, and switching back restores it.
    backend.selectProfile(QStringLiteral("Default"));
    QCOMPARE(backend.activeProfile(), QStringLiteral("Default"));
    QVERIFY(!backend.domains().contains(QStringLiteral("example.com")));
    backend.selectProfile(QStringLiteral("Work"));
    QVERIFY(backend.domains().contains(QStringLiteral("example.com")));

    // Duplicate and empty names are refused.
    backend.addProfile(QStringLiteral("Work"));
    backend.addProfile(QStringLiteral("   "));
    QCOMPARE(backend.profiles().size(), baseCount + 1);

    // And a name that differs only in case, which the storage cannot tell apart.
    // A profile's name is the key it is written under, and on Windows that is a
    // registry value name: "Work" and "work" land in the same place, the second
    // write wins, and after a restart both profiles hold one domain list — so a
    // config silently gets the rules meant for the other profile. Refused on
    // every platform, because settings travel between them.
    backend.addProfile(QStringLiteral("work"));
    backend.addProfile(QStringLiteral("WORK"));
    QCOMPARE(backend.profiles().size(), baseCount + 1);

    backend.removeProfile(QStringLiteral("Work"));
    QCOMPARE(backend.profiles().size(), baseCount);
    QCOMPARE(backend.activeProfile(), QStringLiteral("Default"));
}

void TestBackendSplit::removingAProfileFallsConfigsBackToDefault()
{
    Backend backend;
    backend.addProfile(QStringLiteral("Temp"));
    QVERIFY(backend.profiles().contains(QStringLiteral("Temp")));

    QSignalSpy configChanged(&backend, &Backend::configChanged);
    backend.removeProfile(QStringLiteral("Temp"));

    QVERIFY(!backend.profiles().contains(QStringLiteral("Temp")));
    // Configs that pointed at it must be re-pointed, and the UI told, because a
    // config's effective profile just changed.
    QVERIFY(configChanged.count() > 0);
}

void TestBackendSplit::defaultProfileCannotBeRemoved()
{
    Backend backend;
    backend.removeProfile(QStringLiteral("Default"));
    QVERIFY(backend.profiles().contains(QStringLiteral("Default")));
}

void TestBackendSplit::vpnModeIsPersistedAndNormalized()
{
    {
        Backend backend;
        backend.setVpnMode(QStringLiteral("selective"));
        QCOMPARE(backend.vpnMode(), QStringLiteral("selective"));

        // Anything that isn't a known mode must not be persisted verbatim: the
        // value is read back at startup, so garbage would outlive the session.
        backend.setVpnMode(QStringLiteral("nonsense"));
        QCOMPARE(backend.vpnMode(), QStringLiteral("general"));

        backend.setVpnMode(QStringLiteral("general"));
        QCOMPARE(backend.vpnMode(), QStringLiteral("general"));
        backend.setVpnMode(QStringLiteral("selective"));
    }
    // A new Backend reads the persisted value back.
    Backend reopened;
    QCOMPARE(reopened.vpnMode(), QStringLiteral("selective"));
}

void TestBackendSplit::selectiveModeWithNoRulesKeepsTheFullTunnel()
{
    Backend backend;
    backend.setSplitEnabled(true);
    backend.setVpnMode(QStringLiteral("selective"));
    backend.clearDomains();

    // "selective" routes only the listed rules through the tunnel, so an empty list
    // would route nothing and every byte would leave in the clear while the UI still
    // said Connected. The mode stays *chosen* — the user's setting is not silently
    // rewritten — but it must not be what the core is told.
    QCOMPARE(backend.vpnMode(), QStringLiteral("selective"));
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(backend.selectiveModeWouldLeak());

    // One real rule is enough to make the chosen mode safe, and it must then
    // actually take effect.
    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    QVERIFY(backend.selectiveModeActive());
    QVERIFY(!backend.selectiveModeWouldLeak());

    // Emptying the list again has to fall back, not leak: this is the "Clear all"
    // path on an already-connected client.
    backend.clearDomains();
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(backend.selectiveModeWouldLeak());

    // Bypass mode with no rules is a perfectly ordinary full tunnel, not a leak.
    backend.setVpnMode(QStringLiteral("general"));
    QVERIFY(!backend.selectiveModeWouldLeak());
}

// "Through VPN" with applications and no domains is a complete configuration:
// the listed programs go through the tunnel and nothing else does. Before app
// rules existed, an empty domain list meant "nothing would be routed", so the
// tunnel fell back to carrying everything — doing that here would silently
// route the traffic the user had just arranged to keep out.
void TestBackendSplit::appRulesAreRulesToo()
{
    Backend backend;
    backend.setSplitEnabled(true);
    backend.setVpnMode(QStringLiteral("selective"));
    backend.clearDomains();
    QVERIFY(backend.selectiveModeWouldLeak());

    QVERIFY(backend.addAppRule(QStringLiteral("firefox")));
    QVERIFY(backend.selectiveModeActive());
    QVERIFY2(!backend.selectiveModeWouldLeak(),
             "an app rule is a rule: the mode the user chose is now safe to apply");

    // And removing the last one has to fall back again, exactly as clearing the
    // domains does.
    backend.clearAppRules();
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(backend.selectiveModeWouldLeak());
}

// "*.10.0.0.0/8" is an address with a wildcard in front of it, and the core has
// no such rule. Refused, and the message says why: the subnet itself is fine.
void TestBackendSplit::aWildcardOnAnAddressIsRefusedWithTheReason()
{
    Backend backend;
    backend.clearDomains();
    QSignalSpy errors(&backend, &Backend::errorOccurred);

    QVERIFY(!backend.addDomain(QStringLiteral("*.10.0.0.0/8")));
    QVERIFY(backend.domains().isEmpty());
    QCOMPARE(errors.count(), 1);
    const QString message = errors.at(0).at(0).toString();
    QVERIFY(message.contains(QStringLiteral("*.10.0.0.0/8")));
    QVERIFY2(message.contains(QStringLiteral("without them")), qPrintable(message));

    // Written plainly, it is a rule like any other.
    QVERIFY(backend.addDomain(QStringLiteral("10.0.0.0/8")));
    QCOMPARE(backend.domains(), QStringList{QStringLiteral("10.0.0.0/8")});
    // And an ordinary mistake keeps the ordinary message.
    QVERIFY(!backend.addDomain(QStringLiteral("localhost")));
    QVERIFY(!errors.last().at(0).toString().contains(QStringLiteral("without them")));
}

// One saved by an earlier version, which accepted it. It was the profile's only
// rule here, so "Through VPN" counted it, put the core in selective mode with
// nothing it could match, and sent everything around the tunnel.
void TestBackendSplit::aSavedWildcardOnAnAddressIsNotARule()
{
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("FreeTunnelTest"), QStringLiteral("BackendSplitTest"));
        s.setValue(QStringLiteral("bypass/profile/Default"), QStringList{QStringLiteral("*.1.2.3.4")});
        s.setValue(QStringLiteral("bypass/profile_apps_seeded"), true);
        s.sync();
    }
    Backend backend;
    backend.setSplitEnabled(true);
    backend.setVpnMode(QStringLiteral("selective"));
    QVERIFY(backend.domains().isEmpty());
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(backend.selectiveModeWouldLeak());
}

// A rule for 0.0.0.0/0 or ::/0 is every address of its kind. The Split page took
// it, and under "Bypass VPN" the core sent all of that traffic around the tunnel
// while the window said Connected: the same as an excluded route of every
// address, which Settings refuses. One saved by an earlier version is dropped.
void TestBackendSplit::anAddressRuleOfEveryAddressIsRefused()
{
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("FreeTunnelTest"), QStringLiteral("BackendSplitTest"));
        s.setValue(QStringLiteral("bypass/profile/Default"),
                   QStringList{QStringLiteral("0.0.0.0/0"), QStringLiteral("10.0.0.0/8"),
                               QStringLiteral("::/0")});
        s.setValue(QStringLiteral("bypass/profile_apps_seeded"), true);
        s.sync();
    }
    Backend backend;
    QCOMPARE(backend.domains(), QStringList{QStringLiteral("10.0.0.0/8")});

    QSignalSpy errors(&backend, &Backend::errorOccurred);
    for (const QString &rule : {QStringLiteral("0.0.0.0/0"), QStringLiteral("::/0")}) {
        QVERIFY2(!backend.addDomain(rule), qPrintable(rule));
        const QString message = errors.last().at(0).toString();
        QVERIFY2(message.contains(rule) && message.contains(QStringLiteral("every address")),
                 qPrintable(message));
    }
    QCOMPARE(errors.count(), 2);
    QCOMPARE(backend.domains(), QStringList{QStringLiteral("10.0.0.0/8")});
    // A subnet that is not all of them is a rule like any other.
    QVERIFY(backend.addDomain(QStringLiteral("0.0.0.0/1")));
}

// Settings keep app rules as written, and the helper drops the ones that cannot
// match. A list of only those is no rule at all, and "Through VPN" has to fall
// back to the full tunnel for it as it does for an empty one. Counted raw, it
// put the core in selective mode with nothing listed: everything went around.
void TestBackendSplit::appRulesThatMatchNothingAreNotRules()
{
    QStringList saved{QStringLiteral("relative/firefox"), QStringLiteral("/usr/bin/")};
#ifndef Q_OS_WIN
    // The likeliest way to get here: settings brought over from Windows.
    saved << QStringLiteral("C:\\Program Files\\Mozilla Firefox\\firefox.exe");
#endif
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("FreeTunnelTest"), QStringLiteral("BackendSplitTest"));
        s.setValue(QStringLiteral("bypass/profile/Default"), QStringList{});
        s.setValue(QStringLiteral("bypass/profile_apps/Default"), saved);
        s.setValue(QStringLiteral("bypass/profile_apps_seeded"), true);
        s.sync();
    }
    Backend backend;
    backend.setSplitEnabled(true);
    QSignalSpy warned(&backend, &Backend::errorOccurred);
    backend.setVpnMode(QStringLiteral("selective"));
    QVERIFY(backend.domains().isEmpty());
    QCOMPARE(backend.appRules().size(), saved.size()); // still listed, as written
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(backend.selectiveModeWouldLeak());
    // And the warning does not say there are no rules, over a list of them.
    QCOMPARE(warned.count(), 1);
    const QString warning = warned.first().first().toString();
    QVERIFY2(warning.contains(QStringLiteral("no rules that can be used")), qPrintable(warning));

    // One that can match is enough, as in appRulesAreRulesToo().
    QVERIFY(backend.addAppRule(QStringLiteral("firefox")));
    QVERIFY(backend.selectiveModeActive());
}

namespace {

void touchFile(const QString &path)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.close();
}

// Discord as Squirrel installs it, under `parent`: the updater a rule from an
// earlier version names, and the program that actually runs.
struct SquirrelDiscord {
    QString updater;
    QString running;
};

SquirrelDiscord installSquirrelDiscord(const QString &parent)
{
    SquirrelDiscord d;
    d.updater = QDir::toNativeSeparators(parent + QStringLiteral("/Discord/Update.exe"));
    d.running = QDir::toNativeSeparators(parent + QStringLiteral("/Discord/app-1.0.9163/Discord.exe"));
    touchFile(QDir::fromNativeSeparators(d.updater));
    touchFile(QDir::fromNativeSeparators(d.running));
    return d;
}

void storeAppRules(const QString &profile, const QStringList &rules)
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("FreeTunnelTest"),
                QStringLiteral("BackendSplitTest"));
    s.setValue(QStringLiteral("bypass/profile_apps/") + profile, rules);
    s.setValue(QStringLiteral("bypass/profile_apps_seeded"), true);
    s.sync();
}

} // namespace

// Picking Discord from the list, or dropping its shortcut, stored a rule for its
// updater before shortcuts were followed to their program, and the rule matched
// none of Discord's connections. Such a rule is read as the program from now on,
// and saved that way, so nobody has to find it and add Discord again.
void TestBackendSplit::aRuleStoredForASquirrelUpdaterBecomesItsProgram()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const SquirrelDiscord discord = installSquirrelDiscord(dir.path());
    const QString program = freetunnel::squirrelProgramForUpdaterRule(discord.updater);
    QVERIFY(!program.isEmpty());
    const QString vendorUpdater = QDir::toNativeSeparators(dir.filePath(QStringLiteral("Vendor/Update.exe")));
    touchFile(QDir::fromNativeSeparators(vendorUpdater));
    // The program listed already as well, as someone who added it by hand has it.
    storeAppRules(QStringLiteral("Default"),
                  {discord.updater, QStringLiteral("firefox"), program, vendorUpdater});

    {
        Backend backend;
        QCOMPARE(backend.appRules(), (QStringList{program, QStringLiteral("firefox"), vendorUpdater}));
    }
    // Saved so: read back by a Backend that has nothing left to change.
    QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("FreeTunnelTest"),
                QStringLiteral("BackendSplitTest"));
    QCOMPARE(s.value(QStringLiteral("bypass/profile_apps/Default")).toStringList(),
             (QStringList{program, QStringLiteral("firefox"), vendorUpdater}));
}

// The same where it matters, through what Windows reports for the running
// program: the rule an earlier version stored has to match Discord as it runs,
// in its version directory, from the first start of this version.
void TestBackendSplit::aRuleStoredForASquirrelUpdaterMatchesTheProgramOnWindows()
{
#if !defined(Q_OS_WIN)
    QSKIP("Squirrel installs programs on Windows only");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Under the long name Windows reports a running program by: the runner's
    // temporary folder can be reached through an 8.3 one.
    const SquirrelDiscord discord = installSquirrelDiscord(QFileInfo(dir.path()).canonicalFilePath());
    // In capitals, as nothing on Windows stops a path from being spelled.
    storeAppRules(QStringLiteral("Default"), {discord.updater.toUpper()});
    QVERIFY(!freetunnel::appMatchesRules({discord.running, QStringLiteral("Discord.exe")},
                                         {discord.updater.toUpper()}));

    Backend backend;
    QCOMPARE(backend.appRules().size(), 1);
    const QString rule = backend.appRules().first();
    QVERIFY2(!rule.endsWith(QStringLiteral("Update.exe"), Qt::CaseInsensitive), qPrintable(rule));
    // Discord as it runs, spelled as the rule is: the rule is stored the way
    // Windows reports a running program, which is what the matcher is given.
    const QString root = QFileInfo(QDir::fromNativeSeparators(rule)).path();
    const QString running = QDir::toNativeSeparators(root + QStringLiteral("/app-1.0.9163/Discord.exe"));
    QVERIFY2(QFileInfo(running).isFile(), qPrintable(running));
    QVERIFY2(freetunnel::appMatchesRules({running, QStringLiteral("Discord.exe")}, backend.appRules()),
             qPrintable(rule));
    const QString nextVersion = QString(running).replace(QStringLiteral("app-1.0.9163"),
                                                         QStringLiteral("app-1.0.9170"));
    QVERIFY(freetunnel::appMatchesRules({nextVersion, QStringLiteral("Discord.exe")}, backend.appRules()));
#endif
}

void TestBackendSplit::appRulesAreValidatedDedupedAndPersisted()
{
    {
        Backend backend;
        QVERIFY(backend.appRules().isEmpty());

        // Rejected, and not stored: the user is told rather than shown a rule
        // that could never match anything.
        QVERIFY(!backend.addAppRule(QString()));
        QVERIFY(!backend.addAppRule(QStringLiteral("   ")));
        QVERIFY(!backend.addAppRule(QStringLiteral("relative/path")));
        QVERIFY(backend.appRules().isEmpty());

        QVERIFY(backend.addAppRule(QStringLiteral("  firefox  ")));
        QCOMPARE(backend.appRules(), QStringList{QStringLiteral("firefox")});
        // Re-adding the same program is not an error and not a second entry.
        QVERIFY(!backend.addAppRule(QStringLiteral("firefox")));
        QCOMPARE(backend.appRules().size(), 1);

        // Unlike an address, a rule is never split on whitespace — program paths
        // contain spaces, and splitting one would turn a single valid rule into
        // several invalid ones.
#ifdef Q_OS_WIN
        const QString spaced = QStringLiteral("C:\\Program Files\\Some App\\app.exe");
#else
        const QString spaced = QStringLiteral("/opt/Some App/app");
#endif
        QVERIFY(backend.addAppRule(spaced));
        QCOMPARE(backend.appRules().size(), 2);
        QVERIFY(backend.appRules().last().contains(QLatin1String("Some App")));

        backend.removeAppRule(0);
        QCOMPARE(backend.appRules().size(), 1);
        // Out-of-range removals must not throw the list away.
        backend.removeAppRule(-1);
        backend.removeAppRule(99);
        QCOMPARE(backend.appRules().size(), 1);
    }

    Backend reopened;
    QCOMPARE(reopened.appRules().size(), 1);
    QVERIFY(reopened.appRules().first().contains(QLatin1String("Some App")));
}

// Dropping an icon is the gesture people actually have. What lands on the window
// is a shortcut, not a program, and a rule made from the shortcut's own path
// would be stored, listed back, and never match anything.
void TestBackendSplit::aDroppedShortcutBecomesARuleForTheProgramItNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString program = dir.filePath(QStringLiteral("theprogram"));
    QFile bin(program);
    QVERIFY(bin.open(QIODevice::WriteOnly));
    bin.close();

    const QString entry = dir.filePath(QStringLiteral("shortcut.desktop"));
    QFile f(entry);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(QStringLiteral("[Desktop Entry]\nType=Application\nExec=\"%1\" %u\n")
                    .arg(program).toUtf8());
    f.close();

    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);

    // The URL form, because that is what a drop hands over.
    QVERIFY(backend.addApplicationFromPath(QUrl::fromLocalFile(entry).toString()));
    QCOMPARE(backend.appRules().size(), 1);
    // Canonical, because that is how rules are stored — and on macOS a temporary
    // directory is reached through /var, which is a symlink to /private/var.
    const QString canonical = QFileInfo(program).canonicalFilePath();
    QCOMPARE(backend.appRules().first(),
             QDir::toNativeSeparators(canonical.isEmpty() ? program : canonical));
    QCOMPARE(errors.count(), 0);

    // A folder or a document landing on the window by accident is told apart from
    // a program, and says so rather than storing something unmatchable.
    QVERIFY(!backend.addApplicationFromPath(QUrl::fromLocalFile(dir.path()).toString()));
    QCOMPARE(backend.appRules().size(), 1);
    QCOMPARE(errors.count(), 1);
}

void TestBackendSplit::selectiveModeIsInactiveWhileSplitIsOff()
{
    Backend backend;
    backend.setVpnMode(QStringLiteral("selective"));
    backend.setSplitEnabled(false);
    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    // The master toggle wins: with split tunneling off the core gets no rules at
    // all, so selective mode must not be requested either.
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(!backend.selectiveModeWouldLeak());
}

// The nastiest shape this feature can take. In "Through VPN" the list means
// "only these go through"; in bypass mode the same list means "these stay out".
// Switching the whole feature off puts the core in general mode — so a list that
// was still being pushed reversed its meaning, and a user who turned split
// tunnelling off expecting everything to be protected got the one program they
// cared about sent out in the clear, with the interface saying it was off.
void TestBackendSplit::turningSplitTunnellingOffDoesNotInvertTheAppRules()
{
    Backend backend;
    backend.setSplitEnabled(true);
    backend.setVpnMode(QStringLiteral("selective"));
    QVERIFY(backend.addAppRule(QStringLiteral("firefox")));
    QVERIFY(backend.selectiveModeActive());

    backend.setSplitEnabled(false);
    // The rule stays in the settings — the user did not delete it, and it comes
    // back when they switch the feature on again.
    QCOMPARE(backend.appRules().size(), 1);
    // But it must no longer be in force, in either direction.
    QVERIFY(!backend.selectiveModeActive());
    QVERIFY(!backend.selectiveModeWouldLeak());

    backend.setSplitEnabled(true);
    QVERIFY(backend.selectiveModeActive());
}

// What leaks is the active config's profile. With the Split page on another one,
// every rule added there was answered by "Through VPN has no rules… until you
// add a rule", as though the rule just added did not count. The page's standing
// notice names the config and its profile instead.
void TestBackendSplit::theLeakWarningPopsUpOnlyOverTheProfileItConcerns()
{
    Backend backend;
    backend.setSplitEnabled(true);
    backend.selectProfile(QStringLiteral("Default"));
    backend.clearDomains();
    backend.clearAppRules();
    backend.addProfile(QStringLiteral("Work"));
    backend.selectProfile(QStringLiteral("Work"));
    QSignalSpy errors(&backend, &Backend::errorOccurred);

    // Anything that is not an edit of another profile says it, whichever profile
    // the page shows: here a change of mode. A connect takes the same path.
    backend.setVpnMode(QStringLiteral("selective"));
    QVERIFY(backend.selectiveModeWouldLeak()); // the config still uses Default, which is empty
    QCOMPARE(errors.count(), 1);

    // A rule added to Work, which the config does not use, does not.
    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    QCOMPARE(errors.count(), 1);

    // Over the profile concerned, an edit says it too.
    backend.selectProfile(QStringLiteral("Default"));
    backend.addRecommendedRussia();
    backend.clearDomains();
    QCOMPARE(errors.count(), 2);
    backend.setVpnMode(QStringLiteral("general"));
}

// Deleting the active config hands the slot to another one, and with it the
// profile the tunnel follows. The Split page's notice reads that through
// splitChanged, and without it went on describing the config just deleted.
void TestBackendSplit::deletingTheActiveConfigTellsTheSplitPage()
{
    Backend backend;
    // Configs are kept where every run of this suite looks (test mode puts them
    // under ~/.qttest, not in m_home), so start from none and leave none behind,
    // passwords included, however the case ends.
    const auto removeAll = [&backend]() {
        for (qsizetype n = backend.configs().size(); n > 0; --n)
            backend.removeConfig(0);
    };
    removeAll();
    const auto cleanup = qScopeGuard(removeAll);
    backend.setSplitEnabled(true);
    backend.selectProfile(QStringLiteral("Default"));
    backend.clearDomains();
    backend.clearAppRules();
    backend.addProfile(QStringLiteral("Work"));
    QVERIFY(backend.addDomain(QStringLiteral("example.com")));
    const auto config = [](const QString &name, const QString &profile) {
        return QVariantMap{{QStringLiteral("name"), name},
                           {QStringLiteral("hostname"), QStringLiteral("vpn.example.org")},
                           {QStringLiteral("addresses"), QStringLiteral("198.51.100.7:443")},
                           {QStringLiteral("username"), QStringLiteral("alice")},
                           {QStringLiteral("password"), QStringLiteral("a")},
                           {QStringLiteral("protocol"), QStringLiteral("http2")},
                           {QStringLiteral("splitProfile"), profile}};
    };
    QVERIFY(backend.createConfig(config(QStringLiteral("Home"), QStringLiteral("Default"))));
    QVERIFY(backend.createConfig(config(QStringLiteral("Office"), QStringLiteral("Work"))));
    QCOMPARE(backend.activeConfigProfile(), QStringLiteral("Work")); // Office, just created
    backend.setVpnMode(QStringLiteral("selective"));
    QVERIFY(!backend.selectiveModeWouldLeak());

    QSignalSpy changed(&backend, &Backend::splitChanged);
    backend.removeConfig(backend.activeIndex());
    QCOMPARE(backend.activeConfigProfile(), QStringLiteral("Default")); // Home, which is empty
    QVERIFY(backend.selectiveModeWouldLeak());
    QVERIFY2(changed.count() > 0, "the Split page was not told");
}

// The picker's first open scanned on the UI thread, and on Windows froze the
// window while every Start Menu shortcut was resolved. It gets what the scan on a
// worker has so far, and hears through splitChanged when the rest is in.
void TestBackendSplit::thePickersListComesFromABackgroundScan()
{
    Backend backend;
    QSignalSpy changed(&backend, &Backend::splitChanged);
    QVERIFY(backend.installedApplications().isEmpty());
    QVERIFY2(!backend.installedAppsReady(), "the list was read on this thread");
    QTRY_VERIFY_WITH_TIMEOUT(backend.installedAppsReady(), 30000);
    QVERIFY(changed.count() >= 1);
    QCOMPARE(backend.installedApplications().size(), freetunnel::installedApplications().size());
}

QTEST_MAIN(TestBackendSplit)
// Applications belong to the profile, the same as the addresses. Without that,
// a profile switch changed half of what the tunnel would do and left the other
// half behind, with nothing on screen to say so.
void TestBackendSplit::appRulesBelongToTheProfile()
{
    {
        Backend backend;
        QVERIFY(backend.addAppRule(QStringLiteral("firefox")));
        QCOMPARE(backend.appRules(), QStringList{QStringLiteral("firefox")});

        backend.addProfile(QStringLiteral("Work"));
        backend.selectProfile(QStringLiteral("Work"));
        QVERIFY2(backend.appRules().isEmpty(), "a new profile starts with none of its own");

        QVERIFY(backend.addAppRule(QStringLiteral("thunderbird")));
        QCOMPARE(backend.appRules(), QStringList{QStringLiteral("thunderbird")});

        backend.selectProfile(QStringLiteral("Default"));
        QCOMPARE(backend.appRules(), QStringList{QStringLiteral("firefox")});
    }

    // And each list is stored under its own profile, not merged on the way out.
    Backend reopened;
    QCOMPARE(reopened.appRules(), QStringList{QStringLiteral("firefox")});
    reopened.selectProfile(QStringLiteral("Work"));
    QCOMPARE(reopened.appRules(), QStringList{QStringLiteral("thunderbird")});

    // Deleting a profile takes its applications with it, and leaves the ones
    // that were never its own alone.
    reopened.selectProfile(QStringLiteral("Default"));
    reopened.removeProfile(QStringLiteral("Work"));
    QCOMPARE(reopened.appRules(), QStringList{QStringLiteral("firefox")});
    Backend afterDelete;
    QCOMPARE(afterDelete.profiles(), QStringList{QStringLiteral("Default")});
    QCOMPARE(afterDelete.appRules(), QStringList{QStringLiteral("firefox")});
}

// Before 1.2.0 there was one application list for the whole program. Splitting it
// across the profiles must not read as "my rules are gone", so the old list is
// what every profile that already existed starts from — which is the behaviour it
// had. And the seeding happens once: a profile whose list the user then empties
// stays empty across a restart.
void TestBackendSplit::theOneOldApplicationListSeedsEveryProfileOnce()
{
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("FreeTunnelTest"), QStringLiteral("BackendSplitTest"));
        s.setValue(QStringLiteral("bypass/profile_names"),
                   QStringList{QStringLiteral("Default"), QStringLiteral("Work")});
        s.setValue(QStringLiteral("routing/app_rules"), QStringList{QStringLiteral("firefox")});
        s.sync();
    }

    {
        Backend migrated;
        QCOMPARE(migrated.appRules(), QStringList{QStringLiteral("firefox")});
        migrated.selectProfile(QStringLiteral("Work"));
        QVERIFY2(migrated.appRules().contains(QStringLiteral("firefox")),
                 "the profile that already existed keeps what used to apply to it");
        migrated.clearAppRules();
        QVERIFY(migrated.appRules().isEmpty());
    }

    Backend reopened;
    reopened.selectProfile(QStringLiteral("Work"));
    QVERIFY2(reopened.appRules().isEmpty(), "and the old list does not come back");
    reopened.selectProfile(QStringLiteral("Default"));
    QCOMPARE(reopened.appRules(), QStringList{QStringLiteral("firefox")});
}

#include "test_backend_split.moc"
