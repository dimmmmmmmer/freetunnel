#pragma once

// GUI-side proxy for the VPN. Spawns the privileged helper (the same binary run
// with --helper, elevated once per session) and drives it over a localhost
// socket. Exposes the same signal/slot surface the Backend expects, so the GUI
// never runs the VPN core (or root) in-process.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <string>
#include <vector>

class QProcess;
class QTcpSocket;
class QJsonObject;

class VpnHelperClient : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        WaitingForNetwork,
        Disconnecting,
        Error,
    };
    Q_ENUM(State)

    explicit VpnHelperClient(QObject *parent = nullptr);
    ~VpnHelperClient() override;

    // Stores the config path to hand to the helper on connect. Always succeeds;
    // real load errors arrive asynchronously via vpnError.
    bool loadConfigFromFile(const QString &path);
    void setExtraExclusions(const std::vector<std::string> &exclusions);

    void connectVpn();    // spawns/elevates the helper if needed, then connects
    void disconnectVpn();
    State state() const { return m_state; }

signals:
    void stateChanged(VpnHelperClient::State state);
    void tunnelStats(quint64 upload, quint64 download);
    void connectionInfo(const QString &msg);
    void vpnError(const QString &msg);

private:
    bool ensureHelper();              // spawn elevated helper + open socket
    bool spawnElevatedHelper(quint16 port, const QString &token, QString *err);
    void send(const QJsonObject &obj);
    void onReadyRead();
    void onSocketConnected();
    void handleEvent(const QJsonObject &ev);
    void setState(State s);
    void fail(const QString &msg);

    QProcess *m_proc = nullptr;
    QTcpSocket *m_sock = nullptr;
    quint16 m_port = 0;
    QString m_token;
    QString m_configPath;
    std::vector<std::string> m_exclusions;
    State m_state = State::Disconnected;
    bool m_helloAcked = false;
    bool m_connectPending = false;
    bool m_starting = false; // helper spawn in progress (avoids re-prompting)
    QByteArray m_buf;
};
