// cppcheck-suppress-file missingIncludeSystem
// State-machine tests for QtTrustTunnelClient against the mock core in
// tests/mock_core. The client runs on a dedicated worker thread, exactly like
// the elevated helper hosts it in production (vpn_helper_server.cpp), so the
// timer thread-affinity and cross-thread command handling are exercised too.
#include <QtTest>

#include <QElapsedTimer>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTcpServer>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#endif

#include <QPointer>
#include <QProcess>
#include <QSignalSpy>
#include <QThread>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

#include "mock_core_controller.h"
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_platform.h"

using State = QtTrustTunnelClient::State;

namespace {
constexpr int kLongWaitMs = 20000;
// Set in the copy of this test binary that a test starts to get a process in
// which no session has run yet.
constexpr char kFreshProcessEnv[] = "FT_TEST_FRESH_PROCESS";
} // namespace

class TestQtTrustTunnelClient : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("FT_TEST_SKIP_PRIVILEGE_CHECK", "1");
        // Shrink the watchdog/join intervals (10 s / 30 s / 15 s in production)
        // so the recovery paths run within test timeouts.
        qputenv("FT_TEST_STUCK_JOIN_MS", "400");
        qputenv("FT_TEST_NETWORK_WAIT_MS", "400");
        qputenv("FT_TEST_FD_WATCHDOG_MS", "300");
        qRegisterMetaType<QtTrustTunnelClient::State>();
        // The core log tests write the real default core log file; keep it out
        // of the developer's own data folder. And put a name in its path that no
        // ANSI code page has, as a profile folder named in Cyrillic does: on
        // Windows a file opened by such a path with plain fopen() is not found.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(
                QStringLiteral("test_qt_trusttunnel_client-тест"));
    }

    void init()
    {
        mockcore::Controller::instance().reset();
        m_lastState = State::Disconnected;
        m_errors.clear();

        m_thread = new QThread(this);
        m_client = new QtTrustTunnelClient();
        m_client->setSessionLogging(false); // no core log tail in tests
        m_client->setReconnectBoundsMs(250, 250);      // fast retries
        connect(m_client, &QtTrustTunnelClient::stateChanged, this,
                [this](State s) { m_lastState = s; });
        connect(m_client, &QtTrustTunnelClient::vpnError, this,
                [this](const QString &e) { m_errors << e; });
        m_client->moveToThread(m_thread);
        m_thread->start();
    }

    void cleanup()
    {
        mockcore::Controller::instance().releaseConnect(); // unstick any blocked connect
        QPointer<QtTrustTunnelClient> guard(m_client);
        QMetaObject::invokeMethod(m_client, "deleteLater", Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(guard.isNull(), kLongWaitMs);
        m_client = nullptr;
        m_thread->quit();
        QVERIFY(m_thread->wait(kLongWaitMs));
        delete m_thread;
        m_thread = nullptr;
    }

    void connectReachesConnectedAndDisconnects();
    void anAppRuleTakesItsOwnConnectionOutOfTheTunnel();
    void selectiveModeSendsAMatchedAppTheOtherWay();
    void withNoAppRulesNothingIsForcedAndNothingIsLookedUp();
    void aBurstPastTheLookupBudgetIsKeptInTheTunnel();
    void staleEventFromPreviousSessionIsIgnored();
    void failedAttemptSchedulesWorkingRetry();
    void coreDropTriggersAutoReconnect();
    void disconnectWhileConnectBlockedStaysClean();
    void disconnectWhileConnectStuckAbandonsAttempt();
    void networkWaitTimeoutForcesReconnect();
    void fdWatchdogForcesReconnect();
    void fdWatchdogIgnoresTrafficThatComesBackDown();
    void killSwitchKeepsTheClientAliveWhileWaitingForNetwork();
    void malformedConfigReportsErrorAndDoesNotConnect();
    void structurallyInvalidConfigReportsError();
    void logLevelIsReadBackFromConfig();
    void killSwitchAndVpnModeReachTheCoreConfig();
    void splitRoutesAndExclusionsReachTheCoreConfig();
    void keysThatMakeRootActOnANameNeverReachTheCore();
    void aSocksListenerIsRefused();
    void theCoreIsHandedAServerCertificateVerifier();
    void coreLinesReachTheLogWhileTheSessionRuns();
    void aCoreLineAfterTheSessionEndsNeverUsesAClosedFile();
    void loggingOffForTheNextSessionWritesNowhere();
    void loggingOffFromTheFirstSessionWritesNowhere();

private:
    static bool listContains(const std::vector<std::string> &items, const char *needle)
    {
        return std::find(items.cbegin(), items.cend(), std::string(needle)) != items.cend();
    }

    // A minimally realistic config: the mock toml parser and build_config now
    // behave like the real ones, so a config has to actually look like one.
    static QString validConfigToml(const QString &logLevel = QStringLiteral("warn"))
    {
        return QStringLiteral("loglevel = \"%1\"\n"
                              "[endpoint]\n"
                              "hostname = \"vpn.example\"\n")
                .arg(logLevel);
    }

    void beginConnect(const QString &toml = validConfigToml())
    {
        QMetaObject::invokeMethod(m_client, "beginConnect", Qt::QueuedConnection,
                                  Q_ARG(QString, toml));
    }

    void requestDisconnect()
    {
        QMetaObject::invokeMethod(m_client, "disconnectVpn", Qt::QueuedConnection);
    }

    // Queued like every other command, so it lands before a beginConnect sent
    // after it — the order the helper server sends the two in.
    void setSessionLogging(bool enabled)
    {
        QMetaObject::invokeMethod(m_client, "setSessionLogging", Qt::QueuedConnection,
                                  Q_ARG(bool, enabled));
    }

    void connectAndSettle()
    {
        auto &ctl = mockcore::Controller::instance();
        const int before = ctl.connectCallCount();
        beginConnect();
        QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() > before, kLongWaitMs);
        ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
        QTRY_COMPARE(m_lastState, State::Connected);
    }

    static bool anyLineContains(const QSignalSpy &spy, const QString &text)
    {
        return std::any_of(spy.cbegin(), spy.cend(), [&text](const QList<QVariant> &args) {
            return args.at(0).toString().contains(text);
        });
    }

    static QByteArray coreLogFileContents()
    {
        QFile f(qt_trusttunnel_default_core_log_path());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    QThread *m_thread = nullptr;
    QtTrustTunnelClient *m_client = nullptr;
    State m_lastState = State::Disconnected;
    QStringList m_errors;
};

