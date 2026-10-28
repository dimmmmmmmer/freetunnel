// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QSignalSpy>

#include "app/DesktopChrome.h"

// Set by the build exactly where the portal tests can be built: they drive the
// Linux half directly, through a header in src/app and with QtDBus, and the build
// provides both only under UNIX AND NOT APPLE.
#ifdef FT_TEST_PORTAL
#include "DesktopChromeLinux.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <xcb/xcb.h>

#include <csignal>
#include <cstdlib>
#include <optional>
#include <vector>
#include <sys/prctl.h>
#endif

using freetunnel::ButtonLayout;
using freetunnel::DesktopChrome;
using freetunnel::parseButtonLayout;

class TestDesktopChrome : public QObject {
    Q_OBJECT

private slots:
    void theLayoutsDesktopsActuallyShip_data();
    void theLayoutsDesktopsActuallyShip();
    void aLayoutWithNoColonIsAllLeftSide();
    void aButtonNamedTwiceIsKeptWhereItFirstAppears();
    void onlyAChangedSettingIsAnnounced();
    void theThemeDecidesBetweenPopAndLibadwaita();
    void eachPlatformStartsWithItsOwnLook();
    void theDesktopsDarkPreferenceIsRead();
#ifdef FT_TEST_PORTAL
    void thePortalIsReadBeforeTheFirstFrame();
    void theFirstAnswerForASettingIsTheOneKept();
    void aChangeOnTheDesktopIsFollowed();
    void withNoPortalNothingChangesAndNothingWaits();
    void aSlowPortalDoesNotHoldTheWindowButIsStillHeard();
    void aChangeRightAfterTheFirstReadIsNotLost();
    void aTrayHostThatArrivesLateIsReported();
    void theTrayHostIsAskedNotAssumed();
    void aBarThatLeftTheBusIsNoPlaceToHideTheWindow();
    void aWindowIsIconifiedWhenItsWindowManagerSaysSo();
    void aWindowIsWithdrawnWhenItsWindowManagerSaysSoOrHasNeverSeenIt();
    // Last: it is the one test that points this process's session bus somewhere.
    void followingTheDesktopReadsTheSessionBus();
#endif
};

#ifdef FT_TEST_PORTAL
namespace {

const QString kAppearance = QStringLiteral("org.freedesktop.appearance");
const QString kWm = QStringLiteral("org.gnome.desktop.wm.preferences");
const QString kInterface = QStringLiteral("org.gnome.desktop.interface");

// A dbus-daemon of the test's own. It can start no services, so a portal that is
// not on it is simply not there — rather than started from the machine's own
// service files, which is what a bus from dbus-run-session would do.
class PrivateBus {
public:
    bool start()
    {
        const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        if (daemon.isEmpty() || !m_dir.isValid())
            return false;
        const QString config = m_dir.filePath(QStringLiteral("bus.conf"));
        QFile file(config);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        // Plain literals: moc cannot read past a raw string, and would lose every
        // class after it.
        file.write("<busconfig>\n"
                   "  <type>session</type>\n"
                   "  <listen>unix:tmpdir=/tmp</listen>\n"
                   "  <auth>EXTERNAL</auth>\n"
                   "  <policy context=\"default\">\n"
                   "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
                   "    <allow eavesdrop=\"true\"/>\n"
                   "    <allow own=\"*\"/>\n"
                   "  </policy>\n"
                   "</busconfig>\n");
        file.close();
        // Gone with this process however it ends: a crash or a kill skips the
        // destructor, and would leave a daemon behind for good.
        m_daemon.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
        m_daemon.start(daemon, {QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                                QStringLiteral("--print-address")});
        if (!m_daemon.waitForStarted(5000))
            return false;
        while (!m_daemon.canReadLine() && m_daemon.waitForReadyRead(5000)) {}
        address = QString::fromUtf8(m_daemon.readLine()).trimmed();
        return !address.isEmpty();
    }
    ~PrivateBus()
    {
        m_daemon.terminate();
        m_daemon.waitForFinished(5000);
    }

    QString address;

private:
    QTemporaryDir m_dir;
    QProcess m_daemon;
};

// What the portal sends when a setting changes on the desktop.
struct Change {
    QString ns;
    QString key;
    QVariant value;
};

QDBusMessage settingChanged(const Change &change)
{
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/portal/desktop"),
                                                     QStringLiteral("org.freedesktop.portal.Settings"),
                                                     QStringLiteral("SettingChanged"));
    signal << change.ns << change.key << QVariant::fromValue(QDBusVariant(change.value));
    return signal;
}

// Stands in for xdg-desktop-portal's Settings interface. Its answer is a list,
// not a map, so it can repeat a namespace the way 1.14 does, once per backend.
// Lives on a thread of its own: the code under test blocks the main thread while
// it waits for the answer, and a portal on the same thread could never give one.
class FakePortal : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")

