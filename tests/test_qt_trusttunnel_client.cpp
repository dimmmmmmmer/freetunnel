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
#include <QScopeGuard>
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
        // The uplink follower runs on Windows only in the shipping app; here it
        // runs everywhere, so the state machine it drives is tested on every
        // platform rather than only on the one with no developer machine.
        qputenv("FT_TEST_FOLLOW_UPLINK", "1");
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
        // The uplink* tests take their looks at the network one at a time
        // (lookAtUplink()), so that whether two looks agreed is decided by the
        // test and not by the scheduler; the offline* ones time the timer's
        // looks, a second apart; the rest let the timer look every 100 ms.
        const QByteArray test(QTest::currentTestFunction());
        qputenv("FT_TEST_UPLINK_POLL_MS", test.startsWith("uplink")    ? "3600000"
                                          : test.startsWith("offline") ? "1000"
                                                                       : "100");
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
    void aRuleAddedMidSessionDecidesTheNextConnection();
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
    void aSecondRouteListReplacesTheFirst();
    void aConfigsKillSwitchPortsReachTheCoreOnlyWhenTheUserLetsThem();
    void theKillSwitchPortsSettingAppliesToAConfigAlreadyLoaded();
    void theCoreIsHandedAServerCertificateVerifier();
    void coreLinesReachTheLogWhileTheSessionRuns();
    void aCoreLineAfterTheSessionEndsNeverUsesAClosedFile();
    void loggingOffForTheNextSessionWritesNowhere();
    void loggingOffFromTheFirstSessionWritesNowhere();
    void theCoreStartsOnTheAdapterItCallsActive();
    void aSessionFollowsTheNetworkToAnotherAdapter();
    void uplinkLossIsReportedAndSoIsItsReturn();
    void uplinkBlipIsNotAMove();
    void uplinkMoveGoesAheadWhenItsDnsCannotBeRead();
    void uplinkMoveSurvivesTheNextProtect();
    void uplinkOfANewSessionIsTheOneItWasBuiltOn();
    void anAttemptMadeOfflineIsRetried();
    void anAdapterBackRightAfterAnOfflineFailureStillRetries();
    void aPppLinkTheCoreCannotUseIsNamedNotRetried();
    void aDnsFailureWithANetworkStillStops();
    void uplinkOfflineAsksTheCoreOnlyOnceARouteIsBack();
    void offlineLooksKeepThePace();
    void aRuleEditReachesTheRunningSessionWithoutANewOne();
    void anEditThatChangesNothingLeavesTheSessionAlone();
    void splitRoutingChangesTheSessionInOneStep();
    void theCoreHasTheNewModeBeforeTheProgramRulesChange();
    void anEditMadeWhileTheSessionIsBuiltReachesItOnceBuilt();
    void aBurstOfEditsLeavesTheSessionOnTheLastOne();
    void aFirstConnectTheCoreKeepsRetryingStaysConnectingAndSaysWhy();
    void disconnectStopsASessionTheCoreIsStillRetrying();
    void aNewSessionSaysWhyItIsNotConnectingAgain();