void TestQtTrustTunnelClient::connectReachesConnectedAndDisconnects()
{
    auto &ctl = mockcore::Controller::instance();
    QSignalSpy connectedSpy(m_client, &QtTrustTunnelClient::vpnConnected);
    QSignalSpy disconnectedSpy(m_client, &QtTrustTunnelClient::vpnDisconnected);

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();

    ctl.fireStateChanged(id, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
    QCOMPARE(connectedSpy.count(), 1);

    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    QCOMPARE(disconnectedSpy.count(), 1);
    QTRY_VERIFY(!ctl.clientAlive(id));
    QVERIFY(ctl.disconnectCalls(id) >= 1);

    // A user-initiated disconnect must settle on Disconnected — no late flip
    // to Error/Reconnecting from stray callbacks or timers.
    QTest::qWait(600);
    QCOMPARE(m_lastState, State::Disconnected);
}

// End to end through the real decision path: this test binary opens a real
// socket, names itself in a rule, and the core asks what to do with that exact
// connection. Nothing here is stubbed except the core itself — the rule
// matching and the process lookup are the shipping ones.
void TestQtTrustTunnelClient::anAppRuleTakesItsOwnConnectionOutOfTheTunnel()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    m_client->setVpnMode(false); // general: a listed app leaves the tunnel
    m_client->setAppRules({QFileInfo(QCoreApplication::applicationFilePath()).fileName()});

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();

    ag::VpnConnectRequestSnapshot req;
    req.id = 1;
    req.proto = IPPROTO_TCP;
    req.family = AF_INET;
    req.src_port = server.serverPort();
    req.src_ip = "127.0.0.1";

    const ag::VpnConnectDecision decision = ctl.fireConnectRequest(id, req);
    QCOMPARE(decision.action, ag::VPN_CA_FORCE_BYPASS);
    // And the program's name must NOT be handed back to the core. The core
    // forwards it to the upstream, which puts it in the CONNECT request sent to
    // the VPN endpoint — so naming it here would tell the operator which
    // application opened every connection. It was set once; this is what keeps
    // it from coming back.
    QVERIFY2(decision.app_name.empty(),
             "the application name must not reach the VPN endpoint");
}

