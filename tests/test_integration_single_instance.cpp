// cppcheck-suppress-file missingIncludeSystem
// End-to-end single-instance control IPC, exercising PRODUCTION code: the
// second launch (forwardToRunningInstance) and the token/framing checks the
// listener applies to whatever arrives.
//
// The previous version of this file created a bare QLocalServer, wrote bytes
// into it and read them back — it verified that Qt delivers bytes, contained no
// project code at all, and could not fail. The auth boundary it was named after
// (an unauthenticated local peer must not be able to drive the running app) was
// never touched.
#include <QtTest>

#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <thread>

#include "core/InstanceControl.h"

class TestIntegrationSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void forwardDeliversAuthenticatedCommand();
    void listenerRejectsWrongToken();
    void listenerRejectsUnframedMessage();
    void forwardFailsWhenNoInstanceIsListening();
    void cleanupTestCase();

private:
    struct Received {
        bool got = false;
        QString token;
        QString payload;
    };
    Received readOne(QLocalServer &server, int timeoutMs = 3000);
    void wireCollector(QLocalServer &server);

    QByteArray m_received;
    QString m_socketName;
    QTemporaryDir m_configHome;
};

void TestIntegrationSingleInstance::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    // Keep the auth token out of the real user's store: on Linux the file
    // fallback follows XDG_CONFIG_HOME, and the credential store resolves to a
    // test-only service because test targets are built with FT_ENABLE_TEST_HOOKS
    // (see credentialServiceName()).
    qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SingleInstanceTest"));

    m_socketName = QStringLiteral("freetunnel-si-test-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(m_socketName);
}

void TestIntegrationSingleInstance::cleanupTestCase()
{
    freetunnel::removeInstanceAuthToken();
    QLocalServer::removeServer(m_socketName);
}

// Collect asynchronously rather than polling after the fact: the real sender
// writes, flushes and disconnects, and on Windows named pipes a peer that has
// already gone leaves nothing to accept — the message has to be picked up as it
// arrives, which is also how the production listener works.
TestIntegrationSingleInstance::Received
TestIntegrationSingleInstance::readOne(QLocalServer &server, int timeoutMs)
{
    Received out;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs && !out.got) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (m_received.isEmpty())
            continue;
        if (freetunnel::parseInstanceMessage(m_received, &out.token, &out.payload))
            out.got = true;
    }
    Q_UNUSED(server);
    return out;
}

void TestIntegrationSingleInstance::wireCollector(QLocalServer &server)
{
    m_received.clear();
    connect(&server, &QLocalServer::newConnection, this, [this, &server]() {
        while (QLocalSocket *peer = server.nextPendingConnection()) {
            connect(peer, &QLocalSocket::readyRead, this,
                    [this, peer]() { m_received += peer->readAll(); });
            connect(peer, &QLocalSocket::disconnected, this, [this, peer]() {
                m_received += peer->readAll();
                peer->deleteLater();
            });
            m_received += peer->readAll();
        }
    });
}

// The real second-launch path: find the session token, verify the listener
// belongs to the same user, deliver the control command verbatim.
void TestIntegrationSingleInstance::forwardDeliversAuthenticatedCommand()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QVERIFY(!token.isEmpty());

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(m_socketName));
    wireCollector(server);

    // Send from another thread so the listener is spinning its event loop while
    // the client writes — which is what happens in production (the running
    // instance is a separate process). Doing it inline blocks the loop until the
    // sender has already disconnected, and a Windows named pipe discards data
    // the server never read.
    bool sent = false;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance(m_socketName,
                                                    QStringLiteral("freetunnel://toggle"));
    });
    const Received got = readOne(server);
    sender.join();
    QVERIFY(sent);
    QVERIFY(got.got);
    QCOMPARE(got.payload, QStringLiteral("freetunnel://toggle"));
    // It really is the session token — a sender that shipped anything at all
    // would satisfy the payload check above just the same.
    QVERIFY(freetunnel::instanceTokensEqual(got.token, token));
    QVERIFY(!freetunnel::instanceTokensEqual(got.token, token + QStringLiteral("x")));
}

// A local peer that does not hold the session token must not be able to drive
// the running instance: the framing parses, and the auth check is what rejects.
void TestIntegrationSingleInstance::listenerRejectsWrongToken()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));

    QString parsedToken;
    QString payload;
    const QByteArray forged = freetunnel::formatInstanceMessage(
            QStringLiteral("not-the-token"), QStringLiteral("freetunnel://connect"));
    QVERIFY(freetunnel::parseInstanceMessage(forged, &parsedToken, &payload));
    QCOMPARE(payload, QStringLiteral("freetunnel://connect"));
    QVERIFY(!freetunnel::instanceTokensEqual(parsedToken, token));
}

void TestIntegrationSingleInstance::listenerRejectsUnframedMessage()
{
    QString token;
    QString payload;
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("freetunnel://toggle"), &token, &payload));
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray(), &token, &payload));
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("\npayload-without-token"), &token,
                                              &payload));
}

// No listener: the launch must fall through to starting a new instance rather
// than reporting success, which would make the app silently fail to start.
void TestIntegrationSingleInstance::forwardFailsWhenNoInstanceIsListening()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    const QString dead = m_socketName + QStringLiteral("-nobody-home");
    QLocalServer::removeServer(dead);
    QVERIFY(!freetunnel::forwardToRunningInstance(dead, QStringLiteral("freetunnel://toggle")));
}

QTEST_MAIN(TestIntegrationSingleInstance)
#include "test_integration_single_instance.moc"
