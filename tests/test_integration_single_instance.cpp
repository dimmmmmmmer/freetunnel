#include <QtTest>

#include <QLocalServer>
#include <QLocalSocket>

class TestIntegrationSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void forwardsControlCommand();
    void focusWhenEmptyPayload();
};

void TestIntegrationSingleInstance::forwardsControlCommand()
{
    const QString key = QStringLiteral("FreeTunnelInstanceTest-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(key);

    QLocalServer server;
    QVERIFY(server.listen(key));
    server.setSocketOptions(QLocalServer::UserAccessOption);

    QLocalSocket client;
    client.connectToServer(key);
    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QVERIFY(client.waitForConnected(2000));

    const QByteArray payload = QStringLiteral("freetunnel://toggle").toUtf8();
    QCOMPARE(client.write(payload), payload.size());
    client.flush();
    QVERIFY(peer->waitForReadyRead(2000));
    QCOMPARE(QString::fromUtf8(peer->readAll()), QStringLiteral("freetunnel://toggle"));
    peer->deleteLater();
}

void TestIntegrationSingleInstance::focusWhenEmptyPayload()
{
    const QString key = QStringLiteral("FreeTunnelInstanceFocus-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(key);

    QLocalServer server;
    QVERIFY(server.listen(key));

    QLocalSocket client;
    client.connectToServer(key);
    QVERIFY(server.waitForNewConnection(2000));
    QLocalSocket *peer = server.nextPendingConnection();
    QVERIFY(peer);
    QVERIFY(client.waitForConnected(2000));

    const QByteArray payload = QStringLiteral("focus").toUtf8();
    QCOMPARE(client.write(payload), payload.size());
    client.flush();
    QVERIFY(peer->waitForReadyRead(2000));
    QCOMPARE(QString::fromUtf8(peer->readAll()), QStringLiteral("focus"));
    peer->deleteLater();
}

QTEST_MAIN(TestIntegrationSingleInstance)
#include "test_integration_single_instance.moc"