// The same list must mean the opposite thing in the other mode, exactly as the
// route and domain lists already do.
void TestQtTrustTunnelClient::selectiveModeSendsAMatchedAppTheOtherWay()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    m_client->setVpnMode(true);
    m_client->setAppRules({QFileInfo(QCoreApplication::applicationFilePath()).fileName()});

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();

    ag::VpnConnectRequestSnapshot req;
    req.id = 2;
    req.proto = IPPROTO_TCP;
    req.family = AF_INET;
    req.src_port = server.serverPort();
    req.src_ip = "127.0.0.1";

    QCOMPARE(ctl.fireConnectRequest(id, req).action, ag::VPN_CA_FORCE_REDIRECT);
}

// With the feature unused, every connection must take exactly the path it took
// before it existed — and must not pay for a process lookup to find that out.
void TestQtTrustTunnelClient::withNoAppRulesNothingIsForcedAndNothingIsLookedUp()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();

    ag::VpnConnectRequestSnapshot req;
    req.id = 3;
    req.proto = IPPROTO_TCP;
    req.family = AF_INET;
    req.src_port = server.serverPort();
    req.src_ip = "127.0.0.1";

    const ag::VpnConnectDecision decision = ctl.fireConnectRequest(id, req);
    QCOMPARE(decision.action, ag::VPN_CA_DEFAULT);
    // No rules, no lookup, so nothing to name either.
    QVERIFY(decision.app_name.empty());
}

void TestQtTrustTunnelClient::staleEventFromPreviousSessionIsIgnored()
{
    auto &ctl = mockcore::Controller::instance();

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 firstId = ctl.lastClientId();
    ctl.fireStateChanged(firstId, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    // Config switch: tear down the first session, bring up a second one.
    beginConnect();
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);
    const quint64 secondId = ctl.lastClientId();
    QVERIFY(secondId != firstId);
    ctl.fireStateChanged(secondId, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    // A late DISCONNECTED from the torn-down session must not touch the new
    // one (before session tagging this scheduled a bogus reconnect).
    ctl.fireStateChanged(firstId, ag::VPN_SS_DISCONNECTED, ag::VPN_EC_ERROR, "stale event");
    QTest::qWait(600);
    QCOMPARE(m_lastState, State::Connected);
}

void TestQtTrustTunnelClient::failedAttemptSchedulesWorkingRetry()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setConnectError("mock: connection refused");

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QTRY_COMPARE(m_lastState, State::Reconnecting);

    // The backoff timer must actually fire on the client's thread and launch a
    // second attempt (regression: unparented timers never started once the
    // client was moved to a worker thread).
    ctl.setConnectError("");
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);

    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
}

void TestQtTrustTunnelClient::coreDropTriggersAutoReconnect()
{
    auto &ctl = mockcore::Controller::instance();

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    // The core reports the session died — the wrapper must retry on its own.
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_DISCONNECTED, ag::VPN_EC_ERROR,
                         "server closed the tunnel");
    QTRY_COMPARE(m_lastState, State::Reconnecting);
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);

    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
}

void TestQtTrustTunnelClient::disconnectWhileConnectBlockedStaysClean()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setBlockConnect(true);

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1); // worker thread is now inside connect()
    const quint64 probeId = ctl.lastClientId();

    // The user hits disconnect while the native connect is stuck. The command
    // must still be processed (the client's event loop is not blocked by the
    // attempt) and the session must settle on Disconnected once the native
    // call returns — with no spurious Error from the aborted attempt.
    requestDisconnect();
    QTest::qWait(100);
    ctl.releaseConnect();

    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    QTest::qWait(600);
    QCOMPARE(m_lastState, State::Disconnected);
    QVERIFY2(m_errors.filter(QStringLiteral("connect() failed")).isEmpty(),
             qPrintable(m_errors.join(QStringLiteral("; "))));
    // A connect that succeeds while the user is disconnecting must still be
    // brought DOWN, not merely dropped: the core had already installed the tun
    // device, the routes and the DNS override, and letting the object fall out of
    // scope leaves all of that in place behind a "Disconnected" UI.
    QTRY_VERIFY_WITH_TIMEOUT(ctl.disconnectCalls(probeId) >= 1, kLongWaitMs);
}

