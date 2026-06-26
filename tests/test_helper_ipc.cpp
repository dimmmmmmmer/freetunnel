// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>
#include <QAbstractSocket>

#include "helper_ipc_mock_server.h"
#include "vpn/vpn_helper_protocol.h"

class TestHelperIpc : public QObject {
    Q_OBJECT

private slots:
    void tokensEqualMatches();
    void authSuccess();
    void authRejectsBadToken();
    void rejectsSecondClientAfterAuth();
    void preAuthSquatterDoesNotBlock();
    void oversizedBufferClosesConnection();
    void connectDisconnectFlow();
    void preAuthCommandsRejected();
};

void TestHelperIpc::tokensEqualMatches()
{
    QVERIFY(vpn_helper::tokensEqual(QStringLiteral("abc"), QStringLiteral("abc")));
    QVERIFY(!vpn_helper::tokensEqual(QStringLiteral("abc"), QStringLiteral("abd")));
    QVERIFY(!vpn_helper::tokensEqual(QStringLiteral("ab"), QStringLiteral("abc")));
}

void TestHelperIpc::authSuccess()
{
    const QString token = QStringLiteral("test-token-42");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    QTcpSocket client;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(client.waitForConnected(3000));
    server.acceptPending();

    QJsonObject hello;
    hello["cmd"] = "hello";
    hello["token"] = token;
    client.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(client.waitForReadyRead(3000));

    const QByteArray line = client.readLine();
    const auto doc = QJsonDocument::fromJson(line);
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value("ev").toString(), QStringLiteral("ready"));
    QVERIFY(server.authed());
}

void TestHelperIpc::authRejectsBadToken()
{
    MockHelperServer server(QStringLiteral("correct"));
    QVERIFY(server.listen());

    QTcpSocket client;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(client.waitForConnected(3000));
    server.acceptPending();

    QJsonObject hello;
    hello["cmd"] = "hello";
    hello["token"] = "wrong";
    client.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(client.waitForDisconnected(3000));
    QVERIFY(!server.authed());
}

// Once a client has authenticated it owns the single slot — a later connection
// is dropped.
void TestHelperIpc::rejectsSecondClientAfterAuth()
{
    const QString token = QStringLiteral("tok");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    QTcpSocket first;
    first.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(first.waitForConnected(3000));
    server.acceptPending();
    QJsonObject hello;
    hello["cmd"] = "hello";
    hello["token"] = token;
    first.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    first.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(server.authed());

    QTcpSocket second;
    second.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(second.waitForConnected(3000));
    server.acceptPending();
    QVERIFY(second.waitForDisconnected(2000));

    QCOMPARE(server.connectionCount(), 2);
}

// A local process that opens the loopback port without the token must not be
// able to block the real client from connecting and authenticating afterwards.
void TestHelperIpc::preAuthSquatterDoesNotBlock()
{
    const QString token = QStringLiteral("real-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    // Squatter connects first and never sends a valid hello.
    QTcpSocket squatter;
    squatter.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(squatter.waitForConnected(3000));
    server.acceptPending();

    // The real client connects; it displaces the idle squatter and can then
    // authenticate normally.
    QTcpSocket client;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(client.waitForConnected(3000));
    server.acceptPending();
    QVERIFY(squatter.waitForDisconnected(2000));

    QJsonObject hello;
    hello["cmd"] = "hello";
    hello["token"] = token;
    client.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(client.waitForReadyRead(3000));
    const auto doc = QJsonDocument::fromJson(client.readLine());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value("ev").toString(), QStringLiteral("ready"));
    QVERIFY(server.authed());
}

void TestHelperIpc::oversizedBufferClosesConnection()
{
    MockHelperServer server(QStringLiteral("tok"));
    QVERIFY(server.listen());

    QTcpSocket client;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(client.waitForConnected(3000));
    server.acceptPending();

    client.write(QByteArray(vpn_helper::kMaxIpcLineBytes + 1, 'A'));
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    for (int i = 0; i < 300; ++i) {
        if (client.state() == QAbstractSocket::UnconnectedState)
            break;
        QTest::qWait(10);
    }
    QCOMPARE(client.state(), QAbstractSocket::UnconnectedState);
}

void TestHelperIpc::connectDisconnectFlow()
{
    const QString token = QStringLiteral("flow-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    QTcpSocket client;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(client.waitForConnected(3000));
    server.acceptPending();

    QJsonObject hello;
    hello["cmd"] = "hello";
    hello["token"] = token;
    client.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(client.waitForReadyRead(3000));
    client.readLine();

    QJsonObject connectCmd;
    connectCmd["cmd"] = "connect";
    connectCmd["configPath"] = "/tmp/test.toml";
    client.write(QJsonDocument(connectCmd).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));

    QString lastState;
    for (int attempt = 0; attempt < 10 && lastState != QLatin1String("Connected"); ++attempt) {
        if (client.bytesAvailable() == 0 && !client.waitForReadyRead(3000))
            break;
        while (client.canReadLine()) {
            const auto doc = QJsonDocument::fromJson(client.readLine());
            if (!doc.isObject())
                continue;
            const QJsonObject ev = doc.object();
            if (ev.value("ev").toString() == QLatin1String("state"))
                lastState = ev.value("state").toString();
            if (ev.value("ev").toString() == QLatin1String("stats")) {
                QCOMPARE(ev.value("up").toDouble(), 1024.0);
                QCOMPARE(ev.value("down").toDouble(), 2048.0);
            }
        }
    }
    QCOMPARE(lastState, QStringLiteral("Connected"));
    QCOMPARE(server.lastCmd(), QStringLiteral("connect"));

    QJsonObject disconnectCmd;
    disconnectCmd["cmd"] = "disconnect";
    client.write(QJsonDocument(disconnectCmd).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    lastState.clear();
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (client.bytesAvailable() == 0)
            QVERIFY(client.waitForReadyRead(3000));
        while (client.canReadLine()) {
            const auto doc = QJsonDocument::fromJson(client.readLine());
            if (!doc.isObject())
                continue;
            const QJsonObject ev = doc.object();
            if (ev.value("ev").toString() == QLatin1String("state"))
                lastState = ev.value("state").toString();
        }
        if (lastState == QStringLiteral("Disconnected"))
            break;
    }
    QCOMPARE(lastState, QStringLiteral("Disconnected"));
}

void TestHelperIpc::preAuthCommandsRejected()
{
    MockHelperServer server(QStringLiteral("tok"));
    QVERIFY(server.listen());

    QTcpSocket client;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(client.waitForConnected(3000));
    server.acceptPending();

    QJsonObject connectCmd;
    connectCmd["cmd"] = "connect";
    client.write(QJsonDocument(connectCmd).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(client.waitForDisconnected(3000));
    QVERIFY(!server.authed());
}

QTEST_MAIN(TestHelperIpc)
#include "test_helper_ipc.moc"