public:
    FakePortal(const QString &connectionName, QList<QPair<QString, QVariantMap>> answer, int delayMs,
               QList<Change> afterAnswer)
        : m_connectionName(connectionName), m_answer(std::move(answer)), m_delayMs(delayMs),
          m_afterAnswer(std::move(afterAnswer)) {}

public slots:
    void ReadAll(const QStringList &namespaces, const QDBusMessage &message)
    {
        message.setDelayedReply(true);
        QDBusArgument all;
        all.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QVariantMap>());
        for (const auto &[ns, values] : m_answer) {
            // As the real one does: only what was asked for.
            if (!namespaces.isEmpty() && !namespaces.contains(ns))
                continue;
            all.beginMapEntry();
            all << ns << values;
            all.endMapEntry();
        }
        all.endMap();
        const QDBusMessage reply = message.createReply(QVariant::fromValue(all));
        const QString name = m_connectionName;
        const QList<Change> after = m_afterAnswer;
        QTimer::singleShot(m_delayMs, this, [name, reply, after] {
            QDBusConnection connection(name);
            connection.send(reply);
            // A change on the desktop while the answer was on its way.
            for (const Change &change : after)
                connection.send(settingChanged(change));
        });
    }

private:
    QString m_connectionName;
    QList<QPair<QString, QVariantMap>> m_answer;
    int m_delayMs;
    QList<Change> m_afterAnswer;
};

// A private bus with the stand-in portal on it, and a connection for the code
// under test.
struct PortalOnABus {
    PrivateBus bus;
    QThread thread;
    FakePortal *portal = nullptr;
    QString portalName;
    QString clientName;

    bool start(const QList<QPair<QString, QVariantMap>> &answer, int delayMs = 0, bool present = true,
               const QList<Change> &afterAnswer = {})
    {
        static int serial = 0;
        ++serial;
        portalName = QStringLiteral("fake-portal-%1").arg(serial);
        clientName = QStringLiteral("client-%1").arg(serial);
        if (!bus.start())
            return false;
        if (present) {
            QDBusConnection side = QDBusConnection::connectToBus(bus.address, portalName);
            portal = new FakePortal(portalName, answer, delayMs, afterAnswer);
            thread.start();
            portal->moveToThread(&thread);
            if (!side.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), portal,
                                     QDBusConnection::ExportAllSlots)
                || !side.registerService(QStringLiteral("org.freedesktop.portal.Desktop")))
                return false;
        }
        return client().isConnected();
    }
    QDBusConnection client() const { return QDBusConnection::connectToBus(bus.address, clientName); }

    // What the portal sends when a setting changes on the desktop.
    void change(const QString &ns, const QString &key, const QVariant &value) const
    {
        QDBusConnection(portalName).send(settingChanged({ns, key, value}));
    }

    ~PortalOnABus()
    {
        QDBusConnection::disconnectFromBus(clientName);
        QDBusConnection::disconnectFromBus(portalName);
        thread.quit();
        thread.wait();
        delete portal; // its thread has finished: nothing else can touch it now
    }
};

// Stands in for a StatusNotifierWatcher: the one property the code under test
// reads, and nothing else a watcher does. On a thread of its own for the reason
// FakePortal is.
class FakeWatcher : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)

public:
    explicit FakeWatcher(bool host) : m_host(host) {}
    bool hostRegistered() const { return m_host; }

private:
    bool m_host;
};

// A watcher that owns the name and has no such property: one that is not the
// StatusNotifierWatcher it says it is, or an old one.
class FakeWatcherWithoutTheProperty : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
};

// What the stand-in watcher on a private bus is like.
enum class Watcher { None, HostRegistered, NoHost, NoProperty };

// A private bus with a stand-in watcher on it (or none), and a connection for the
// code under test.
struct WatcherOnABus {
    PrivateBus bus;
    QThread thread;
    QObject *watcher = nullptr;
    QString watcherName = QStringLiteral("fake-tray-watcher");
    QString clientName = QStringLiteral("tray-watcher-client");

    bool start(Watcher kind)
    {
        if (!bus.start())
            return false;
        if (kind != Watcher::None) {
            QDBusConnection side = QDBusConnection::connectToBus(bus.address, watcherName);
            if (kind == Watcher::NoProperty)
                watcher = new FakeWatcherWithoutTheProperty;
            else
                watcher = new FakeWatcher(kind == Watcher::HostRegistered);
            thread.start();
            watcher->moveToThread(&thread);
            if (!side.registerObject(QStringLiteral("/StatusNotifierWatcher"), watcher,
                                     QDBusConnection::ExportAllProperties)
                || !side.registerService(QStringLiteral("org.kde.StatusNotifierWatcher")))
                return false;
        }
        return client().isConnected();
    }
    QDBusConnection client() const { return QDBusConnection::connectToBus(bus.address, clientName); }