void TestQtTrustTunnelClient::disconnectWhileConnectStuckAbandonsAttempt()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setBlockConnect(true);

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 firstId = ctl.lastClientId();

    // The native connect never returns within the join window (400 ms in
    // tests). disconnectVpn must ABANDON the stuck attempt — no terminate(),
    // no indefinite hang — and settle on Disconnected.
    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);

    // Let the zombie thread's native call return: the stale attempt must drop
    // its result, and the abandoned core client must be cleaned up.
    ctl.releaseConnect();
    QTRY_VERIFY_WITH_TIMEOUT(!ctl.clientAlive(firstId), kLongWaitMs);
    // Cleaned up means DISCONNECTED, not merely destructed: the abandoned attempt
    // had finished connecting, so the tunnel it installed has to come down.
    QTRY_VERIFY_WITH_TIMEOUT(ctl.disconnectCalls(firstId) >= 1, kLongWaitMs);
    QTest::qWait(300);
    QCOMPARE(m_lastState, State::Disconnected); // stale result really dropped

    // A fresh session on a fresh thread must work after the abandonment.
    beginConnect();
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
}

void TestQtTrustTunnelClient::networkWaitTimeoutForcesReconnect()
{
    auto &ctl = mockcore::Controller::instance();

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    // The core reports no network and never self-recovers; after the wait
    // timeout (400 ms in tests) the wrapper must tear down and reconnect.
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_WAITING_FOR_NETWORK);
    QTRY_COMPARE(m_lastState, State::WaitingForNetwork);
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);

    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
}

// With the kill switch on, the recovery watchdogs must not destroy the core
// client: the block on non-tunnelled traffic lives on that object, so tearing it
// down and then backing off (up to 30 s per round, doubling, forever) leaves the
// machine sending everything in the clear — on the network the user just woke up
// on, which is precisely when they were counting on it. Waiting longer for a core
// that may be wedged is the safe direction, and the state stays
// WaitingForNetwork so the UI keeps saying the tunnel is not up.
void TestQtTrustTunnelClient::killSwitchKeepsTheClientAliveWhileWaitingForNetwork()
{
    auto &ctl = mockcore::Controller::instance();

    QMetaObject::invokeMethod(m_client, "setKillSwitch", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const uint64_t id = ctl.lastClientId();
    ctl.fireStateChanged(id, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_FOR_NETWORK);
    QTRY_COMPARE(m_lastState, State::WaitingForNetwork);

    // Well past the 400 ms test watchdog interval: without the kill switch this is
    // exactly where networkWaitTimeoutForcesReconnect() sees a second connect.
    QTest::qWait(1500);
    QCOMPARE(ctl.connectCallCount(), 1);
    QVERIFY(ctl.clientAlive(id));
    QCOMPARE(ctl.disconnectCalls(id), 0);
    QCOMPARE(m_lastState, State::WaitingForNetwork);

    // The core recovering on its own must still be honoured — the watchdog is
    // suppressed, not the state machine.
    ctl.fireStateChanged(id, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
}

// The regression that made this worth changing. A program routed around the
// tunnel opens its connections directly from this process, so a browser on the
// bypass list holds many sockets here — and the old check, which compared the
// current count to the count at connect, called that a leak and told the user
// their connection was "using an unusual number of system resources". Load that
// comes back down must be left alone.
void TestQtTrustTunnelClient::fdWatchdogIgnoresTrafficThatComesBackDown()
{
#ifdef Q_OS_WIN
    QSKIP("fd counting is not supported on Windows — the watchdog is inert there");
#else
    auto &ctl = mockcore::Controller::instance();

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    struct rlimit rl {};
    QVERIFY(::getrlimit(RLIMIT_NOFILE, &rl) == 0);
    const int threshold = std::clamp(static_cast<int>(rl.rlim_cur) / 4, 256, 1024);

    // Three bursts well past the threshold, each released before the next —
    // exactly the shape of a browser loading pages.
    for (int round = 0; round < 3; ++round) {
        std::vector<int> fds;
        for (int i = 0; i < threshold + 32; ++i) {
            const int fd = ::open("/dev/null", O_RDONLY);
            if (fd >= 0)
                fds.push_back(fd);
        }
        if (static_cast<int>(fds.size()) < threshold + 1) {
            for (const int fd : fds)
                ::close(fd);
            QSKIP("cannot open enough descriptors to make this meaningful here");
        }
        QTest::qWait(400); // at least one watchdog check sees the peak
        for (const int fd : fds)
            ::close(fd);
        QTest::qWait(400); // and at least one sees it gone
    }

    QCOMPARE(ctl.connectCallCount(), 1);
    QCOMPARE(m_lastState, State::Connected);
#endif
}

void TestQtTrustTunnelClient::fdWatchdogForcesReconnect()
{
#ifdef Q_OS_WIN
    QSKIP("fd counting is not supported on Windows — the watchdog is inert there");
#else
    auto &ctl = mockcore::Controller::instance();

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected); // fd baseline recorded here

    // A leak: descriptors taken and never given back, held across the whole
    // window the watchdog looks at. The threshold is scaled to the process's
    // own limit, so it is computed here rather than written down twice.
    struct rlimit rl {};
    QVERIFY(::getrlimit(RLIMIT_NOFILE, &rl) == 0);
    const int threshold = std::clamp(static_cast<int>(rl.rlim_cur) / 4, 256, 1024);
    std::vector<int> fds;
    for (int i = 0; i < threshold + 32; ++i) {
        const int fd = ::open("/dev/null", O_RDONLY);
        if (fd >= 0)
            fds.push_back(fd);
    }
    if (static_cast<int>(fds.size()) < threshold + 1) {
        for (const int fd : fds)
            ::close(fd);
        QSKIP("cannot open enough descriptors to exceed the threshold here");
    }
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);
    for (const int fd : fds)
        ::close(fd);

    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
#endif
}