private:
    // A connected session whose core was built while `ifIndex` was the active
    // adapter. Fails the test (check QTest::currentTestFailed()) if it is not.
    void connectOnUplink(uint32_t ifIndex, quint64 *id)
    {
        auto &ctl = mockcore::Controller::instance();
        ctl.setActiveUplink(ifIndex);
        beginConnect();
        QTRY_VERIFY(ctl.connectCallCount() >= 1);
        *id = ctl.lastClientId();
        ctl.fireStateChanged(*id, ag::VPN_SS_CONNECTED);
        QTRY_COMPARE(m_lastState, State::Connected);
        QCOMPARE(mockcore::Controller::outboundInterface(), ifIndex);
    }

    // One look at the network, taken on the client's own thread as the timer
    // would take it.
    void lookAtUplink()
    {
        QVERIFY(QMetaObject::invokeMethod(m_client, "followUplink",
                                          Qt::BlockingQueuedConnection));
    }

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

    // Connected, and adopted: the first live edit is how a test knows the client
    // holds the session, since a CONNECTED event can be handled before the
    // finished attempt is. Returns the session's id.
    quint64 connectedWithLiveSession()
    {
        auto &ctl = mockcore::Controller::instance();
        beginConnect();
        if (!QTest::qWaitFor([&ctl]() { return ctl.connectCallCount() >= 1; }, kLongWaitMs))
            return 0;
        const quint64 id = ctl.lastClientId();
        ctl.fireStateChanged(id, ag::VPN_SS_CONNECTED);
        QMetaObject::invokeMethod(m_client, "setExtraExclusionDomains",
                                  Qt::BlockingQueuedConnection,
                                  Q_ARG(QStringList, QStringList({QStringLiteral("seed.example")})));
        const bool live = QTest::qWaitFor(
                [this, &ctl]() {
                    return ctl.exclusionUpdates().size() == 1 && m_lastState == State::Connected;
                },
                kLongWaitMs);
        return live ? id : 0;
    }

    static bool has(const mockcore::ExclusionsUpdate &u, const char *entry)
    {
        return u.exclusions.find(entry) != std::string::npos;
    }

    // A connection from a socket this test holds, so the shipping lookup names
    // this test binary as its program.
    static ag::VpnConnectRequestSnapshot ownConnection(const QTcpServer &server, uint64_t id)
    {
        ag::VpnConnectRequestSnapshot req;
        req.id = id;
        req.proto = IPPROTO_TCP;
        req.family = AF_INET;
        req.src_port = server.serverPort();
        req.src_ip = "127.0.0.1";
        return req;
    }

    static QString ownProgram()
    {
        return QFileInfo(QCoreApplication::applicationFilePath()).fileName();
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

// The GUI no longer rebuilds the tunnel when a program rule changes: it counts on
// this object reading the rules on every connection. Were they ever captured per
// session instead, a rule added while connected would silently do nothing until
// the next reconnect — so the running session is held to it here.
void TestQtTrustTunnelClient::aRuleAddedMidSessionDecidesTheNextConnection()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    m_client->setVpnMode(false);
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();
    ctl.fireStateChanged(id, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);

    ag::VpnConnectRequestSnapshot req;
    req.id = 4;
    req.proto = IPPROTO_TCP;
    req.family = AF_INET;
    req.src_port = server.serverPort();
    req.src_ip = "127.0.0.1";
    QCOMPARE(ctl.fireConnectRequest(id, req).action, ag::VPN_CA_DEFAULT);

    QMetaObject::invokeMethod(
            m_client, "setAppRules", Qt::BlockingQueuedConnection,
            Q_ARG(QStringList,
                  QStringList({QFileInfo(QCoreApplication::applicationFilePath()).fileName()})));
    req.id = 5;
    QCOMPARE(ctl.fireConnectRequest(id, req).action, ag::VPN_CA_FORCE_BYPASS);
    QCOMPARE(ctl.connectCallCount(), 1); // the same session, not a new one
    QVERIFY(ctl.clientAlive(id));
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

// The one key of those the user can hand back to the config: Windows ports the
// kill switch lets through, which someone reaching this machine over Remote
// Desktop with the kill switch on needs. Only while the setting is on, and only
// that key — the setting is about the kill switch, not about trusting the file.
// Both directions, so a wrapper that always keeps the ports fails as loudly as
// one that never does.
void TestQtTrustTunnelClient::aConfigsKillSwitchPortsReachTheCoreOnlyWhenTheUserLetsThem()
{
    auto &ctl = mockcore::Controller::instance();
    const QString toml = QStringLiteral("loglevel = \"warn\"\n"
                                        "ssl_session_cache_path = \"/var/tmp/somewhere\"\n"
                                        "killswitch_allow_ports = [3389, 5900]\n"
                                        "[endpoint]\n"
                                        "hostname = \"vpn.example\"\n"
                                        "[listener.tun]\n"
                                        "device_name = \"named-by-file\"\n"
                                        "use_existing = true\n"
                                        "netns = \"elsewhere\"\n");

    QMetaObject::invokeMethod(m_client, "setKillSwitchPortsFromConfig",
                              Qt::BlockingQueuedConnection, Q_ARG(bool, true));
    beginConnect(toml);
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QTRY_VERIFY(ctl.lastCoreConfig().captured);

    mockcore::CoreConfigSnapshot cfg = ctl.lastCoreConfig();
    QCOMPARE(QString::fromStdString(cfg.killswitch_allow_ports), QStringLiteral("[3389, 5900]"));
    QVERIFY2(!cfg.ssl_session_storage_path.has_value(),
             "letting the config decide the kill switch's ports let it name a directory too");
    QVERIFY(cfg.device_name.empty());
    QVERIFY(!cfg.use_existing);
    QVERIFY(!cfg.netns.has_value());

    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    QMetaObject::invokeMethod(m_client, "setKillSwitchPortsFromConfig",
                              Qt::BlockingQueuedConnection, Q_ARG(bool, false));
    beginConnect(toml);
    QTRY_VERIFY_WITH_TIMEOUT(ctl.coreConfigCaptureCount() >= 2, kLongWaitMs);
    cfg = ctl.lastCoreConfig();
    QVERIFY2(cfg.killswitch_allow_ports.empty(),
             "the setting was turned off, and the config still opened the kill switch");
}

// The setting can arrive after the config it applies to has been read: the ports
// are set aside when the file is, so either order gives the core the same thing.
void TestQtTrustTunnelClient::theKillSwitchPortsSettingAppliesToAConfigAlreadyLoaded()
{
    auto &ctl = mockcore::Controller::instance();
    const QString toml = QStringLiteral("loglevel = \"warn\"\n"
                                        "killswitch_allow_ports = [3389]\n"
                                        "[endpoint]\n"
                                        "hostname = \"vpn.example\"\n");
    const auto loadThenSetThenConnect = [&](bool portsFromConfig) {
        bool loaded = false;
        QMetaObject::invokeMethod(
                m_client, [&]() { loaded = m_client->loadConfigFromToml(toml); },
                Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_client, "setKillSwitchPortsFromConfig",
                                  Qt::BlockingQueuedConnection, Q_ARG(bool, portsFromConfig));
        QMetaObject::invokeMethod(m_client, "connectVpn", Qt::QueuedConnection);
        return loaded;
    };

    QVERIFY(loadThenSetThenConnect(true));
    QTRY_VERIFY(ctl.lastCoreConfig().captured);
    QCOMPARE(QString::fromStdString(ctl.lastCoreConfig().killswitch_allow_ports),
             QStringLiteral("[3389]"));

    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    // Loaded while the setting is still on, so the ports are in the config when
    // it is turned off.
    QVERIFY(loadThenSetThenConnect(false));
    QTRY_VERIFY_WITH_TIMEOUT(ctl.coreConfigCaptureCount() >= 2, kLongWaitMs);
    QVERIFY(ctl.lastCoreConfig().killswitch_allow_ports.empty());
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

// The route list is a setting, like the domain list beside it: a second one
// replaces the first. While a loaded config was waiting to be used, a second list
// was appended to the first, so a route taken off the list still reached the
// core and would have stayed outside the tunnel for the whole session.
void TestQtTrustTunnelClient::aSecondRouteListReplacesTheFirst()
{
    auto &ctl = mockcore::Controller::instance();

    bool loaded = false;
    QMetaObject::invokeMethod(
            m_client, [this, &loaded]() { loaded = m_client->loadConfigFromToml(validConfigToml()); },
            Qt::BlockingQueuedConnection);
    QVERIFY(loaded);
    QMetaObject::invokeMethod(m_client, "setExcludedRouteStrings", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("10.66.0.0/16")})));
    QMetaObject::invokeMethod(m_client, "setExcludedRouteStrings", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("192.168.7.0/24")})));

    // connectVpn, not beginConnect: the latter loads the config afresh, and the
    // config already loaded is the one this is about.
    QMetaObject::invokeMethod(m_client, "connectVpn", Qt::QueuedConnection);
    QTRY_VERIFY(ctl.lastCoreConfig().captured);

    const mockcore::CoreConfigSnapshot cfg = ctl.lastCoreConfig();
    QVERIFY2(!listContains(cfg.excluded_routes, "10.66.0.0/16"),
             "a route taken off the list still reached the core");
    QCOMPARE(std::count(cfg.excluded_routes.cbegin(), cfg.excluded_routes.cend(),
                        std::string("192.168.7.0/24")),
             1);
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

