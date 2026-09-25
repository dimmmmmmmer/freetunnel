// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QHash>
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
// Drive the mutual-auth handshake from the client side: send a nonce, verify the
// server's proof, answer with ours. Returns false if any step fails. Tests that
// want to exercise a REJECTED peer should not use this.
bool mockHelperHandshake(class MockHelperServer &server, QTcpSocket &client,
                         const QString &token);

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
    // The last WHOLE message received for a given command, empty if that command
    // has not arrived yet. lastCmd() alone only ever proved that a name went past
    // — it could not tell a kill switch sent as true from one sent as false, or
    // from one whose payload key had been renamed and now decodes to false. The
    // settings this carries decide whether traffic is blocked and what is routed,
    // so the double has to keep the values, not just the verbs.
    QJsonObject lastMessageFor(const QString &cmd) const { return m_lastByCmd.value(cmd); }
    int connectionCount() const { return m_connectionCount; }
    int connectCount() const { return m_connectCount; }

    // Scripted outcomes for the Backend tests, keyed on a value quoted in the
    // connect's config: its hostname, or its password. A failing connect gets
    // Connecting, then Reconnecting with `error`, which is how the core reports
    // an attempt it will keep retrying. A refused one gets `error` and no state
    // at all, as when the core is already in Error and cannot load the config.
    void failConnectsWith(const QString &value, const QString &error) { m_failing.insert(value, error); }
    void refuseConnectsWith(const QString &value, const QString &error) { m_refusing.insert(value, error); }
    // The old session reports `error` while a disconnect is tearing it down.
    void setTeardownError(const QString &error) { m_teardownError = error; }

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
    QString m_challenge; // nonce we asked the client to answer
    QString m_lastCmd;
    QHash<QString, QJsonObject> m_lastByCmd;
    int m_connectionCount = 0;
    int m_connectCount = 0;
    bool m_tunnelUp = false;
    QHash<QString, QString> m_failing;
    QHash<QString, QString> m_refusing;
    QString m_teardownError;

signals:
    void quitRequested();
};