// A config that isn't TOML must surface as an error, not as a connect attempt.
// This path was untestable while the mock parser could not fail.
void TestQtTrustTunnelClient::malformedConfigReportsErrorAndDoesNotConnect()
{
    auto &ctl = mockcore::Controller::instance();
    const int before = ctl.connectCallCount();

    beginConnect(QStringLiteral("this is not a config\n"));

    QTRY_COMPARE(m_lastState, State::Error);
    QTRY_VERIFY(!m_errors.isEmpty());
    QVERIFY(m_errors.join(QLatin1Char('|')).contains(QStringLiteral("Failed parsing config")));
    QCOMPARE(ctl.connectCallCount(), before);
}

// Valid TOML that isn't a valid config: the core's build_config refuses it.
void TestQtTrustTunnelClient::structurallyInvalidConfigReportsError()
{
    auto &ctl = mockcore::Controller::instance();
    const int before = ctl.connectCallCount();

    beginConnect(QStringLiteral("unrelated = \"value\"\n"));

    QTRY_COMPARE(m_lastState, State::Error);
    QTRY_VERIFY(!m_errors.isEmpty());
    QVERIFY(m_errors.join(QLatin1Char('|'))
                    .contains(QStringLiteral("Invalid TrustTunnel config structure")));
    QCOMPARE(ctl.connectCallCount(), before);
}

// The Verbose-logs toggle works by writing `loglevel` into the config TOML and
// having the wrapper read it back (the core's build_config does not surface it).
// Without this the toggle silently did nothing.
void TestQtTrustTunnelClient::logLevelIsReadBackFromConfig()
{
    auto &ctl = mockcore::Controller::instance();

    ag::Logger::last_level() = ag::LOG_LEVEL_ERROR; // so a no-op would be visible
    beginConnect(validConfigToml(QStringLiteral("info")));
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QTRY_COMPARE(ag::Logger::last_level(), ag::LOG_LEVEL_INFO);

    requestDisconnect();
    QTRY_COMPARE(m_lastState, State::Disconnected);

    beginConnect(validConfigToml(QStringLiteral("warn")));
    QTRY_VERIFY(ctl.connectCallCount() >= 2);
    QTRY_COMPARE(ag::Logger::last_level(), ag::LOG_LEVEL_WARN);
}

