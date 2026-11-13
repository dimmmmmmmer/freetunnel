// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>

// A remote subscription/list server must not keep growing Qt's HTTP parser
// buffers by withholding the end of a response line/header block.
class TestQtHttpLimits : public QObject {
    Q_OBJECT
private slots:
    void unterminatedResponseIsRejected_data() {
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("status-line") << QByteArray("HTTP/1.1 200 ") + QByteArray(2048, 'x');
        QTest::newRow("header-block")
            << QByteArray("HTTP/1.1 200 OK\r\nX-Large: ") + QByteArray(300 * 1024, 'x');
    }

    void unterminatedResponseIsRejected() {
        QFETCH(QByteArray, response);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server, response] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                socket->write(response);
                // Deliberately leave the connection open and the header unfinished.
            }
        });
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort())));
        request.setTransferTimeout(10000);
        auto *reply = manager.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(reply->error() != QNetworkReply::NoError);
        QVERIFY(reply->error() != QNetworkReply::TimeoutError);
    }
};

QTEST_GUILESS_MAIN(TestQtHttpLimits)
#include "test_qt_http_limits.moc"