    // The watcher leaves the bus, as it does when the bar that owned it is gone.
    // Returns once the bus has noticed.
    bool stopWatcher()
    {
        QDBusConnection::disconnectFromBus(watcherName);
        const QDBusConnection connection = client();
        for (int waited = 0; waited < 5000; waited += 20) {
            if (!connection.interface()->isServiceRegistered(QStringLiteral("org.kde.StatusNotifierWatcher")))
                return true;
            QTest::qWait(20);
        }
        return false;
    }

    ~WatcherOnABus()
    {
        QDBusConnection::disconnectFromBus(clientName);
        QDBusConnection::disconnectFromBus(watcherName);
        thread.quit();
        thread.wait();
        delete watcher; // its thread has finished: nothing else can touch it now
    }
};

// A private X server, which starts on a display number of its own choosing and so
// can never be the one the machine is showing. It is connected to by that name and
// never by DISPLAY. Without Xvfb the tests that need it skip.
class PrivateXServer {
public:
    bool start()
    {
        const QString xvfb = QStandardPaths::findExecutable(QStringLiteral("Xvfb"));
        if (xvfb.isEmpty())
            return false;
        m_server.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
        // -displayfd 1: the display number it picked, on its standard output, once it
        // is ready to be connected to.
        m_server.start(xvfb, {QStringLiteral("-displayfd"), QStringLiteral("1"),
                              QStringLiteral("-screen"), QStringLiteral("0"),
                              QStringLiteral("64x64x24"), QStringLiteral("-nolisten"),
                              QStringLiteral("tcp")});
        if (!m_server.waitForStarted(5000))
            return false;
        while (!m_server.canReadLine() && m_server.waitForReadyRead(5000)) {}
        const QString number = QString::fromUtf8(m_server.readLine()).trimmed();
        if (number.isEmpty())
            return false;
        display = QStringLiteral(":") + number;
        return true;
    }
    ~PrivateXServer()
    {
        m_server.terminate();
        m_server.waitForFinished(5000);
    }

    QString display;

private:
    QProcess m_server;
};

// What windowIconified() said, in words a failure can show.
QString saying(const std::optional<bool> &iconified)
{
    return !iconified ? QStringLiteral("cannot tell") : (*iconified ? QStringLiteral("iconified") : QStringLiteral("not iconified"));
}

// What trayHostRegistered() said.
QString sayingHost(const std::optional<bool> &registered)
{
    return !registered ? QStringLiteral("cannot tell") : (*registered ? QStringLiteral("a host") : QStringLiteral("no host"));
}

xcb_atom_t atomOf(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *reply =
            xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, static_cast<uint16_t>(qstrlen(name)), name), nullptr);
    const xcb_atom_t atom = reply ? reply->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    std::free(reply);
    return atom;
}

// Without a dbus-daemon these cannot run. That is a reason to skip on a
// developer's machine, never on CI, where it would quietly drop the only tests of
// the portal code.
#define START_OR_SKIP(setup, ...)                                                          \
    do {                                                                                   \
        if (!(setup).start(__VA_ARGS__)) {                                                 \
            if (qEnvironmentVariableIsSet("CI"))                                           \
                QFAIL("could not start a private dbus-daemon with a stand-in portal");     \
            QSKIP("no dbus-daemon to run a stand-in portal on");                           \
        }                                                                                  \
    } while (false)

} // namespace
#endif

void TestDesktopChrome::theLayoutsDesktopsActuallyShip_data()
{
    QTest::addColumn<QString>("layout");
    QTest::addColumn<QStringList>("left");
    QTest::addColumn<QStringList>("right");

    const QString min = QStringLiteral("minimize");
    const QString max = QStringLiteral("maximize");
    const QString close = QStringLiteral("close");
    // Read off a real Pop!_OS 22.04 — no maximise button, which our window drew
    // anyway and no other window on that desktop has.
    QTest::newRow("Pop!_OS") << QStringLiteral("appmenu:minimize,close")
                             << QStringList{} << QStringList{min, close};
    // Stock GNOME's schema default: close alone.
    QTest::newRow("GNOME default") << QStringLiteral("appmenu:close") << QStringList{}
                                   << QStringList{close};
    // What kde-gtk-config writes for default Plasma.
    QTest::newRow("KDE via kde-gtk-config") << QStringLiteral("icon:minimize,maximize,close")
                                            << QStringList{} << QStringList{min, max, close};
    // The macOS-like arrangement people set in Tweaks.
    QTest::newRow("buttons on the left") << QStringLiteral("close,minimize,maximize:")
                                         << QStringList{close, min, max} << QStringList{};
    QTest::newRow("no buttons") << QStringLiteral(":") << QStringList{} << QStringList{};
    // Things that are not our buttons, and spacing, are dropped rather than
    // misread as one.
    QTest::newRow("spacers and menus") << QStringLiteral("menu,spacer: minimize , appmenu,close ")
                                       << QStringList{} << QStringList{min, close};
}