// The last link of the kill-switch chain (Backend -> IPC -> helper -> here):
// setKillSwitch/setVpnMode have to land in the config the core is CONSTRUCTED
// with. Nothing downstream of this object can put them back, and nothing in the
// UI can tell that they went missing — the toggle keeps reading ON while every
// session runs without the traffic block. Both directions are asserted, so a
// wrapper that hardcodes a value fails just as loudly as one that drops it.
void TestQtTrustTunnelClient::killSwitchAndVpnModeReachTheCoreConfig()
{
    auto &ctl = mockcore::Controller::instance();

    QMetaObject::invokeMethod(m_client, "setKillSwitch", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));
    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QTRY_VERIFY(ctl.lastCoreConfig().captured);

    mockcore::CoreConfigSnapshot cfg = ctl.lastCoreConfig();
    QVERIFY2(cfg.killswitch_enabled,
             "the kill switch was on, but the core was built with it off — every session "
             "would run without the traffic block while the GUI toggle still reads ON");
    QCOMPARE(cfg.mode, int(ag::VPN_MODE_SELECTIVE));

    // And the OFF direction, from a second session: selective routing left on by
    // accident tunnels only the split list, so everything else leaves in the clear.
    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    QMetaObject::invokeMethod(m_client, "setKillSwitch", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, false));
    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, false));

    beginConnect();
    QTRY_VERIFY_WITH_TIMEOUT(ctl.coreConfigCaptureCount() >= 2, kLongWaitMs);
    cfg = ctl.lastCoreConfig();
    QVERIFY(!cfg.killswitch_enabled);
    QCOMPARE(cfg.mode, int(ag::VPN_MODE_GENERAL));
}

// Same argument for the split-tunnel settings: the routes and the domain
// exclusions decide what leaves the machine outside the tunnel, and until now
// nothing checked that the lists the GUI sends survive as far as the core.
void TestQtTrustTunnelClient::splitRoutesAndExclusionsReachTheCoreConfig()
{
    auto &ctl = mockcore::Controller::instance();

    QMetaObject::invokeMethod(m_client, "setExcludedRouteStrings", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList,
                                    QStringList({QStringLiteral("10.66.0.0/16"),
                                                 QStringLiteral("192.168.7.0/24")})));
    QMetaObject::invokeMethod(m_client, "setExtraExclusionDomains", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList,
                                    QStringList({QStringLiteral("intranet.example"),
                                                 QStringLiteral("printer.local")})));

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QTRY_VERIFY(ctl.lastCoreConfig().captured);

    const mockcore::CoreConfigSnapshot cfg = ctl.lastCoreConfig();
    QVERIFY2(listContains(cfg.excluded_routes, "10.66.0.0/16"),
             "an excluded route the user configured never reached the core");
    QVERIFY(listContains(cfg.excluded_routes, "192.168.7.0/24"));
    const QString exclusions = QString::fromStdString(cfg.exclusions);
    QVERIFY2(exclusions.contains(QStringLiteral("intranet.example")), qPrintable(exclusions));
    QVERIFY2(exclusions.contains(QStringLiteral("printer.local")), qPrintable(exclusions));
}

// This object is the root helper's, and the TOML it is handed keeps every key an
// imported file had. A file can name a directory for the core to delete and write
// files in, ports for the kill switch to let through, and an interface name or a
// network namespace for root to set up — none of which FreeTunnel ever writes.
// They have to be gone by the time the core is built, while the routing the same
// file asked for stays: that is the user's, and the reason to import it.
void TestQtTrustTunnelClient::keysThatMakeRootActOnANameNeverReachTheCore()
{
    auto &ctl = mockcore::Controller::instance();

    beginConnect(QStringLiteral("loglevel = \"warn\"\n"
                                "ssl_session_cache_path = \"/var/tmp/somewhere\"\n"
                                "killswitch_allow_ports = [3389, 5900]\n"
                                "[endpoint]\n"
                                "hostname = \"vpn.example\"\n"
                                "[listener.tun]\n"
                                "device_name = \"named-by-file\"\n"
                                "use_existing = true\n"
                                "netns = \"elsewhere\"\n"
                                "included_routes = [\"10.20.0.0/16\"]\n"));
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QTRY_VERIFY(ctl.lastCoreConfig().captured);

    const mockcore::CoreConfigSnapshot cfg = ctl.lastCoreConfig();
    QVERIFY2(!cfg.ssl_session_storage_path.has_value(),
             "root would empty and refill a directory the config file named");
    QVERIFY2(cfg.killswitch_allow_ports.empty(),
             "the kill switch would let traffic through on ports the config file chose");
    QVERIFY2(cfg.device_name.empty(), "root would set up an interface under a name the file chose");
    QVERIFY(!cfg.use_existing);
    QVERIFY2(!cfg.netns.has_value(), "root would move the tunnel into a namespace the file named");
    QVERIFY2(listContains(cfg.included_routes, "10.20.0.0/16"),
             "the routes the imported file asked for must still reach the core");
}

