// cppcheck-suppress-file missingIncludeSystem
// What the REAL helper promises about its own life, checked against the real
// helper process (tests/real_helper.h), not MockHelperServer.
//
// The helper runs as root and owns the tunnel, so when it leaves, and what it
// takes with it, is as much a part of the boundary as what it parses. Each of
// these was written down — in vpn_helper_server.cpp and docs/security-threats.md
// — and until now checked only against the hand-written double, which implements
// its own version of each rule: a regression in the real one left every test
// green.
#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QProcessEnvironment>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "real_helper.h"
#include "vpn/vpn_helper_client.h"

using realhelper::envWith;
using realhelper::fileLines;

namespace {

// The minute a helper nobody claimed waits before leaving, shortened through
// FT_TEST_HELPER_AUTH_WINDOW_MS (a test hook compiled out of release builds).
// Long enough for a slow CI runner to start the helper and authenticate within it.
constexpr int kAuthWindowMs = 3000;

QProcessEnvironment withShortAuthWindow()
{
    return envWith(QStringLiteral("FT_TEST_HELPER_AUTH_WINDOW_MS"), QString::number(kAuthWindowMs));
}

} // namespace

class TestHelperServerLifecycle : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void theHelperLeavesWithTheGuiAndTakesTheTunnelDown();
    void theGuiStopsSayingConnectedWhenTheHelperDies();
    void aConnectionAfterAuthenticationIsTurnedAway();
    void aHelperNobodyAuthenticatesLeavesOnItsOwn();
    void anAuthenticatedHelperOutlivesThatDeadline();
    void aPeerThatNeverAuthenticatesIsDroppedAtTheDeadline();

private:
    QTemporaryDir m_dir;
};

void TestHelperServerLifecycle::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(QFile::exists(QStringLiteral(FT_TEST_HELPER_BINARY)),
             "helper binary was not built next to this test");
    QVERIFY2(realhelper::ensureWintunPlaceholder(), "could not place the wintun.dll placeholder");
}

// The helper's socket to the GUI is the only thing keeping it alive once it is
// authenticated: when that goes — the GUI quit, crashed, or was killed — the
// helper quits, and the tunnel comes down on the way out (HelperServer's
// destructor). A helper that outlived its GUI would leave routes, DNS and the
// kill switch installed with nothing left to turn them off.
void TestHelperServerLifecycle::theHelperLeavesWithTheGuiAndTakesTheTunnelDown()
{
    const QString token = QStringLiteral("token-for-gui-gone");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token));

    {
        QTcpSocket gui;
        QVERIFY(helper.connectTo(gui));
        QVERIFY(realhelper::authenticate(gui, token));
        gui.write(realhelper::line({{QStringLiteral("cmd"), QStringLiteral("connect")},
                                    {QStringLiteral("configToml"), realhelper::minimalConfigToml()}}));
        gui.flush();
        // A tunnel the core is holding, or there is nothing to take down.
        QTRY_VERIFY_WITH_TIMEOUT(fileLines(helper.coreEventLog()).contains(QStringLiteral("connect 1")),
                                 20000);
        QVERIFY(helper.running());
        gui.abort(); // the GUI is gone, without so much as a quit
    }

    QVERIFY2(helper.process()->waitForFinished(10000), "the helper outlived its GUI");
    QCOMPARE(helper.process()->exitStatus(), QProcess::NormalExit);
    QCOMPARE(helper.process()->exitCode(), 0);
    const QStringList events = fileLines(helper.coreEventLog());
    QVERIFY2(events.contains(QStringLiteral("disconnect 1")), qPrintable(events.join(QLatin1Char('|'))));
    QVERIFY2(events.contains(QStringLiteral("destroyed 1")), qPrintable(events.join(QLatin1Char('|'))));
}

// The same moment from the other side. A helper that dies — crashed, killed, or
// taken down with the session it was serving — says nothing on its way out: the
// socket closing is all the GUI ever gets. If the GUI did not take that as the
// end of the session, the window would go on saying Connected over a tunnel that
// no longer exists, which is the one thing a VPN client must never show.
void TestHelperServerLifecycle::theGuiStopsSayingConnectedWhenTheHelperDies()
{
    const QString token = QStringLiteral("token-for-helper-gone");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token, envWith(QStringLiteral("FT_TEST_CORE_REPORTS_CONNECTED"),
                                        QStringLiteral("1"))));
    const auto pointed = realhelper::pointClientsAt(helper.port(), token);

    VpnHelperClient client;
    client.loadConfigFromToml(realhelper::minimalConfigToml());
    client.connectVpn();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), VpnHelperClient::State::Connected, 20000);

    // Killed, not asked: no last "state" event can stand in for the socket.
    helper.process()->kill();
    QVERIFY(helper.process()->waitForFinished(5000));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), VpnHelperClient::State::Disconnected, 10000);
}

