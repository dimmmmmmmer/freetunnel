// cppcheck-suppress-file missingIncludeSystem
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>

#ifdef _WIN32
// Windows SDK doesn't define POSIX iovec; define a minimal version before vpn.h uses it.
#ifndef IOVEC_DEFINED_QT
#define IOVEC_DEFINED_QT
struct iovec {
    void *iov_base; // cppcheck-suppress unusedStructMember
    size_t iov_len; // cppcheck-suppress unusedStructMember
};
#endif
#endif

#include "vpn/trusttunnel/client.h"
#include "vpn/trusttunnel/auto_network_monitor.h"
#include "vpn/trusttunnel/config.h"
#include "vpn/vpn.h" // for ag::iovec on Windows

class QtTrustTunnelClient : public QObject {
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

    explicit QtTrustTunnelClient(QObject *parent = nullptr);
    ~QtTrustTunnelClient();

    void setConfig(ag::TrustTunnelConfig config);
    bool loadConfigFromFile(const QString &path);
    bool loadConfigFromToml(const QString &tomlContent);
    void setAutoReconnectEnabled(bool enabled);
    void setReconnectBoundsMs(int initialDelayMs, int maxDelayMs);

    Q_INVOKABLE void connectVpn();
    Q_INVOKABLE void beginConnect(const QString &configToml, const QString &configPath);
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE bool isConnected() const;
    Q_INVOKABLE State state() const;
    Q_INVOKABLE void setLogLevel(const QString &level); // applied live (no reconnect)
    void setRoutingRules(const std::vector<std::string> &includeRoutes,
            const std::vector<std::string> &excludeRoutes);
    void setCustomDns(const std::vector<std::string> &dnsServers);
    Q_INVOKABLE void setExtraExclusionDomains(const QStringList &domains);
    Q_INVOKABLE void setExcludedRouteStrings(const QStringList &routes);
    void setExtraExclusions(const std::vector<std::string> &exclusions);
    Q_INVOKABLE void setVpnMode(bool selective); // selective = route only the exclusions list
    Q_INVOKABLE void setKillSwitch(bool enabled);
    Q_INVOKABLE void setSessionLogging(const QString &path, bool enabled);

signals:
    void stateChanged(QtTrustTunnelClient::State state);
    void vpnConnected();
    void vpnDisconnected();
    void vpnError(const QString &msg);
    void connectProgress(const QString &step);
    void connectionInfo(const QString &msg);
    void coreLogLine(const QString &line);
    void clientOutput(const QString &bytes); // bytes in chunk
    void tunnelStats(quint64 upload, quint64 download); // per-connection delta bytes

private slots:
    void doConnectAttemptInThread();
    void pollCoreLogFile();

private:
    ag::VpnCallbacks makeCallbacks();
    void doConnectAttempt();
    void startConnectAttempt();
    void scheduleReconnect(const QString &reason);
    void setState(State s);
    void handleCoreStateChanged(ag::VpnSessionState coreState, int errCode, const QString &errText);
    void handleCoreConnected();
    void handleCoreConnecting();
    void handleCoreRecovery(const QString &reason);
    void handleCoreWaitingForNetwork();
    void handleCoreDisconnected(int errCode, const QString &errText);
    void applyCoreLogPathToConfig();
    void startCoreLogTail();
    void stopCoreLogTail();
    void teardownClient();
    void checkFdHealth();
    bool reloadStoredConfigIfNeeded();
    bool ensureClientReady();
    void failConnectFatal(const QString &qErr, bool privilegeHint);
    bool attemptTunnelConnect();
    void teardownIfReconnecting(bool isReconnect);
    void forceFdReconnect(const QString &logReason, const QString &userReason);
    void protectOutboundSocket(ag::SocketProtectEvent *event);
    static int countOpenFds();
    static int getFdLimit();

    std::unique_ptr<ag::TrustTunnelClient> m_client;
    std::unique_ptr<ag::AutoNetworkMonitor> m_networkMonitor;
    std::optional<ag::TrustTunnelConfig> m_config;
    QString m_lastConfigPath; // stored so we can reload config after disconnect
    QString m_lastConfigToml; // in-memory config for reconnect without on-disk secrets
    std::vector<std::string> m_extraIncludedRoutes;
    std::vector<std::string> m_extraExcludedRoutes;
    std::vector<std::string> m_customDns;
    std::vector<std::string> m_extraExclusions;
    std::string m_originalExclusions; // exclusions from config file before our additions
    bool m_selectiveMode = false;     // route only the exclusions (vs bypass them)
    bool m_killSwitch = false;
    bool m_loggingEnabled = true;
    QTimer m_reconnectTimer;
    QTimer m_fdWatchdogTimer;
    int m_fdBaseline = -1; // open fd count right after connect (for leak detection)
    QTimer m_networkWaitTimer;   // fires if we stay in WaitingForNetwork too long
    QTimer *m_coreLogPoll = nullptr;
    QThread m_connectThread;
    State m_state = State::Disconnected;
    bool m_autoReconnect = true;
    // Written from the object's thread, read from m_connectThread (and vice
    // versa for error paths) — must be atomic to avoid torn/stale reads.
    std::atomic<bool> m_stopRequested{false};
    // Incremented for every new core client (and again on teardown); core
    // callbacks capture the value and stale queued events are dropped.
    std::atomic<quint64> m_sessionGen{0};
    bool m_everConnected = false; // true after first successful connect in this session
    int m_reconnectDelayMs = 1000;
    int m_reconnectMaxMs = 30000;
    ag::LogLevel m_logLevel = ag::LOG_LEVEL_INFO;
    QString m_coreLogPath;
    qint64 m_coreLogOffset = 0;
    QByteArray m_coreLogLineBuffer;
    std::chrono::steady_clock::time_point m_lastConnectAttempt{};
#ifdef Q_OS_WIN
    uint32_t m_winPhysicalIfIndex = 0;
#endif
};