void TestDesktopChrome::theLayoutsDesktopsActuallyShip()
{
    QFETCH(QString, layout);
    QFETCH(QStringList, left);
    QFETCH(QStringList, right);
    const ButtonLayout parsed = parseButtonLayout(layout);
    QCOMPARE(parsed.left, left);
    QCOMPARE(parsed.right, right);
}

// GTK's rule, not a guess: a layout with no colon is entirely the left side.
void TestDesktopChrome::aLayoutWithNoColonIsAllLeftSide()
{
    const ButtonLayout parsed = parseButtonLayout(QStringLiteral("close,maximize"));
    QCOMPARE(parsed.left, (QStringList{QStringLiteral("close"), QStringLiteral("maximize")}));
    QVERIFY(parsed.right.isEmpty());
}

// Two close buttons would be two close buttons on screen.
void TestDesktopChrome::aButtonNamedTwiceIsKeptWhereItFirstAppears()
{
    const ButtonLayout parsed = parseButtonLayout(QStringLiteral("close,close:close,minimize"));
    QCOMPARE(parsed.left, QStringList{QStringLiteral("close")});
    QCOMPARE(parsed.right, QStringList{QStringLiteral("minimize")});
}

// The portal re-sends settings, and a change announced for nothing re-lays out
// the title bar for nothing.
void TestDesktopChrome::onlyAChangedSettingIsAnnounced()
{
    DesktopChrome desktop;
    QSignalSpy changed(&desktop, &DesktopChrome::changed);
    const QString prefs = QStringLiteral("org.gnome.desktop.wm.preferences");

    desktop.applySetting(prefs, QStringLiteral("button-layout"), QStringLiteral("appmenu:minimize,close"));
    const int afterFirst = changed.count();
    QCOMPARE(desktop.controlsRight(), (QStringList{QStringLiteral("minimize"), QStringLiteral("close")}));

    desktop.applySetting(prefs, QStringLiteral("button-layout"), QStringLiteral("appmenu:minimize,close"));
    QCOMPARE(changed.count(), afterFirst);

    desktop.applySetting(prefs, QStringLiteral("action-double-click-titlebar"), QStringLiteral("minimize"));
    QCOMPARE(desktop.doubleClickAction(), QStringLiteral("minimize"));
    QCOMPARE(changed.count(), afterFirst + 1);
    desktop.applySetting(prefs, QStringLiteral("action-double-click-titlebar"), QStringLiteral("minimize"));
    QCOMPARE(changed.count(), afterFirst + 1); // the same action again is no change

    // A key this does not use is ignored, and says nothing.
    desktop.applySetting(prefs, QStringLiteral("focus-mode"), QStringLiteral("sloppy"));
    QCOMPARE(changed.count(), afterFirst + 1);
}

void TestDesktopChrome::theThemeDecidesBetweenPopAndLibadwaita()
{
#ifndef Q_OS_LINUX
    QSKIP("the GTK theme only chooses the look on Linux; Windows and macOS have their own");
#else
    DesktopChrome desktop;
    const QString iface = QStringLiteral("org.gnome.desktop.interface");
    desktop.applySetting(iface, QStringLiteral("gtk-theme"), QStringLiteral("Pop-dark"));
    QCOMPARE(desktop.controlStyle(), QStringLiteral("pop"));
    desktop.applySetting(iface, QStringLiteral("gtk-theme"), QStringLiteral("Adwaita"));
    QCOMPARE(desktop.controlStyle(), QStringLiteral("adwaita"));
#endif
}

// Before any desktop has said anything — and, on Windows and macOS, for good: no
// setting ever reaches them. On Windows this default is the only thing that picks
// the Windows 11 caption buttons, and CI is the only place it is ever run there.
void TestDesktopChrome::eachPlatformStartsWithItsOwnLook()
{
    const DesktopChrome desktop;
    const QStringList all{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")};
#if defined(Q_OS_WIN)
    QCOMPARE(desktop.controlStyle(), QStringLiteral("windows"));
    QCOMPARE(desktop.controlsRight(), all);
#elif defined(Q_OS_MACOS)
    // AppKit draws the traffic lights; the window draws nothing of its own.
    QCOMPARE(desktop.controlStyle(), QStringLiteral("native"));
    QVERIFY(desktop.controlsRight().isEmpty());
#else
    // What the window looked like before it asked the desktop, so that where no
    // portal answers nothing changes.
    QCOMPARE(desktop.controlStyle(), QStringLiteral("adwaita"));
    QCOMPARE(desktop.controlsRight(), all);
#endif
    QVERIFY(desktop.controlsLeft().isEmpty());
    QCOMPARE(desktop.doubleClickAction(), QStringLiteral("toggle-maximize"));
    // Off Linux this never has anything to say. Qt asks Windows and macOS itself,
    // and where it cannot tell either — Windows high contrast — the answer the app
    // gave before, light, must stand.
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);
#ifndef Q_OS_LINUX
    // And following the desktop is nothing at all there.
    DesktopChrome followed;
    QSignalSpy changed(&followed, &DesktopChrome::changed);
    followed.followDesktop();
    QCOMPARE(changed.count(), 0);
    QCOMPARE(followed.colorScheme(), Qt::ColorScheme::Unknown);
#endif
}

