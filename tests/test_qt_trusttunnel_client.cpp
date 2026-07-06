// cppcheck-suppress-file missingIncludeSystem
// State-machine tests for QtTrustTunnelClient against the mock core in
// tests/mock_core. The client runs on a dedicated worker thread, exactly like
// the elevated helper hosts it in production (vpn_helper_server.cpp), so the
// timer thread-affinity and cross-thread command handling are exercised too.
#include <QtTest>

#include <QPointer>
#include <QSignalSpy>
#include <QThread>

#include "mock_core_controller.h"
#include "qt_trusttunnel_client.h"

using State = QtTrustTunnelClient::State;

namespace {
constexpr int kLongWaitMs = 20000;
} // namespace

class TestQtTrustTunnelClient : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("FT_TEST_SKIP_PRIVILEGE_CHECK", "1");
        qRegisterMetaType<QtTrustTunnelClient::State>();
    }

    void init()
    {
        mockcore::Controller::instance().reset();
        m_lastState = State::Disconnected;
        m_errors.clear();

        m_thread = new QThread(this);
        m_client = new QtTrustTunnelClient();
        m_client->setSessionLogging(QString(), false); // no core log tail in tests
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
    void staleEventFromPreviousSessionIsIgnored();
    void failedAttemptSchedulesWorkingRetry();
    void coreDropTriggersAutoReconnect();
    void disconnectWhileConnectBlockedStaysClean();

private:
    void beginConnect()
    {
        QMetaObject::invokeMethod(m_client, "beginConnect", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("mock = true")),
                                  Q_ARG(QString, QString()));
    }

    void requestDisconnect()
    {
        QMetaObject::invokeMethod(m_client, "disconnectVpn", Qt::QueuedConnection);
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
}

QTEST_GUILESS_MAIN(TestQtTrustTunnelClient)
#include "test_qt_trusttunnel_client.moc"
