#pragma once

// GUI-side proxy for the VPN. Spawns the privileged helper (the same binary run
// with --helper, elevated once per session) and drives it over a user-local
// socket. Exposes the same signal/slot surface the Backend expects, so the GUI
// never runs the VPN core (or root) in-process.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <string>
#include <vector>

class QLocalSocket;
class QProcess;
class QTimer;
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
    void setExcludedRoutes(const std::vector<std::string> &routes);
    void setVpnMode(bool selective);
    void setKillSwitch(bool enabled);

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
    bool spawnElevatedHelper(const QString &socketName, const QString &tokenPath, QString *err);
    void abortStartup();              // cancel a pending elevation/handshake
    void clearTokenFile();            // remove the on-disk auth token (best-effort)
    void send(const QJsonObject &obj);
    void onReadyRead();
    void onSocketConnected();
    void handleEvent(const QJsonObject &ev);
    void setState(State s);
    void fail(const QString &msg);

    QProcess *m_proc = nullptr;
    QLocalSocket *m_sock = nullptr;
    QString m_socketName;
    QString m_token;
    QString m_tokenPath; // 0600 file used to hand the token to the elevated helper
    QString m_configPath;
    std::vector<std::string> m_exclusions;
    std::vector<std::string> m_excludedRoutes;
    bool m_selective = false;
    bool m_killSwitch = false;
    State m_state = State::Disconnected;
    bool m_helloAcked = false;
    bool m_connectPending = false;
    bool m_starting = false; // helper spawn in progress (avoids re-prompting)
    QTimer *m_attempt = nullptr; // retries the socket connect while the helper boots
    int m_tries = 0;
    QByteArray m_buf;
};