// org.freedesktop.appearance's color-scheme: 0 no preference, 1 dark, 2 light.
void TestDesktopChrome::theDesktopsDarkPreferenceIsRead()
{
    DesktopChrome desktop;
    QSignalSpy changed(&desktop, &DesktopChrome::changed);
    const QString appearance = QStringLiteral("org.freedesktop.appearance");
    const QString key = QStringLiteral("color-scheme");

    desktop.applySetting(appearance, key, QVariant(1u));
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Dark);
    QCOMPARE(changed.count(), 1);
    desktop.applySetting(appearance, key, QVariant(1u));
    QCOMPARE(changed.count(), 1); // re-sent, not changed

    desktop.applySetting(appearance, key, QVariant(2u));
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Light);
    desktop.applySetting(appearance, key, QVariant(0u));
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown); // no preference is not light

    // Values this was not written for are not a reason to guess.
    desktop.applySetting(appearance, key, QVariant(1u));
    desktop.applySetting(appearance, key, QVariant(7u));
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);
    desktop.applySetting(appearance, key, QVariant(1u));
    desktop.applySetting(appearance, key, QVariant(QStringLiteral("dark")));
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);

    // Only that key of only that namespace.
    desktop.applySetting(appearance, QStringLiteral("accent-color"), QVariant(1u));
    desktop.applySetting(QStringLiteral("org.gnome.desktop.interface"), key, QVariant(1u));
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);
}

#ifdef FT_TEST_PORTAL
// Everything is read before the call returns — before the window's first frame,
// in the app — and no event loop has to run for it.
void TestDesktopChrome::thePortalIsReadBeforeTheFirstFrame()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {{kAppearance, {{QStringLiteral("color-scheme"), 1u}}},
                          {kWm, {{QStringLiteral("button-layout"), QStringLiteral("appmenu:minimize,close")},
                                 {QStringLiteral("action-double-click-titlebar"), QStringLiteral("minimize")}}},
                          {kInterface, {{QStringLiteral("gtk-theme"), QStringLiteral("Pop-dark")}}}});
    DesktopChrome desktop;
    freetunnel::watchPortalSettings(&desktop, setup.client());
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Dark);
    QCOMPARE(desktop.controlsRight(), (QStringList{QStringLiteral("minimize"), QStringLiteral("close")}));
    QCOMPARE(desktop.doubleClickAction(), QStringLiteral("minimize"));
    QCOMPARE(desktop.controlStyle(), QStringLiteral("pop"));
}

// xdg-desktop-portal 1.14, as on Pop!_OS 22.04, answers ReadAll once per backend,
// so every namespace arrives twice. The first is the one its own Read returns.
void TestDesktopChrome::theFirstAnswerForASettingIsTheOneKept()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {{kAppearance, {{QStringLiteral("color-scheme"), 1u}}},
                          {kWm, {{QStringLiteral("button-layout"), QStringLiteral("appmenu:close")}}},
                          {kAppearance, {{QStringLiteral("color-scheme"), 2u}}},
                          {kWm, {{QStringLiteral("button-layout"), QStringLiteral("close,minimize:")},
                                 {QStringLiteral("action-right-click-titlebar"), QStringLiteral("none")}}}});
    DesktopChrome desktop;
    freetunnel::watchPortalSettings(&desktop, setup.client());
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Dark);
    QCOMPARE(desktop.controlsRight(), QStringList{QStringLiteral("close")});
    QVERIFY(desktop.controlsLeft().isEmpty());
    // A key only the second backend gave is still taken: first per setting, not
    // first per namespace.
    QCOMPARE(desktop.rightClickAction(), QStringLiteral("none"));
}

