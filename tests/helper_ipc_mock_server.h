#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>

// Loopback mock of vpn_helper_server JSON protocol (no VPN core). Used by
// test_helper_ipc to validate the IPC contract the GUI relies on.
//
// Like the real helper, only one client is ever served. The real server tracks
// every pre-auth connection and lets the one that presents a valid token win
// (closing the rest); this mock uses the simpler, deterministic equivalent of
// "the newest pre-auth connection takes the slot" so a token-less squatter that
// connected first can never block the real client from authenticating.
class MockHelperServer : public QObject {
    Q_OBJECT
public:
    explicit MockHelperServer(const QString &token, QObject *parent = nullptr);
    ~MockHelperServer();

    bool listen();
    quint16 port() const;
    bool isListening() const { return m_server.isListening(); }
    void acceptPending(int timeoutMs = 3000);
    bool waitForClientData(int timeoutMs = 3000);
    bool processAvailable();
    bool authed() const { return m_authed; }
    QString lastCmd() const { return m_lastCmd; }
    int connectionCount() const { return m_connectionCount; }

private:
    void adoptSocket(QTcpSocket *s);
    void send(const QJsonObject &e);
    void onReadyRead();
    void handle(const QJsonObject &c);

    QString m_token;
    QTcpServer m_server;
    QTcpSocket *m_sock = nullptr;
    QByteArray m_buf;
    bool m_authed = false;
    QString m_lastCmd;
    int m_connectionCount = 0;
    int m_connectCount = 0;
    bool m_tunnelUp = false;

signals:
    void quitRequested();
};
