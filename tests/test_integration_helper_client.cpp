#include <QtTest>

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include "helper_ipc_mock_server.h"

// GUI-side helper IPC client (no elevated process): mirrors VpnHelperClient handshake.
class TestIntegrationHelperClient : public QObject {
    Q_OBJECT

private slots:
    void clientHandshakeAndConnectFlow();
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

    QJsonObject hello;
    hello[QStringLiteral("cmd")] = QStringLiteral("hello");
    hello[QStringLiteral("token")] = token;
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

QTEST_MAIN(TestIntegrationHelperClient)
#include "test_integration_helper_client.moc"