// Switching the desktop to dark, or back, while the app is open.
void TestDesktopChrome::aChangeOnTheDesktopIsFollowed()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {{kAppearance, {{QStringLiteral("color-scheme"), 2u}}}});
    DesktopChrome desktop;
    freetunnel::watchPortalSettings(&desktop, setup.client());
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Light);

    setup.change(kAppearance, QStringLiteral("color-scheme"), 1u);
    QTRY_COMPARE(desktop.colorScheme(), Qt::ColorScheme::Dark);
    setup.change(kWm, QStringLiteral("button-layout"), QStringLiteral("close:"));
    QTRY_COMPARE(desktop.controlsLeft(), QStringList{QStringLiteral("close")});
    setup.change(kAppearance, QStringLiteral("color-scheme"), 0u);
    QTRY_COMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);
}

// A desktop without the portal — a bare window manager — keeps the window as it
// always was, and does not make it wait to find that out.
void TestDesktopChrome::withNoPortalNothingChangesAndNothingWaits()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {}, 0, false);
    DesktopChrome desktop;
    QSignalSpy changed(&desktop, &DesktopChrome::changed);
    QElapsedTimer clock;
    clock.start();
    freetunnel::watchPortalSettings(&desktop, setup.client());
    QVERIFY2(clock.elapsed() < freetunnel::kPortalReadTimeoutMs / 2, "an absent portal is not waited for");
    QCoreApplication::processEvents();
    QCOMPARE(changed.count(), 0);
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);
}

// A portal that takes longer than the limit holds the window no longer than the
// limit — and what it says still arrives, afterwards.
void TestDesktopChrome::aSlowPortalDoesNotHoldTheWindowButIsStillHeard()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {{kAppearance, {{QStringLiteral("color-scheme"), 1u}}}},
                  freetunnel::kPortalReadTimeoutMs + 500);
    DesktopChrome desktop;
    QElapsedTimer clock;
    clock.start();
    freetunnel::watchPortalSettings(&desktop, setup.client());
    const qint64 waited = clock.elapsed();
    // Nine tenths, not the whole: Qt's coarse timers may end the wait up to five
    // per cent early. A read that did not wait at all returns in a millisecond.
    QVERIFY2(waited >= freetunnel::kPortalReadTimeoutMs * 9 / 10, "the first read does wait, up to the limit");
    QVERIFY2(waited < freetunnel::kPortalReadTimeoutMs + 400, "and no longer");
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Unknown);
    QTRY_COMPARE_WITH_TIMEOUT(desktop.colorScheme(), Qt::ColorScheme::Dark, 5000);
}
#endif

#ifdef FT_TEST_PORTAL
// Following starts before the first read, so a change announced while the read's
// answer is on its way is heard after it rather than lost: here the portal
// answers light and at once says the desktop has gone dark.
void TestDesktopChrome::aChangeRightAfterTheFirstReadIsNotLost()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {{kAppearance, {{QStringLiteral("color-scheme"), 2u}}}}, 0, true,
                  {{kAppearance, QStringLiteral("color-scheme"), 1u}});
    DesktopChrome desktop;
    freetunnel::watchPortalSettings(&desktop, setup.client());
    QTRY_COMPARE(desktop.colorScheme(), Qt::ColorScheme::Dark);
}

// Qt asks whether a tray host is on the bus once, when the tray icon is made. A
// panel that comes up after the app — at login, or the AppIndicator extension
// turned on later — has to be noticed, or the icon never appears.
void TestDesktopChrome::aTrayHostThatArrivesLateIsReported()
{
    PrivateBus bus;
    if (!bus.start()) {
        if (qEnvironmentVariableIsSet("CI"))
            QFAIL("could not start a private dbus-daemon");
        QSKIP("no dbus-daemon to run a stand-in tray host on");
    }
    const QString clientName = QStringLiteral("tray-client");
    const QString hostName = QStringLiteral("tray-host");
    {
        DesktopChrome desktop;
        QSignalSpy appeared(&desktop, &DesktopChrome::trayHostAppeared);
        freetunnel::watchTrayHost(&desktop, QDBusConnection::connectToBus(bus.address, clientName));
        QCOMPARE(appeared.count(), 0);

        QDBusConnection host = QDBusConnection::connectToBus(bus.address, hostName);
        QVERIFY(host.registerService(QStringLiteral("org.kde.StatusNotifierWatcher")));
        QTRY_COMPARE(appeared.count(), 1);
    }
    QDBusConnection::disconnectFromBus(hostName);
    QDBusConnection::disconnectFromBus(clientName);
}

