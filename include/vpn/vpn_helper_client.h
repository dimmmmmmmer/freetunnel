// cppcheck-suppress-file missingIncludeSystem
#pragma once

// GUI-side proxy for the VPN. Spawns the privileged helper (the same binary run
// with --helper, elevated once per session) and drives it over loopback TCP.
// Exposes the same signal/slot surface the Backend expects, so the GUI never
// runs the VPN core (or root) in-process.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <optional>
#include <string>
#include <vector>

class QTcpSocket;
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

    // Removes the token files an earlier run left in @p dir: regular `.fthelper-*`
    // files of this user's, last changed more than @p minAgeSecs ago. The helper
    // only reads a token file, and the GUI removes its own once the helper has
    // answered or the attempt is given up, so one is left only when the GUI ended
    // in between. Runs as the user, in the GUI; the constructor calls it on the
    // directory the token files go to. Returns how many it removed.
    static int removeStaleTokenFiles(const QString &dir, qint64 minAgeSecs);

    bool loadConfigFromToml(const QString &tomlContent);
    void setExtraExclusions(const std::vector<std::string> &exclusions);
    void setExcludedRoutes(const std::vector<std::string> &routes);
    void setVpnMode(bool selective);
    // Per-application split tunnelling; read the same way as the routes list,
    // with setVpnMode deciding which way a match goes.
    void setAppRules(const std::vector<std::string> &rules);
    // The three above as one command, which is how an edit made while connected
    // has to travel: the helper hands them to the running session together, so
    // nothing is routed by half of an edit.
    void setSplitRouting(const std::vector<std::string> &exclusions, bool selective,
                         const std::vector<std::string> &appRules);
    void setKillSwitch(bool enabled);
    void setLogLevel(const QString &level); // "warn"/"info"/… applied live, no reconnect
    void setSessionLogging(bool enabled);

    void connectVpn();
    void disconnectVpn();
    void shutdown();
    State state() const { return m_state; }
    // The helper is up and has authenticated. Until then a connect is queued, and
    // it goes out with every current setting (handleReadyEvent) once it is.
    bool helperReady() const { return m_helloAcked; }
    // Whether a setting the running session was BUILT with has changed since the
    // last connect went out: the excluded routes or the kill switch. Those go into
    // the system's routing and the traffic block when the session is built, so
    // only a new session applies them. The split-tunnelling rules and the mode
    // reach a running session live, and are not among them. True when there is no
    // such connect: none has gone out, or the GUI has ended its session or lost
    // the helper since.
    bool sessionSettingsChanged() const;

signals:
    void stateChanged(VpnHelperClient::State state);
    void tunnelStats(quint64 upload, quint64 download);
    void connectionInfo(const QString &msg);
    void connectProgress(const QString &step);
    void coreLogLine(const QString &line);
    void vpnError(const QString &msg);

private:
    bool ensureHelper();
    bool spawnElevatedHelper(quint16 port, const QString &tokenPath, QString *err);
    void resetHelperTransport();
    bool configureTestHelper();
    bool configureProductionHelper();
    // Fail fast when the elevation prompt is answered with "no", instead of
    // polling a port nothing will ever listen on for a full minute.
    void watchElevationOutcome();
    void wireHelperSocket(bool testHelper);
    void startHelperConnectRetry();
    void abortStartup();
    void clearTokenFile();
    void send(const QJsonObject &obj);
    void onReadyRead();
    void onSocketConnected();
    void handleEvent(const QJsonObject &ev);
    // The first half of the mutual handshake, split out of handleEvent() so that
    // the peer check and the event dispatch can each be read on their own.
    void handleChallengeEvent(const QJsonObject &ev);
    void handleReadyEvent();
    void setState(State s);
    void fail(const QString &msg);

    // What a session is built from and keeps, as opposed to what reaches it
    // live: the rules, the mode and the log level.
    struct SessionSettings {
        std::vector<std::string> excludedRoutes;
        bool killSwitch = false;
        bool operator==(const SessionSettings &) const = default;
    };
    SessionSettings sessionSettings() const;

    QProcess *m_proc = nullptr;
    QTcpSocket *m_sock = nullptr;
    quint16 m_tcpPort = 0;
    QString m_token;
    QString m_guiNonce;        // our half of the mutual-auth handshake
    bool m_peerProven = false; // peer answered our challenge with a valid proof
    QString m_tokenPath;
    QString m_configToml;
    std::vector<std::string> m_exclusions;
    std::vector<std::string> m_excludedRoutes;
    bool m_selective = false;
    std::vector<std::string> m_appRules;
    bool m_killSwitch = false;
    // As the last connect carried them. Cleared when the GUI ends that session or
    // loses the helper. A session the core ends by itself (an error, a drop it
    // gave up on) leaves it set until the next connect replaces it; nothing is
    // built in between for it to be wrong about.
    std::optional<SessionSettings> m_sessionBuiltWith;
    QString m_logLevel = QStringLiteral("warn");
    bool m_loggingEnabled = true;
    State m_state = State::Disconnected;
    bool m_helloAcked = false;
    bool m_connectPending = false;
    bool m_starting = false;
    QTimer *m_attempt = nullptr;
    // Deadline for the peer to prove itself once the socket is up.
    //
    // Connecting disarmed every other failure detector: the retry budget that
    // produces the only user-visible "could not reach the helper" message deletes
    // itself on ConnectedState, and the elevation-outcome watcher returns early
    // once the socket is connected. So a port answered by anything that accepts
    // and then says nothing left the window on "Connecting…" with no error, ever
    // — after the user had already typed their administrator password. The port
    // is a random pick that nothing reserves, so an unrelated local service is
    // enough to cause it by accident, and a local process camping on the range is
    // enough to cause it on purpose.
    QTimer *m_handshake = nullptr;
    int m_tries = 0;
    QByteArray m_buf;
};
