#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QString>

// Minimal loopback HTTP/1.1 server for UpdateChecker integration tests.
class MockHttpServer : public QObject {
    Q_OBJECT
public:
    struct Route {
        QByteArray body;
        int status = 200;
        QByteArray contentType = QByteArrayLiteral("application/json");
    };

    explicit MockHttpServer(QObject *parent = nullptr);

    bool listen();
    quint16 port() const;
    void setRoute(const QString &path, const Route &route);
    QString baseUrl() const;

private:
    void onNewConnection();

    QTcpServer m_server;
    QHash<QString, Route> m_routes;
};