// FreeTunnel only writes a TUN listener. A SOCKS one is a proxy root would open on
// the address the TOML names, and there would be no tunnel interface for the
// routes, the DNS or the kill switch — while the app reports the VPN as up.
void TestQtTrustTunnelClient::aSocksListenerIsRefused()
{
    auto &ctl = mockcore::Controller::instance();
    const int before = ctl.connectCallCount();

    beginConnect(QStringLiteral("loglevel = \"warn\"\n"
                                "[endpoint]\n"
                                "hostname = \"vpn.example\"\n"
                                "[listener.socks]\n"
                                "address = \"0.0.0.0:1080\"\n"));

    QTRY_COMPARE(m_lastState, State::Error);
    QTRY_VERIFY(!m_errors.isEmpty());
    QVERIFY(m_errors.join(QLatin1Char('|'))
                    .contains(QStringLiteral("Invalid TrustTunnel config structure")));
    QCOMPARE(ctl.connectCallCount(), before);
    QCOMPARE(ctl.coreConfigCaptureCount(), 0);
}

// The core asks the app to vet the server's certificate through the callbacks it
// was handed at construction. If that one assignment goes missing the core is
// left with no verifier, its event->result stays at 0, and every certificate is
// accepted — an on-path attacker terminates the tunnel's TLS and the app reports
// a healthy connection. Assert both that a verifier is installed AT ALL and that
// it is the one that actually refuses a bad chain.
void TestQtTrustTunnelClient::theCoreIsHandedAServerCertificateVerifier()
{
    auto &ctl = mockcore::Controller::instance();

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const uint64_t id = ctl.lastClientId();

    const int accepted = ctl.fireVerifyCertificate(id, "leaf-pem", "chain-pem");
    QVERIFY2(accepted != mockcore::Controller::kNoVerifyHandler,
             "the core was given callbacks with no certificate verifier at all, so it would "
             "accept whatever certificate the peer presents");
    QCOMPARE(accepted, 0);
    QCOMPARE(QString::fromStdString(ctl.lastVerifiedCert()), QStringLiteral("leaf-pem"));

    // Same client, chain now refused: the verdict has to flip. An installed
    // handler that answers 0 regardless is no better than none.
    ctl.setCertError("certificate chain is not trusted");
    QTest::ignoreMessage(
            QtWarningMsg,
            "TrustTunnel certificate verification failed: certificate chain is not trusted");
    QCOMPARE(ctl.fireVerifyCertificate(id, "impostor-pem", "impostor-chain"), -1);
}

QTEST_GUILESS_MAIN(TestQtTrustTunnelClient)
// What happens once the lookup has spent its budget, which a long enough burst
// will do: on this machine a walk costs about 2.8 ms, and a few dozen
// back-to-back connections exhaust the credit that pays for them.
//
// Before this, such a connection was answered exactly like one the walk had
// examined and found unlisted — VPN_CA_DEFAULT — and in "Through VPN" the core's
// default is to leave the tunnel. So a burst from a listed program put its own
// traffic on the open network, which is the one thing the rule was written to
// prevent, and nothing said so. An unexamined connection is kept in the tunnel
// instead: wrong only for a program nobody listed, and wrong there in the
// direction that costs bandwidth rather than privacy.
void TestQtTrustTunnelClient::aBurstPastTheLookupBudgetIsKeptInTheTunnel()
{
    auto &ctl = mockcore::Controller::instance();

    m_client->setVpnMode(true); // "Through VPN": default means OUT of the tunnel
    m_client->setAppRules({QFileInfo(QCoreApplication::applicationFilePath()).fileName()});

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();

    // Ports nobody holds, so every one of them is a miss that buys a fresh walk
    // until there is nothing left to buy one with.
    bool keptIn = false;
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < 4000 && !keptIn && clock.elapsed() < 20000; ++i) {
        ag::VpnConnectRequestSnapshot req;
        req.id = static_cast<std::uint64_t>(100 + i);
        req.proto = IPPROTO_TCP;
        req.family = AF_INET;
        req.src_port = static_cast<std::uint16_t>(20000 + i);
        req.src_ip = "127.0.0.1";
        keptIn = ctl.fireConnectRequest(id, req).action == ag::VPN_CA_FORCE_REDIRECT;
    }
    QVERIFY2(keptIn, "the budget must run out and unexamined connections stay in the tunnel");
}