// Qt's tray.available is that a StatusNotifierWatcher was on the bus when the icon
// was made, and a bar can own one and show no icons. Whether a host is behind it is
// the watcher's own IsStatusNotifierHostRegistered, which is asked. No watcher on
// the bus is an answer too, and it is no. Only a question that was not answered
// leaves it open.
void TestDesktopChrome::theTrayHostIsAskedNotAssumed()
{
    struct Case {
        const char *name;
        Watcher watcher;
        std::optional<bool> expected; // empty: cannot tell
    };
    static const Case cases[] = {
        {"a host is registered", Watcher::HostRegistered, true},
        {"a watcher with no host", Watcher::NoHost, false},
        {"no watcher", Watcher::None, false},
        {"a watcher without the property", Watcher::NoProperty, std::nullopt},
    };
    for (const Case &testCase : cases) {
        WatcherOnABus setup;
        if (!setup.start(testCase.watcher)) {
            if (qEnvironmentVariableIsSet("CI"))
                QFAIL("could not start a private dbus-daemon with a stand-in tray watcher");
            QSKIP("no dbus-daemon to run a stand-in tray watcher on");
        }
        QElapsedTimer clock;
        clock.start();
        const std::optional<bool> answer = freetunnel::trayHostRegistered(setup.client());
        QVERIFY2(clock.elapsed() < freetunnel::kTrayHostTimeoutMs + 400,
                 qPrintable(QStringLiteral("%1: it took %2 ms")
                                    .arg(QString::fromLatin1(testCase.name)).arg(clock.elapsed())));
        QVERIFY2(sayingHost(answer) == sayingHost(testCase.expected),
                 qPrintable(QStringLiteral("%1: %2, not %3").arg(QString::fromLatin1(testCase.name),
                                                                  sayingHost(answer),
                                                                  sayingHost(testCase.expected))));
    }
    // And no bus at all: nothing was asked.
    QVERIFY(!freetunnel::trayHostRegistered(QDBusConnection(QStringLiteral("no-such-connection"))).has_value());
}

// The bar that made the tray icon is gone (it crashed, or a key bound to restart it
// ran `pkill waybar || waybar`). The icon still says `available`, because Qt
// asked once; the window hidden to it could not be brought back, and the only way
// out would be a second launch. So the close that would have hidden it quits, as
// with no tray at all.
void TestDesktopChrome::aBarThatLeftTheBusIsNoPlaceToHideTheWindow()
{
    WatcherOnABus setup;
    if (!setup.start(Watcher::HostRegistered)) {
        if (qEnvironmentVariableIsSet("CI"))
            QFAIL("could not start a private dbus-daemon with a stand-in tray watcher");
        QSKIP("no dbus-daemon to run a stand-in tray watcher on");
    }
    const QDBusConnection client = setup.client();
    DesktopChrome chrome;
    chrome.setTrayHostProbe([client] { return freetunnel::trayHostRegistered(client); });
    QObject icon;
    icon.setProperty("available", true); // what Qt said when the icon was made

    QVERIFY2(chrome.trayCanHoldWindow(&icon), "a bar with a host is a place to hide the window");
    QVERIFY(setup.stopWatcher());
    QVERIFY2(chrome.hasTray(&icon), "the icon still says it has a tray");
    QVERIFY2(!chrome.trayCanHoldWindow(&icon), "but with the bar gone there is nowhere to hide the window");
}

// A window manager that minimises says so twice, ICCCM's way (WM_STATE) and EWMH's
// (_NET_WM_STATE_HIDDEN), and one that is asked to and does not says neither. The
// properties are set here by hand on a private X server, which stands in for both
// kinds, down to what Hyprland does: it rewrites _NET_WM_STATE on every change of
// focus and lists HIDDEN as supported without ever setting it.
void TestDesktopChrome::aWindowIsIconifiedWhenItsWindowManagerSaysSo()
{
    PrivateXServer server;
    if (!server.start())
        QSKIP("no Xvfb to run a private X server on");
    xcb_connection_t *c = xcb_connect(server.display.toUtf8().constData(), nullptr);
    const auto disconnect = qScopeGuard([c] { xcb_disconnect(c); });
    QVERIFY2(xcb_connection_has_error(c) == 0, "could not connect to the private X server");
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    const xcb_window_t window = xcb_generate_id(c);
    xcb_create_window(c, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 10, 10, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    const xcb_atom_t wmState = atomOf(c, "WM_STATE");
    const xcb_atom_t netState = atomOf(c, "_NET_WM_STATE");
    const xcb_atom_t hidden = atomOf(c, "_NET_WM_STATE_HIDDEN");
    const xcb_atom_t focused = atomOf(c, "_NET_WM_STATE_FOCUSED");
    QVERIFY(wmState != XCB_ATOM_NONE && netState != XCB_ATOM_NONE && hidden != XCB_ATOM_NONE);

    const auto setWmState = [&](uint32_t state) {
        const uint32_t value[2] = {state, XCB_WINDOW_NONE};
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, wmState, wmState, 32, 2, value);
    };
    const auto setNetState = [&](std::initializer_list<xcb_atom_t> atoms) {
        const std::vector<xcb_atom_t> value(atoms);
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, netState, XCB_ATOM_ATOM, 32,
                            static_cast<uint32_t>(value.size()), value.data());
    };
    const auto now = [&] { return saying(freetunnel::windowIconified(c, window)); };

    QCOMPARE(now(), QStringLiteral("not iconified")); // nobody has touched it
    setWmState(1);                                    // NormalState
    QCOMPARE(now(), QStringLiteral("not iconified"));
    setWmState(3);                                    // IconicState
    QCOMPARE(now(), QStringLiteral("iconified"));

    setWmState(1);
    setNetState({focused}); // what Hyprland writes whenever focus moves
    QCOMPARE(now(), QStringLiteral("not iconified"));
    setNetState({focused, hidden});
    QCOMPARE(now(), QStringLiteral("iconified")); // EWMH alone is enough
    setNetState({});
    QCOMPARE(now(), QStringLiteral("not iconified"));

    // Nothing to ask about is not an answer of "no".
    QCOMPARE(saying(freetunnel::windowIconified(c, xcb_generate_id(c))), QStringLiteral("cannot tell"));
    QCOMPARE(saying(freetunnel::windowIconified(nullptr, window)), QStringLiteral("cannot tell"));
}