// One GUI, ever. Once a client has authenticated, the slot is taken for the
// helper's whole life: a later connection is closed on arrival, unheard, and the
// session it would have interfered with carries on.
void TestHelperServerLifecycle::aConnectionAfterAuthenticationIsTurnedAway()
{
    const QString token = QStringLiteral("token-for-late-peer");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token));

    QTcpSocket gui;
    QVERIFY(helper.connectTo(gui));
    QVERIFY(realhelper::authenticate(gui, token));

    QTcpSocket late;
    QVERIFY(helper.connectTo(late));
    // It even knows the protocol; it is not asked.
    late.write(realhelper::line({{QStringLiteral("cmd"), QStringLiteral("hello")},
                                 {QStringLiteral("nonce"), QStringLiteral("late-nonce")}}));
    late.flush();
    // Well inside the 5 s a pending peer would get: closed on arrival.
    QVERIFY2(late.state() == QAbstractSocket::UnconnectedState || late.waitForDisconnected(2000),
             "a connection after authentication was kept");
    QVERIFY2(!late.readAll().contains("challenge"), "a connection after authentication was answered");

    QVERIFY(helper.running());
    QVERIFY2(realhelper::answersOnSession(gui), "the authenticated session did not survive the late peer");
}

// The helper is started before anyone can know whether the GUI will reach it —
// the elevation prompt may be declined, the GUI may give up — so one that nobody
// authenticated leaves by itself, a minute after it started.
void TestHelperServerLifecycle::aHelperNobodyAuthenticatesLeavesOnItsOwn()
{
    realhelper::Process helper(m_dir.path());
    QElapsedTimer sinceStart;
    sinceStart.start();
    QVERIFY(helper.start(QStringLiteral("token-nobody-presents"), withShortAuthWindow()));

    QVERIFY2(helper.process()->waitForFinished(10000), "an unauthenticated helper stayed up");
    QCOMPARE(helper.process()->exitStatus(), QProcess::NormalExit);
    QCOMPARE(helper.process()->exitCode(), 0);
    // Its own deadline, not something else that ended it sooner. Less 10%: a
    // timer this long is a coarse one, which Qt may fire up to 5% early.
    QVERIFY2(sinceStart.elapsed() >= kAuthWindowMs * 9 / 10,
             qPrintable(QString::number(sinceStart.elapsed())));
}

// The other half of the same rule: the deadline is for a helper nobody claimed.
// One the GUI has authenticated serves for as long as the GUI stays.
void TestHelperServerLifecycle::anAuthenticatedHelperOutlivesThatDeadline()
{
    const QString token = QStringLiteral("token-claimed-in-time");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token, withShortAuthWindow()));

    QTcpSocket gui;
    QVERIFY(helper.connectTo(gui));
    QVERIFY(realhelper::authenticate(gui, token));

    QTest::qWait(kAuthWindowMs + 2000); // well past the deadline
    QVERIFY2(helper.running(), "the helper left under an authenticated GUI");
    QVERIFY(realhelper::answersOnSession(gui));
}

// Every connection that has not authenticated is dropped five seconds after it
// arrived, whatever it has or has not said — docs/security-threats.md lists
// this among what keeps the root process from being held open by strangers.
// Only the oversized-line cap had a test on the real helper; the deadline itself
// was checked against the double alone.
void TestHelperServerLifecycle::aPeerThatNeverAuthenticatesIsDroppedAtTheDeadline()
{
    const QString token = QStringLiteral("token-for-slow-peers");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token));

    QElapsedTimer sinceConnect;
    QTcpSocket silent; // says nothing at all
    QTcpSocket halfway; // says hello, takes the challenge, never answers it
    sinceConnect.start();
    QVERIFY(helper.connectTo(silent));
    QVERIFY(helper.connectTo(halfway));
    halfway.write(realhelper::line({{QStringLiteral("cmd"), QStringLiteral("hello")},
                                    {QStringLiteral("nonce"), QStringLiteral("halfway-nonce")}}));
    halfway.flush();
    QVERIFY(realhelper::readLineWithin(halfway).contains("challenge"));

    QVERIFY2(silent.waitForDisconnected(9000), "a silent peer was never dropped");
    QVERIFY2(halfway.state() == QAbstractSocket::UnconnectedState || halfway.waitForDisconnected(2000),
             "a peer that never answered its challenge was never dropped");
    // The deadline, not an early drop for some other reason (less 10%, as above).
    QVERIFY2(sinceConnect.elapsed() >= 4500, qPrintable(QString::number(sinceConnect.elapsed())));

    // The peers went; the helper did not, and still takes the genuine client.
    QVERIFY(helper.running());
    QTcpSocket gui;
    QVERIFY(helper.connectTo(gui));
    QVERIFY(realhelper::authenticate(gui, token));
}

QTEST_MAIN(TestHelperServerLifecycle)
#include "test_helper_server_lifecycle.moc"
