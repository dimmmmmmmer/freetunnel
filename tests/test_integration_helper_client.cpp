// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <QSignalSpy>
#include <QTcpServer>

#include "helper_ipc_mock_server.h"
#include "vpn/vpn_helper_client.h"
#include "vpn/vpn_helper_protocol.h"

// GUI-side helper IPC client (no elevated process): mirrors VpnHelperClient handshake.
class TestIntegrationHelperClient : public QObject {
    Q_OBJECT

private slots:
    void clientHandshakeAndConnectFlow();
    void realClientRefusesPeerThatCannotProveTheToken();
    void realClientRefusesPeerThatSkipsTheChallenge();
};

void TestIntegrationHelperClient::clientHandshakeAndConnectFlow()
{
    const QString token = QStringLiteral("client-integration-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    QTcpSocket sock;
    sock.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(sock.waitForConnected(3000));
    server.acceptPending();

    QVERIFY(mockHelperHandshake(server, sock, token));
    QJsonObject hello;
    hello[QStringLiteral("cmd")] = QStringLiteral("noop");
    sock.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(server.authed());

    QJsonObject setMode;
    setMode[QStringLiteral("cmd")] = QStringLiteral("setMode");
    setMode[QStringLiteral("selective")] = false;
    sock.write(QJsonDocument(setMode).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(1000));
    QCOMPARE(server.lastCmd(), QStringLiteral("setMode"));

    QJsonObject connectCmd;
    connectCmd[QStringLiteral("cmd")] = QStringLiteral("connect");
    connectCmd[QStringLiteral("configPath")] = QStringLiteral("/tmp/test.toml");
    sock.write(QJsonDocument(connectCmd).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(3000));
    QCOMPARE(server.lastCmd(), QStringLiteral("connect"));

    QString lastState;
    for (int i = 0; i < 6 && lastState != QLatin1String("Connected"); ++i) {
        if (sock.bytesAvailable() == 0 && !sock.waitForReadyRead(3000))
            break;
        while (sock.canReadLine()) {
            const auto doc = QJsonDocument::fromJson(sock.readLine());
            if (!doc.isObject())
                continue;
            const QJsonObject ev = doc.object();
            if (ev.value(QStringLiteral("ev")).toString() == QLatin1String("state"))
                lastState = ev.value(QStringLiteral("state")).toString();
        }
    }
    QCOMPARE(lastState, QStringLiteral("Connected"));

    QJsonObject disconnectCmd;
    disconnectCmd[QStringLiteral("cmd")] = QStringLiteral("disconnect");
    sock.write(QJsonDocument(disconnectCmd).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(3000));
    QCOMPARE(server.lastCmd(), QStringLiteral("disconnect"));
}

// The helper only starts listening after the elevation prompt is answered, so a
// local process can own that port in the meantime. It must not be able to talk
// the GUI into handing over the config: that payload carries the VPN password,
// and an answer of "Connected" from it would show a protected tunnel where there
// is none. Drive the REAL VpnHelperClient against a peer that cannot produce the
// proof and assert it never sends anything sensitive.
void TestIntegrationHelperClient::realClientRefusesPeerThatCannotProveTheToken()
{
    QTcpServer rogue;
    QVERIFY(rogue.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    connect(&rogue, &QTcpServer::newConnection, this, [&]() {
        peer = rogue.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, this, [&]() {
            received += peer->readAll();
            // Answer the hello like a helper would, but with a proof we cannot
            // actually compute — we do not know the token.
            if (received.contains("\"cmd\":\"hello\"")) {
                QJsonObject ch;
                ch[QStringLiteral("ev")] = QStringLiteral("challenge");
                ch[QStringLiteral("proof")] = QString(64, QLatin1Char('0'));
                ch[QStringLiteral("nonce")] = QStringLiteral("rogue-nonce");
                peer->write(QJsonDocument(ch).toJson(QJsonDocument::Compact) + '\n');
                peer->flush();
            }
        });
    });

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(rogue.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", "the-real-token");

    VpnHelperClient client;
    QSignalSpy errors(&client, &VpnHelperClient::vpnError);
    client.loadConfigFromToml(QStringLiteral("password = \"super-secret\"\n"));
    client.connectVpn();

    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    // Guard against passing for the wrong reason: the negative assertions below
    // are only meaningful if the client actually reached this peer and opened
    // the handshake with it.
    QVERIFY(received.contains("\"cmd\":\"hello\""));
    QVERIFY(!received.contains("super-secret"));
    QVERIFY(!received.contains("\"cmd\":\"auth\""));
    QVERIFY(!received.contains("\"cmd\":\"connect\""));

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

// Verifying the challenge is not sufficient on its own: the client must also
// refuse to act on ANYTHING from a peer that never proved itself. A rogue that
// simply skipped the challenge and announced {"ev":"ready"} was handed the
// config TOML — password included — and could then report a tunnel that did not
// exist. Everything said before a peer is proven has to be inert.
void TestIntegrationHelperClient::realClientRefusesPeerThatSkipsTheChallenge()
{
    QTcpServer rogue;
    QVERIFY(rogue.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    connect(&rogue, &QTcpServer::newConnection, this, [&]() {
        peer = rogue.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, this, [&]() {
            const bool first = received.isEmpty();
            received += peer->readAll();
            if (first) {
                // No challenge, no proof — just claim the handshake is done.
                peer->write(QByteArrayLiteral("{\"ev\":\"ready\"}\n"));
                peer->flush();
            }
        });
    });

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(rogue.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", "the-real-token");

    VpnHelperClient client;
    QSignalSpy errors(&client, &VpnHelperClient::vpnError);
    client.loadConfigFromToml(QStringLiteral("password = \"super-secret\"\n"));
    client.connectVpn();

    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    // The client did talk to this peer, so the negative assertions mean something.
    QVERIFY(received.contains("\"cmd\":\"hello\""));
    QVERIFY(!received.contains("super-secret"));
    QVERIFY(!received.contains("\"cmd\":\"connect\""));
    QVERIFY(!received.contains("\"cmd\":\"auth\""));

    // A fabricated state from it must not move the client either.
    if (peer) {
        peer->write(QByteArrayLiteral("{\"ev\":\"state\",\"state\":\"Connected\"}\n"));
        peer->flush();
    }
    QTest::qWait(200);
    QVERIFY(client.state() != VpnHelperClient::State::Connected);

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

QTEST_MAIN(TestIntegrationHelperClient)
#include "test_integration_helper_client.moc"