// A window that has been unmapped is withdrawn by its window manager before it is
// mapped again (ICCCM 4.1.4): WM_STATE goes to WithdrawnState, or the property is
// removed, which is what Hyprland does. One it still manages says Normal or
// Iconic. Set by hand on a private X server, as above.
void TestDesktopChrome::aWindowIsWithdrawnWhenItsWindowManagerSaysSoOrHasNeverSeenIt()
{
    PrivateXServer server;
    if (!server.start())
        QSKIP("no Xvfb to run a private X server on");
    xcb_connection_t *c = xcb_connect(server.display.toUtf8().constData(), nullptr);
    const auto disconnect = qScopeGuard([c] { xcb_disconnect(c); });
    QVERIFY2(xcb_connection_has_error(c) == 0, "could not connect to the private X server");
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    const xcb_window_t window = xcb_generate_id(c);
    xcb_create_window(c, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 10, 10, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    const xcb_atom_t wmState = atomOf(c, "WM_STATE");
    QVERIFY(wmState != XCB_ATOM_NONE);

    const auto setWmState = [&](uint32_t state) {
        const uint32_t value[2] = {state, XCB_WINDOW_NONE};
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, wmState, wmState, 32, 2, value);
    };
    const auto words = [](const std::optional<bool> &withdrawn) {
        return !withdrawn ? QStringLiteral("cannot tell")
                          : (*withdrawn ? QStringLiteral("withdrawn") : QStringLiteral("not withdrawn"));
    };
    const auto now = [&] { return words(freetunnel::windowWithdrawn(c, window)); };

    QCOMPARE(now(), QStringLiteral("withdrawn")); // no window manager has seen it
    setWmState(1);                                // NormalState
    QCOMPARE(now(), QStringLiteral("not withdrawn"));
    setWmState(0);                                // WithdrawnState
    QCOMPARE(now(), QStringLiteral("withdrawn"));
    setWmState(3);                                // IconicState: still managed, and on the screen as an icon
    QCOMPARE(now(), QStringLiteral("not withdrawn"));
    xcb_delete_property(c, window, wmState);      // what Hyprland does when the window is unmapped
    QCOMPARE(now(), QStringLiteral("withdrawn"));

    // Nothing to ask about is not an answer of "gone".
    QCOMPARE(words(freetunnel::windowWithdrawn(c, xcb_generate_id(c))), QStringLiteral("cannot tell"));
    QCOMPARE(words(freetunnel::windowWithdrawn(nullptr, window)), QStringLiteral("cannot tell"));
}

// What the app itself calls: followDesktop(), on the session bus.
void TestDesktopChrome::followingTheDesktopReadsTheSessionBus()
{
    PortalOnABus setup;
    START_OR_SKIP(setup, {{kAppearance, {{QStringLiteral("color-scheme"), 1u}}}});
    // The session bus is this process's for good once anything connects to it, so
    // it is pointed at the private one first; nothing earlier in this file uses it.
    const QByteArray saved = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    qputenv("DBUS_SESSION_BUS_ADDRESS", setup.bus.address.toUtf8());
    DesktopChrome desktop;
    desktop.followDesktop();
    qputenv("DBUS_SESSION_BUS_ADDRESS", saved);

    QCOMPARE(QDBusConnection::sessionBus().interface()->serviceOwner(
                     QStringLiteral("org.freedesktop.portal.Desktop")).value(),
             QDBusConnection(setup.portalName).baseService()); // the stand-in, not the machine's
    QCOMPARE(desktop.colorScheme(), Qt::ColorScheme::Dark); // read before followDesktop() returned
}
#endif

QTEST_GUILESS_MAIN(TestDesktopChrome)
#include "test_desktopchrome.moc"