// The GUI no longer builds a new session for a domain or address rule or for the
// mode: it counts on this object handing them to the one that is running. Were
// that ever dropped, an edit would change the window and not the tunnel until the
// next reconnect, and under "Through VPN" a removed rule would keep its traffic in
// the tunnel while the page said otherwise.
void TestQtTrustTunnelClient::aRuleEditReachesTheRunningSessionWithoutANewOne()
{
    auto &ctl = mockcore::Controller::instance();
    const quint64 id = connectedWithLiveSession();
    QVERIFY(id != 0);
    std::vector<mockcore::ExclusionsUpdate> updates = ctl.exclusionUpdates();
    QCOMPARE(updates.at(0).client, id);
    QCOMPARE(updates.at(0).mode, int(ag::VPN_MODE_GENERAL));
    QVERIFY(has(updates.at(0), "seed.example"));

    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));
    QMetaObject::invokeMethod(m_client, "setExtraExclusionDomains", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("intranet.example")})));

    updates = ctl.exclusionUpdates();
    QCOMPARE(updates.size(), size_t(3));
    QCOMPARE(updates.at(1).mode, int(ag::VPN_MODE_SELECTIVE));
    QVERIFY(has(updates.at(1), "seed.example"));
    QCOMPARE(updates.at(2).client, id);
    QCOMPARE(updates.at(2).mode, int(ag::VPN_MODE_SELECTIVE));
    QVERIFY2(has(updates.at(2), "intranet.example"), updates.at(2).exclusions.c_str());
    QVERIFY2(!has(updates.at(2), "seed.example"), "a rule taken off the list stayed in the session");

    // The same session throughout: nothing built, nothing torn down.
    QCOMPARE(ctl.connectCallCount(), 1);
    QCOMPARE(ctl.coreConfigCaptureCount(), 1);
    QVERIFY(ctl.clientAlive(id));
    QCOMPARE(ctl.disconnectCalls(id), 0);
    QCOMPARE(m_lastState, State::Connected);
}

// The core resets every connection it carries when it is handed exclusions, and
// the GUI sends every list again after any split-tunnelling edit, a program rule
// included. Passing on an edit that changes nothing the core routes by would cut
// every open connection for a change that did not concern them.
void TestQtTrustTunnelClient::anEditThatChangesNothingLeavesTheSessionAlone()
{
    auto &ctl = mockcore::Controller::instance();
    const quint64 id = connectedWithLiveSession();
    QVERIFY(id != 0);

    QMetaObject::invokeMethod(m_client, "setExtraExclusionDomains", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("seed.example")})));
    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, false));
    // Only the program rules differ, and those are read per connection.
    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("seed.example")})),
                              Q_ARG(bool, false), Q_ARG(QStringList, QStringList({ownProgram()})));
    QCOMPARE(ctl.exclusionUpdates().size(), size_t(1));

    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));
    QCOMPARE(ctl.exclusionUpdates().size(), size_t(2)); // and a real change still goes
}