// The core logs through one callback for the whole process. Left to itself, the
// core client owned the file behind it: it opened the file when given a path,
// pointed the callback at it, closed it when the client was destroyed and left
// the callback pointing at the closed file. The helper is one process with a new
// core client for every session, so core lines went through a closed FILE in a
// root process. These run with logging on, which every other test here turns off.

// A line logged in the middle of a session has to reach the GUI then, not when a
// buffer fills or the session ends: the core's own file was fully buffered, and
// a warning about the connection showed up long after it mattered, or never.
void TestQtTrustTunnelClient::coreLinesReachTheLogWhileTheSessionRuns()
{
    auto &ctl = mockcore::Controller::instance();
    QSignalSpy lines(m_client, &QtTrustTunnelClient::coreLogLine);
    setSessionLogging(true);
    connectAndSettle();

    ctl.coreLog(ag::LOG_LEVEL_WARN, "upstream stopped answering");
    QTRY_VERIFY(anyLineContains(lines, QStringLiteral("upstream stopped answering")));
    QCOMPARE(ctl.coreLogWritesThroughClosedFile(), 0);
}

// The core keeps logging after a session is over — its threads do not ask which
// client set the callback — and that line must not go through the file the
// destroyed client closed.
void TestQtTrustTunnelClient::aCoreLineAfterTheSessionEndsNeverUsesAClosedFile()
{
    auto &ctl = mockcore::Controller::instance();
    setSessionLogging(true);
    connectAndSettle();
    const quint64 id = ctl.lastClientId();

    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    QTRY_VERIFY(!ctl.clientAlive(id));

    ctl.coreLog(ag::LOG_LEVEL_WARN, "late line from a core thread");
    QCOMPARE(ctl.coreLogWritesThroughClosedFile(), 0);
    // Still logging, so it is kept: the file belongs to the helper now, and it
    // stays open until the next session or until logging is switched off.
    QVERIFY(coreLogFileContents().contains("late line from a core thread"));
}

// Switching logging off for the next session left the callback where the
// previous client had put it — on a file closed when that client was destroyed
// — so EVERY line of the new session went through it. Logging off has to mean
// nowhere: not the old file, and not stderr either, which in the helper is a
// pipe or a temp file nobody reads.
void TestQtTrustTunnelClient::loggingOffForTheNextSessionWritesNowhere()
{
    auto &ctl = mockcore::Controller::instance();
    setSessionLogging(true);
    connectAndSettle();
    const quint64 firstId = ctl.lastClientId();

    setSessionLogging(false);
    connectAndSettle(); // a config switch: the first client is retired first
    QTRY_VERIFY(!ctl.clientAlive(firstId));

    ctl.coreLog(ag::LOG_LEVEL_WARN, "said with logging off");
    QCOMPARE(ctl.coreLogWritesThroughClosedFile(), 0);
    QCOMPARE(ctl.coreLogLinesToStderr(), 0);
    QVERIFY(!coreLogFileContents().contains("said with logging off"));
}

// With logging off from the helper's first session there is no closed file, but
// the core's default callback writes to stderr, which in the helper is pkexec's
// pipe to the GUI (never read) or a root-owned temp file on macOS. The sink has
// to be installed for that session too, so its lines go nowhere. Only a process
// in which nothing has replaced the core's callback yet can show that, so this
// runs itself again, alone, in a new process.
void TestQtTrustTunnelClient::loggingOffFromTheFirstSessionWritesNowhere()
{
    if (!qEnvironmentVariableIsSet(kFreshProcessEnv)) {
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QString::fromLatin1(kFreshProcessEnv), QStringLiteral("1"));
        child.setProcessEnvironment(env);
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(),
                    {QString::fromLatin1(QTest::currentTestFunction())});
        QVERIFY2(child.waitForFinished(3 * kLongWaitMs), "the test in a new process did not finish");
        const QByteArray output = child.readAll();
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 output.constData());
        return;
    }

    auto &ctl = mockcore::Controller::instance();
    QVERIFY2(ctl.coreLoggerCallbackSets() == 0,
             "something replaced the core's logger before the first session; this proves nothing");
    connectAndSettle(); // logging off, as init() leaves it

    ctl.coreLog(ag::LOG_LEVEL_WARN, "said with logging off from the start");
    QCOMPARE(ctl.coreLogLinesToStderr(), 0);
    QCOMPARE(ctl.coreLogWritesThroughClosedFile(), 0);
}

#include "test_qt_trusttunnel_client.moc"
