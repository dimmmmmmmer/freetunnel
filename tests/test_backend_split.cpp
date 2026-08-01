// cppcheck-suppress-file missingIncludeSystem
// Split-tunnel behaviour: excluded routes, profiles, and which edits are
// supposed to disturb a live tunnel.
//
// Only addDomain() validation was covered before, so route add/remove/restore,
// the whole profile model, and the "don't churn the tunnel for a no-op" rules
// were free to regress silently — including the setVpnMode fix, which is exactly
// the kind of change that looks right and does nothing.
#include <QtTest>

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "app/Backend.h"
#include "core/AppSettings.h"

class TestBackendSplit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void addExcludedRouteAcceptsValidAndRejectsInvalid();
    void addExcludedRouteAcceptsAPastedList();
    void addExcludedRouteIgnoresDuplicates();
    void removeAndClearExcludedRoutes();
    void restoreDefaultsIsANoOpWhenAlreadyDefault();
    void profileCreateSelectAndRemove();
    void removingAProfileFallsConfigsBackToDefault();
    void defaultProfileCannotBeRemoved();
    void vpnModeIsPersistedAndNormalized();

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

QTEST_MAIN(TestBackendSplit)
#include "test_backend_split.moc"