// Domains, mode and programs from one edit reach the session together. Taken one
// at a time the session would, for a moment, route by a mix that is neither the
// old settings nor the new: the first program added to "Through VPN" with no
// addresses would have the core selective while no program was listed yet, and
// every connection would leave the tunnel.
void TestQtTrustTunnelClient::splitRoutingChangesTheSessionInOneStep()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint64 id = connectedWithLiveSession();
    QVERIFY(id != 0);
    QCOMPARE(ctl.fireConnectRequest(id, ownConnection(server, 10)).action, ag::VPN_CA_DEFAULT);

    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("x.example")})),
                              Q_ARG(bool, true), Q_ARG(QStringList, QStringList({ownProgram()})));

    const std::vector<mockcore::ExclusionsUpdate> updates = ctl.exclusionUpdates();
    QCOMPARE(updates.size(), size_t(2)); // one for the seed, ONE for all of this
    QCOMPARE(updates.at(1).mode, int(ag::VPN_MODE_SELECTIVE));
    QVERIFY(has(updates.at(1), "x.example"));
    QVERIFY(!has(updates.at(1), "seed.example"));
    // And the program rule came with the mode it is read by: listed under
    // "Through VPN", this program's connection goes into the tunnel.
    QCOMPARE(ctl.fireConnectRequest(id, ownConnection(server, 11)).action,
             ag::VPN_CA_FORCE_REDIRECT);
    QCOMPARE(ctl.connectCallCount(), 1);
}

// A connection is routed in two places: the program rules decide it as it
// arrives, and the core applies the mode when it completes it, in the order its
// queue was filled. Taking the last program out of "Through VPN" switches the
// core back to the full tunnel. Were the program rules dropped before the core
// had the new mode, that program's next connection would carry no rule into a
// core still selective, and leave the tunnel. So at the instant the core is
// handed the update, the old program rules must still be the ones deciding.
void TestQtTrustTunnelClient::theCoreHasTheNewModeBeforeTheProgramRulesChange()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList()), Q_ARG(bool, true),
                              Q_ARG(QStringList, QStringList({ownProgram()})));
    const quint64 id = connectedWithLiveSession();
    QVERIFY(id != 0);
    QCOMPARE(ctl.exclusionUpdates().at(0).mode, int(ag::VPN_MODE_SELECTIVE));

    ag::VpnConnectAction atUpdate = ag::VPN_CA_REJECT;
    int hooked = 0;
    ctl.setUpdateHook([&](uint64_t client) {
        if (client != id)
            return;
        atUpdate = ctl.fireConnectRequest(id, ownConnection(server, 20 + hooked)).action;
        ++hooked;
    });
    const auto clearHook = qScopeGuard([&ctl]() { ctl.setUpdateHook(nullptr); });

    // The last program taken out: the GUI falls back to the full tunnel.
    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("seed.example")})),
                              Q_ARG(bool, false), Q_ARG(QStringList, QStringList()));
    QCOMPARE(hooked, 1);
    QVERIFY2(atUpdate == ag::VPN_CA_FORCE_REDIRECT,
             "the program rules changed before the core had the new mode, so the program "
             "just taken out of Through VPN was sent around the still-selective core");
    QCOMPARE(ctl.fireConnectRequest(id, ownConnection(server, 30)).action, ag::VPN_CA_DEFAULT);

    // The Mode switch alone keeps the same order: back to "Through VPN" with the
    // program listed, then to "Bypass VPN", where the listed program leaves.
    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("seed.example")})),
                              Q_ARG(bool, true), Q_ARG(QStringList, QStringList({ownProgram()})));
    QCOMPARE(hooked, 2);
    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, false));
    QCOMPARE(hooked, 3);
    QVERIFY2(atUpdate == ag::VPN_CA_FORCE_REDIRECT,
             "setVpnMode changed the program rules' mode before the core's");
    QCOMPARE(ctl.fireConnectRequest(id, ownConnection(server, 31)).action,
             ag::VPN_CA_FORCE_BYPASS);
}

// The GUI no longer rebuilds a session that is still being built when a rule
// changes, so an edit that lands while the core's connect() runs reaches only the
// working set. Handing it over when the attempt is adopted is the only way it
// gets into that session at all.
void TestQtTrustTunnelClient::anEditMadeWhileTheSessionIsBuiltReachesItOnceBuilt()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setBlockConnect(true);
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1); // the worker is inside connect()
    const quint64 id = ctl.lastClientId();

    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("late.example")})),
                              Q_ARG(bool, true), Q_ARG(QStringList, QStringList()));
    const QString builtWith = QString::fromStdString(ctl.lastCoreConfig().exclusions);
    QVERIFY2(!builtWith.contains(QStringLiteral("late.example")), qPrintable(builtWith));
    QVERIFY(ctl.exclusionUpdates().empty()); // nothing running to hand it to yet

    ctl.releaseConnect();
    QTRY_VERIFY_WITH_TIMEOUT(!ctl.exclusionUpdates().empty(), kLongWaitMs);
    const std::vector<mockcore::ExclusionsUpdate> updates = ctl.exclusionUpdates();
    QCOMPARE(updates.size(), size_t(1));
    QCOMPARE(updates.at(0).client, id);
    QCOMPARE(updates.at(0).mode, int(ag::VPN_MODE_SELECTIVE));
    QVERIFY(has(updates.at(0), "late.example"));
    QCOMPARE(ctl.connectCallCount(), 1);
}

