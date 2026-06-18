#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>

// Local-socket mock of vpn_helper_server JSON protocol (no VPN core). Used by
// test_helper_ipc to validate the IPC contract the GUI relies on.
class MockHelperServer : public QObject {
    Q_OBJECT
public:
    explicit MockHelperServer(const QString &token, QObject *parent = nullptr);
    ~MockHelperServer();

    bool listen();
    QString serverName() const { return m_name; }
    bool isListening() const { return m_server.isListening(); }
    void acceptPending(int timeoutMs = 3000);
    bool waitForClientData(int timeoutMs = 3000);
    bool processAvailable();
    bool authed() const { return m_authed; }
    QString lastCmd() const { return m_lastCmd; }
    int connectionCount() const { return m_connectionCount; }

private:
    void adoptSocket(QLocalSocket *s);
    void send(const QJsonObject &e);
    void onReadyRead();
    void handle(const QJsonObject &c);

    QString m_token;
    QString m_name;
    QLocalServer m_server;
    QLocalSocket *m_sock = nullptr;
    QByteArray m_buf;
    bool m_authed = false;
    QString m_lastCmd;
    int m_connectionCount = 0;

signals:
    void quitRequested();
};
