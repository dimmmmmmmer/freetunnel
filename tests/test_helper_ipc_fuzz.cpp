#include <QtTest>

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpSocket>

#include "helper_ipc_mock_server.h"
#include "vpn/vpn_helper_protocol.h"

class TestHelperIpcFuzz : public QObject {
    Q_OBJECT

private slots:
    void preAuthGarbageNeverCrashes();
    void postAuthGarbageNeverCrashes();
    void malformedJsonLinesIgnoredAfterAuth();
    void randomCommandFieldsAfterAuth();
    void oversizedLinesCloseConnection();
};

namespace {

QByteArray randomBytes(QRandomGenerator &rng, int maxLen)
{
    const int len = rng.bounded(1, maxLen + 1);
    QByteArray out(len, Qt::Uninitialized);
    for (int i = 0; i < len; ++i)
        out[i] = static_cast<char>(rng.bounded(256));
    return out;
}

bool authenticateClient(MockHelperServer &server, QTcpSocket &client, const QString &token)
{
    if (!server.listen())
        return false;
    client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    if (!client.waitForConnected(3000))
        return false;
    server.acceptPending();

    QJsonObject hello;
    hello["cmd"] = "hello";
    hello["token"] = token;
    client.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    if (!server.waitForClientData(3000))
        return false;
    if (!client.waitForReadyRead(3000))
        return false;
    client.readLine();
    return server.authed();
}

} // namespace

void TestHelperIpcFuzz::preAuthGarbageNeverCrashes()
{
    for (quint32 seed = 0; seed < 40; ++seed) {
        QRandomGenerator rng(seed);
        MockHelperServer server(QStringLiteral("fuzz-pre"));
        QVERIFY(server.listen());

        QTcpSocket client;
        client.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
        QVERIFY(client.waitForConnected(3000));
        server.acceptPending();

        client.write(randomBytes(rng, 4096));
        client.write("\n", 1);
        client.flush();
        QVERIFY(server.waitForClientData(3000));
        for (int i = 0; i < 10; ++i)
            server.processAvailable();

        QVERIFY(!server.authed());
        QVERIFY(server.isListening());
    }
}

void TestHelperIpcFuzz::postAuthGarbageNeverCrashes()
{
    for (quint32 seed = 0; seed < 40; ++seed) {
        QRandomGenerator rng(seed + 1000);
        const QString token = QStringLiteral("fuzz-post");
        MockHelperServer server(token);
        QTcpSocket client;
        QVERIFY(authenticateClient(server, client, token));

        for (int line = 0; line < 8; ++line) {
            client.write(randomBytes(rng, 2048));
            client.write("\n", 1);
        }
        client.flush();
        QVERIFY(server.waitForClientData(3000));
        for (int i = 0; i < 10; ++i)
            server.processAvailable();
        QVERIFY(server.isListening());
    }
}

void TestHelperIpcFuzz::malformedJsonLinesIgnoredAfterAuth()
{
    MockHelperServer server(QStringLiteral("fuzz-json"));
    QTcpSocket client;
    QVERIFY(authenticateClient(server, client, QStringLiteral("fuzz-json")));

    client.write("not-json\n");
    client.write("{]\n");
    client.write("42\n");
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    for (int i = 0; i < 10; ++i)
        server.processAvailable();

    QJsonObject connectCmd;
    connectCmd["cmd"] = "connect";
    client.write(QJsonDocument(connectCmd).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    QCOMPARE(server.lastCmd(), QStringLiteral("connect"));
}

void TestHelperIpcFuzz::randomCommandFieldsAfterAuth()
{
    for (quint32 seed = 0; seed < 30; ++seed) {
        QRandomGenerator rng(seed + 2000);
        MockHelperServer server(QStringLiteral("fuzz-cmd"));
        QTcpSocket client;
        QVERIFY(authenticateClient(server, client, QStringLiteral("fuzz-cmd")));

        QJsonObject cmd;
        cmd["cmd"] = QString::fromUtf8(randomBytes(rng, 32));
        cmd["token"] = QString::fromUtf8(randomBytes(rng, 64));
        cmd["configToml"] = QString::fromUtf8(randomBytes(rng, 512));
        cmd["domains"] = QJsonArray{
            QString::fromUtf8(randomBytes(rng, 16)),
            QString::fromUtf8(randomBytes(rng, 24)),
        };
        client.write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + '\n');
        client.flush();
        QVERIFY(server.waitForClientData(3000));
        for (int i = 0; i < 10; ++i)
            server.processAvailable();
        QVERIFY(server.isListening());
    }
}

void TestHelperIpcFuzz::oversizedLinesCloseConnection()
{
    MockHelperServer server(QStringLiteral("fuzz-big"));
    QTcpSocket client;
    QVERIFY(authenticateClient(server, client, QStringLiteral("fuzz-big")));

    client.write(QByteArray(vpn_helper::kMaxIpcLineBytes + 1, 'x'));
    client.flush();
    QVERIFY(server.waitForClientData(3000));
    for (int i = 0; i < 300; ++i) {
        if (client.state() == QAbstractSocket::UnconnectedState)
            break;
        QTest::qWait(10);
    }
    QCOMPARE(client.state(), QAbstractSocket::UnconnectedState);
}

QTEST_MAIN(TestHelperIpcFuzz)
#include "test_helper_ipc_fuzz.moc"