// The core keeps one pending update, not a queue: one handed to it before its
// loop has run the previous one cancels that one (vpn_update_exclusions() holds
// its task in a single slot). Edits that come in a burst, as two commands read
// from the helper's socket at once do, can therefore reach the session as the
// last of them alone. That only works if every update carries the whole of what
// the session routes by, and if "nothing changed" is judged against the last
// update handed over rather than what the session was built with: a burst that
// ends where the session started must still be handed over, or the core keeps
// the edit before it.
void TestQtTrustTunnelClient::aBurstOfEditsLeavesTheSessionOnTheLastOne()
{
    auto &ctl = mockcore::Controller::instance();
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint64 id = connectedWithLiveSession();
    QVERIFY(id != 0);
    const std::string builtWith = ctl.lastCoreConfig().exclusions;

    // Queued back to back, and only the last one waited for.
    QMetaObject::invokeMethod(m_client, "setVpnMode", Qt::QueuedConnection, Q_ARG(bool, true));
    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::QueuedConnection,
                              Q_ARG(QStringList, QStringList({QStringLiteral("a.example")})),
                              Q_ARG(bool, true), Q_ARG(QStringList, QStringList({ownProgram()})));
    QMetaObject::invokeMethod(m_client, "setSplitRouting", Qt::BlockingQueuedConnection,
                              Q_ARG(QStringList, QStringList()), Q_ARG(bool, false),
                              Q_ARG(QStringList, QStringList()));

    const std::vector<mockcore::ExclusionsUpdate> updates = ctl.exclusionUpdates();
    QCOMPARE(updates.size(), size_t(4)); // the seed, then one per edit
    // Each one whole, since each may be the one the core keeps.
    QCOMPARE(updates.at(1).mode, int(ag::VPN_MODE_SELECTIVE));
    QVERIFY2(has(updates.at(1), "seed.example"), updates.at(1).exclusions.c_str());
    QCOMPARE(updates.at(2).mode, int(ag::VPN_MODE_SELECTIVE));
    QVERIFY2(has(updates.at(2), "a.example"), updates.at(2).exclusions.c_str());
    QVERIFY2(!has(updates.at(2), "seed.example"), updates.at(2).exclusions.c_str());
    // And the last is what the window shows: the session as it was built.
    QCOMPARE(updates.back().client, id);
    QCOMPARE(updates.back().mode, int(ag::VPN_MODE_GENERAL));
    QCOMPARE(updates.back().exclusions, builtWith);
    QCOMPARE(ctl.fireConnectRequest(id, ownConnection(server, 40)).action, ag::VPN_CA_DEFAULT);
    QCOMPARE(ctl.connectCallCount(), 1);
}

// With the kill switch on, the core keeps a first connect that fails inside its
// session (vendor patch 03) and goes round its recovery loop, and the block holds
// while it does. Nothing of ours may take that session down: the state stays
// Connecting, which is true, and the reason it is not connecting yet is said once
// per reason rather than once per round, where the core giving up used to say it.
void TestQtTrustTunnelClient::aFirstConnectTheCoreKeepsRetryingStaysConnectingAndSaysWhy()
{
    auto &ctl = mockcore::Controller::instance();
    QMetaObject::invokeMethod(m_client, "setKillSwitch", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();
    QTRY_COMPARE(m_lastState, State::Connecting);

    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Failed to ping location");
    QTRY_COMPARE(m_errors.size(), 1);
    QCOMPARE(m_errors.first(), QStringLiteral("Connection failed: Failed to ping location"));
    QCOMPARE(m_lastState, State::Connecting);

    ctl.fireStateChanged(id, ag::VPN_SS_RECOVERING);
    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Failed to ping location");
    ctl.fireStateChanged(id, ag::VPN_SS_RECOVERING);
    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Connection refused");
    // Two reasons taking turns are still said once each.
    ctl.fireStateChanged(id, ag::VPN_SS_RECOVERING);
    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Failed to ping location");
    QTRY_VERIFY(m_errors.size() >= 2);
    // Well past the 250 ms reconnect backoff this fixture sets.
    QTest::qWait(1500);
    QCOMPARE(m_errors.size(), 2);
    QCOMPARE(m_errors.last(), QStringLiteral("Connection failed: Connection refused"));
    QCOMPARE(m_lastState, State::Connecting);
    QCOMPARE(ctl.connectCallCount(), 1);
    QVERIFY(ctl.clientAlive(id));
    QCOMPARE(ctl.disconnectCalls(id), 0);

    ctl.fireStateChanged(id, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
    // Once it has connected, a drop is a reconnect, told by the state, as before,
    // whatever its reason: one not given yet, so only that rule keeps it quiet.
    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Endpoint closed the session");
    QTRY_COMPARE(m_lastState, State::Reconnecting);
    QTest::qWait(300);
    QCOMPARE(m_errors.size(), 2);
}

// The session the core keeps retrying is still the user's to end, at once.
void TestQtTrustTunnelClient::disconnectStopsASessionTheCoreIsStillRetrying()
{
    auto &ctl = mockcore::Controller::instance();
    QMetaObject::invokeMethod(m_client, "setKillSwitch", Qt::BlockingQueuedConnection,
                              Q_ARG(bool, true));
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    const quint64 id = ctl.lastClientId();
    ctl.fireStateChanged(id, ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Failed to ping location");
    QTRY_COMPARE(m_errors.size(), 1);

    QElapsedTimer clock;
    clock.start();
    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, 5000);
    QVERIFY2(clock.elapsed() < 5000, "disconnecting waited on the core's retries");
    QTRY_VERIFY(!ctl.clientAlive(id));
    QVERIFY(ctl.disconnectCalls(id) >= 1);
    // And it stays off: a retry round still queued must not bring it back.
    ctl.fireStateChanged(id, ag::VPN_SS_RECOVERING);
    QTest::qWait(600);
    QCOMPARE(m_lastState, State::Disconnected);
    QCOMPARE(ctl.connectCallCount(), 1);
}

// "Once per reason" is per session. The next one, after a disconnect or after
// the core ended the last, starts with nothing said, or a user who reconnects to
// the same unreachable server would be told nothing the second time.
void TestQtTrustTunnelClient::aNewSessionSaysWhyItIsNotConnectingAgain()
{
    auto &ctl = mockcore::Controller::instance();
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Failed to ping location");
    QTRY_COMPARE(m_errors.size(), 1);

    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);
    beginConnect();
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 2, kLongWaitMs);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_WAITING_RECOVERY, ag::VPN_EC_ERROR,
                         "Failed to ping location");
    QTRY_COMPARE(m_errors.size(), 2);
    QCOMPARE(m_errors.last(), QStringLiteral("Connection failed: Failed to ping location"));
}

// Windows is where this bites: the core is pinned to the adapter picked before
// its tunnel exists, and that pick used to be ours — which asks where a packet
// to a public address would go, and on a reconnect gets the answer "into the
// previous session's tunnel", then settles for the first adapter Windows lists.
// It is the core's own pick now, the one its DNS setup already insists on.
// Elsewhere the core's network monitor makes this pick and the test holds
// trivially.
void TestQtTrustTunnelClient::theCoreStartsOnTheAdapterItCallsActive()
{
    auto &ctl = mockcore::Controller::instance();
    // An index no real adapter of the machine running this has, so a pick made
    // from the machine's own interfaces cannot pass by coincidence.
    ctl.setActiveUplink(4242);

    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 1);
    QCOMPARE(mockcore::Controller::outboundInterface(), 4242u);
}

// The cable the session started on is unplugged and Wi-Fi is up. On Windows the
// core never noticed: nothing in it watches the network there, so it kept
// binding its sockets to the dead adapter until it ran out of recovery attempts
// and dropped the session, about a minute later. The core must be pointed at the
// new adapter first and told about it second — its API requires that order.
// Its system DNS servers have to move too, and before it is told: they were
// read off the old adapter, and the core restarts its DNS on the notification
// with whatever servers it holds.
void TestQtTrustTunnelClient::aSessionFollowsTheNetworkToAnotherAdapter()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id);
    if (QTest::currentTestFailed())
        return;
    QCOMPARE(ctl.systemDnsReads(id), std::vector<uint32_t>{7u}); // the attempt's own

    // Nothing moved: nothing to say, however many times the timer looks.
    QTest::qWait(500);
    QVERIFY(ctl.networkChanges(id).empty());

    ctl.setActiveUplink(9);
    QTRY_COMPARE(ctl.networkChanges(id).size(), std::size_t{1});
    const auto change = ctl.networkChanges(id).front();
    QCOMPARE(change.state, ag::VPN_NS_CONNECTED);
    QCOMPARE(change.outbound, 9u);
    QCOMPARE(mockcore::Controller::outboundInterface(), 9u);
    QCOMPARE(ctl.systemDnsReads(id), (std::vector<uint32_t>{7u, 9u}));
    QCOMPARE(change.dnsReads, std::size_t{2}); // read before the core was told

    // Said once, not on every look after.
    QTest::qWait(500);
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{1});
    QCOMPARE(m_lastState, State::Connected);
}

// No adapter with a default route at all, then the same one back — Wi-Fi
// dropping and rejoining. The core is told the network is gone, which parks it
// in WAITING_FOR_NETWORK instead of spending its recovery attempts on a network
// that is not there, and with the kill switch on that is where the session
// waits. It then has to be told the network is back even though the adapter is
// the same one, or it waits there for good.
void TestQtTrustTunnelClient::uplinkLossIsReportedAndSoIsItsReturn()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id);
    if (QTest::currentTestFailed())
        return;

    ctl.setActiveUplink(0);
    lookAtUplink();
    QVERIFY(ctl.networkChanges(id).empty()); // one look is not enough to leave
    lookAtUplink();
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{1});
    QCOMPARE(ctl.networkChanges(id).at(0).state, ag::VPN_NS_NOT_CONNECTED);
    // The binding stays: there is nothing better to point it at.
    QCOMPARE(mockcore::Controller::outboundInterface(), 7u);

    // Nor are the DNS servers read: with no adapter there are none to read.
    QCOMPARE(ctl.systemDnsReads(id).size(), std::size_t{1});

    // Back from nothing, one look is enough: the session is down until it is used.
    // The DNS servers are read again even though the adapter is the same one —
    // the network behind it need not be (another Wi-Fi network, another router).
    ctl.setActiveUplink(7);
    lookAtUplink();
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{2});
    QCOMPARE(ctl.networkChanges(id).at(1).state, ag::VPN_NS_CONNECTED);
    QCOMPARE(ctl.networkChanges(id).at(1).outbound, 7u);
    QCOMPARE(ctl.systemDnsReads(id), (std::vector<uint32_t>{7u, 7u}));
    QCOMPARE(ctl.networkChanges(id).at(1).dnsReads, std::size_t{2});
}

// The DNS servers of the new network cannot always be read — an adapter half
// set up, a lease not yet in. The move still happens: the core keeps the
// servers it had, which is no worse than not reading them, and the session is
// not dropped over it.
void TestQtTrustTunnelClient::uplinkMoveGoesAheadWhenItsDnsCannotBeRead()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id);
    if (QTest::currentTestFailed())
        return;

    ctl.setDnsError("Failed to collect DNS servers: interface not found");
    ctl.setActiveUplink(9);
    lookAtUplink();
    lookAtUplink();
    QCOMPARE(ctl.systemDnsReads(id).size(), std::size_t{2});
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{1});
    QCOMPARE(ctl.networkChanges(id).at(0).state, ag::VPN_NS_CONNECTED);
    QCOMPARE(ctl.networkChanges(id).at(0).outbound, 9u);
    QVERIFY(ctl.clientAlive(id));
    QCOMPARE(ctl.disconnectCalls(id), 0);
    QCOMPARE(m_lastState, State::Connected);
}

// Leaving a working adapter costs the session a reconnect, so a look that
// disagrees with the one before it is not acted on — a route being replaced or
// a lease being renewed must not cost two.
void TestQtTrustTunnelClient::uplinkBlipIsNotAMove()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id);
    if (QTest::currentTestFailed())
        return;

    for (const uint32_t blip : {9u, 0u, 9u}) {
        ctl.setActiveUplink(blip);
        lookAtUplink();
        ctl.setActiveUplink(7);
        lookAtUplink();
    }
    QVERIFY(ctl.networkChanges(id).empty());
    QCOMPARE(mockcore::Controller::outboundInterface(), 7u);
}

// Windows pins the outbound interface again before every socket the core
// protects, so that a late write by anything else cannot leave the core's own
// traffic on the wrong adapter. The pin has to move with the session: left on
// the adapter the session started on, the first protect after a move would put
// the core straight back on it.
void TestQtTrustTunnelClient::uplinkMoveSurvivesTheNextProtect()
{
#ifndef Q_OS_WIN
    QSKIP("only Windows pins the outbound interface on every protect");
#else
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id);
    if (QTest::currentTestFailed())
        return;

    ctl.setActiveUplink(9);
    lookAtUplink();
    lookAtUplink();
    QCOMPARE(mockcore::Controller::outboundInterface(), 9u);

    // A stray write — an abandoned attempt's network monitor starting late
    // makes exactly this one, with the adapter of its own day.
    ag::vpn_network_manager_set_outbound_interface(7);
    QVERIFY(ctl.fireProtect(id) != mockcore::Controller::kNoProtectHandler);
    QCOMPARE(mockcore::Controller::outboundInterface(), 9u);
#endif
}

// A session is judged against the adapter its own core was built on, not the
// one the session before it ended on: otherwise reconnecting after a move would
// start with a bogus "the network changed" and a needless second reconnect.
void TestQtTrustTunnelClient::uplinkOfANewSessionIsTheOneItWasBuiltOn()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 first = 0;
    connectOnUplink(7, &first);
    if (QTest::currentTestFailed())
        return;
    requestDisconnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Disconnected, kLongWaitMs);

    ctl.setActiveUplink(9);
    beginConnect();
    QTRY_VERIFY(ctl.connectCallCount() >= 2);
    const quint64 second = ctl.lastClientId();
    ctl.fireStateChanged(second, ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
    QCOMPARE(mockcore::Controller::outboundInterface(), 9u);

    lookAtUplink();
    lookAtUplink();
    QVERIFY(ctl.networkChanges(second).empty());
}

// On Windows the core reads the system DNS servers off the active adapter and
// fails the connect when there is none. Every session rebuilt while the network
// was gone — after the network-wait timeout, or after the core gave up
// recovering — hit that, and it was taken as a broken setup: the session
// stopped in Error and never came back by itself, network or not.
void TestQtTrustTunnelClient::anAttemptMadeOfflineIsRetried()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setActiveUplink(0);
    ctl.setDnsError("Couldn't detect active network interface");

    beginConnect();
    // Built, failed at DNS, built again: a retry, not a stop.
    QTRY_VERIFY_WITH_TIMEOUT(ctl.coreConfigCaptureCount() >= 2, kLongWaitMs);
    QVERIFY(m_lastState != State::Error);

    // The network comes back; the next retry gets through.
    ctl.setDnsError({});
    ctl.setActiveUplink(7);
    QTRY_VERIFY_WITH_TIMEOUT(ctl.connectCallCount() >= 1, kLongWaitMs);
    ctl.fireStateChanged(ctl.lastClientId(), ag::VPN_SS_CONNECTED);
    QTRY_COMPARE(m_lastState, State::Connected);
}

// The failure says it was offline; by the time anything looks again, an
// adapter may be back. That is the network returning, not a broken setup —
// judging by the second look stopped such a session in Error after all.
void TestQtTrustTunnelClient::anAdapterBackRightAfterAnOfflineFailureStillRetries()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setActiveUplink(7);
    ctl.setDnsError("Couldn't detect active network interface");

    beginConnect();
    QTRY_VERIFY_WITH_TIMEOUT(ctl.coreConfigCaptureCount() >= 2, kLongWaitMs);
    QVERIFY(m_lastState != State::Error);
}

// Online, but over a link the core does not count as an adapter: PPPoE, or a
// modem that brings up a PPP link. The core's DNS setup fails exactly as it
// does offline, and retrying would show "Reconnecting" for ever. It stops, and
// says why in words a user can act on.
void TestQtTrustTunnelClient::aPppLinkTheCoreCannotUseIsNamedNotRetried()
{
    auto &ctl = mockcore::Controller::instance();
    qputenv("FT_TEST_PPP_LINK", "1");
    const auto noPpp = qScopeGuard([] { qunsetenv("FT_TEST_PPP_LINK"); });
    ctl.setActiveUplink(0);
    ctl.setDnsError("Couldn't detect active network interface");

    beginConnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Error, kLongWaitMs);
    QVERIFY(!m_errors.isEmpty());
    QVERIFY2(m_errors.constLast().contains(QLatin1String("PPP link")),
             qPrintable(m_errors.constLast()));
    QTest::qWait(800); // several retry intervals
    QCOMPARE(ctl.coreConfigCaptureCount(), 1);
    QCOMPARE(m_lastState, State::Error);
}

// The other side of that line: with a network present, a DNS setup that fails
// is not going to fix itself by being retried every few seconds.
void TestQtTrustTunnelClient::aDnsFailureWithANetworkStillStops()
{
    auto &ctl = mockcore::Controller::instance();
    ctl.setActiveUplink(7);
    ctl.setDnsError("Failed to update DNS servers");

    beginConnect();
    QTRY_COMPARE_WITH_TIMEOUT(m_lastState, State::Error, kLongWaitMs);
    QTest::qWait(800); // several retry intervals
    QCOMPARE(ctl.coreConfigCaptureCount(), 1);
    QCOMPARE(m_lastState, State::Error);
}

// Offline, the core's own look at the network writes a warning to its log each
// time it finds nothing. Asked every 2 s, that was a warning every 2 s for as
// long as the machine was offline; so the looks went to every 10 s, and the
// session came back up to 10 s after the network did. Now the routing table is
// read first, without the core, which is asked once there is a default route
// again, and otherwise only now and then.
void TestQtTrustTunnelClient::uplinkOfflineAsksTheCoreOnlyOnceARouteIsBack()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id);
    if (QTest::currentTestFailed())
        return;
    qputenv("FT_TEST_DEFAULT_ROUTE", "0");
    const auto noRoute = qScopeGuard([] { qunsetenv("FT_TEST_DEFAULT_ROUTE"); });

    ctl.setActiveUplink(0);
    lookAtUplink();
    lookAtUplink();
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{1});
    QCOMPARE(ctl.networkChanges(id).at(0).state, ag::VPN_NS_NOT_CONNECTED);
    const int asked = ctl.activeUplinkLooks();

    // Every 15th look asks all the same, should the table and the core disagree.
    for (int round = 1; round <= 2; ++round) {
        for (int i = 0; i < 14; ++i)
            lookAtUplink();
        QCOMPARE(ctl.activeUplinkLooks(), asked + round - 1);
        lookAtUplink();
        QCOMPARE(ctl.activeUplinkLooks(), asked + round);
    }
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{1});

    // A default route again: the next look asks, and the session goes on over
    // the core's pick.
    qputenv("FT_TEST_DEFAULT_ROUTE", "1");
    ctl.setActiveUplink(9);
    lookAtUplink();
    QCOMPARE(ctl.activeUplinkLooks(), asked + 3);
    QCOMPARE(ctl.networkChanges(id).size(), std::size_t{2});
    QCOMPARE(ctl.networkChanges(id).at(1).state, ag::VPN_NS_CONNECTED);
    QCOMPARE(ctl.networkChanges(id).at(1).outbound, 9u);
}

// Waking from sleep or rejoining Wi-Fi: the network is back a moment after it
// went. The look after the session was parked comes one interval later, as
// online, not five, which left the session down for up to 10 s on Windows.
void TestQtTrustTunnelClient::offlineLooksKeepThePace()
{
    auto &ctl = mockcore::Controller::instance();
    quint64 id = 0;
    connectOnUplink(7, &id); // the timer looks every second (init())
    if (QTest::currentTestFailed())
        return;

    ctl.setActiveUplink(0);
    QTRY_COMPARE_WITH_TIMEOUT(ctl.networkChanges(id).size(), std::size_t{1}, 5000);
    QCOMPARE(ctl.networkChanges(id).at(0).state, ag::VPN_NS_NOT_CONNECTED);
    ctl.setActiveUplink(7);
    QTRY_COMPARE_WITH_TIMEOUT(ctl.networkChanges(id).size(), std::size_t{2}, 3000);
    QCOMPARE(ctl.networkChanges(id).at(1).state, ag::VPN_NS_CONNECTED);
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
