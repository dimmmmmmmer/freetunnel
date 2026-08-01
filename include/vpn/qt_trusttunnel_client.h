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
#include <mutex>
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
    bool loadConfigFromToml(const QString &tomlContent);
    void setReconnectBoundsMs(int initialDelayMs, int maxDelayMs);

    Q_INVOKABLE void connectVpn();
    Q_INVOKABLE void beginConnect(const QString &configToml);
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE bool isConnected() const;
    Q_INVOKABLE State state() const;
    Q_INVOKABLE void setLogLevel(const QString &level); // applied live (no reconnect)
    void setExcludedRoutes(const std::vector<std::string> &excludeRoutes);
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
    void tunnelStats(quint64 upload, quint64 download); // per-connection delta bytes

private slots:
    void doConnectAttemptInThread();
    void pollCoreLogFile();

private:
    // Shared with every connect attempt and with the core callbacks. An
    // ABANDONED attempt (and the core client it left running) resumes long
    // after this object may already be destroyed, so its "is my result still
    // wanted / does the owner still exist" check must not read the object.
    struct LifetimeGuard {
        std::mutex mutex; // held while a callback hops back to the owner
        bool alive = true; // guarded by mutex
        std::atomic<quint64> attemptGen{0};
    };
    using GuardPtr = std::shared_ptr<LifetimeGuard>;
    static bool attemptIsStale(const GuardPtr &guard, quint64 attemptGen);

    ag::VpnCallbacks makeCallbacks(const GuardPtr &guard);
    void doConnectAttempt(const GuardPtr &guard, quint64 attemptGen);
    bool joinOrAbandonConnectThread(int waitMs);
    void startConnectAttempt();
    void scheduleReconnect(const QString &reason);
    void setState(State s);
    void handleCoreStateChanged(ag::VpnSessionState coreState, int errCode, const QString &errText);
    void handleCoreConnected();
    void handleCoreConnecting();
    void handleCoreRecovery(const QString &reason);
    void handleCoreWaitingForNetwork();
    void handleCoreDisconnected(int errCode, const QString &errText);
    void setConfigLocked(ag::TrustTunnelConfig config);
    void applyCoreLogPathToConfig();
    void applyCoreLogPathToConfigLocked();
    void startCoreLogTail();
    void stopCoreLogTail();
    void teardownClient();
    void checkFdHealth();
    bool reloadStoredConfigIfNeeded();
    bool ensureClientReady(const GuardPtr &guard, quint64 attemptGen);
    void failConnectFatal(const QString &qErr, bool privilegeHint);
    bool attemptTunnelConnect(const GuardPtr &guard, quint64 attemptGen);
    void teardownIfReconnecting(bool isReconnect);
    void forceFdReconnect(const QString &logReason, const QString &userReason);
    void protectOutboundSocket(ag::SocketProtectEvent *event);
    static int countOpenFds();
    static int getFdLimit();

    std::unique_ptr<ag::TrustTunnelClient> m_client;
    std::unique_ptr<ag::AutoNetworkMonitor> m_networkMonitor;
    // Guards the config working set: m_config, m_lastConfigToml,
    // m_extraExcludedRoutes, m_extraExclusions, m_originalExclusions,
    // m_selectiveMode, m_killSwitch, m_loggingEnabled, m_logLevel and
    // m_coreLogPath. The IPC setters run on this object's thread while the
    // connect thread moves the config into the core client — the
    // unsynchronised move-out used to corrupt the heap in a root process.
    // Never held across a blocking core call, so it cannot deadlock the join.
    mutable std::mutex m_configMutex;
    std::optional<ag::TrustTunnelConfig> m_config;
    QString m_lastConfigToml; // in-memory config for reconnect without on-disk secrets
    std::vector<std::string> m_extraExcludedRoutes;
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
    // Heap-allocated and unparented on purpose: a connect attempt stuck inside
    // a blocking native call is ABANDONED (thread pointer dropped, deleted on
    // finished) rather than terminate()d — a parented member thread would be
    // deleted while still running when this object dies.
    QThread *m_connectThread = nullptr;
    // Written from m_connectThread as well as this object's thread: a stale
    // read made connectVpn() see Connecting after a worker-thread Error and
    // silently drop the user's Connect click.
    std::atomic<State> m_state{State::Disconnected};
    bool m_autoReconnect = true;
    // Written from the object's thread, read from m_connectThread (and vice
    // versa for error paths) — must be atomic to avoid torn/stale reads.
    std::atomic<bool> m_stopRequested{false};
    // Incremented for every new core client (and again on teardown); core
    // callbacks capture the value and stale queued events are dropped.
    std::atomic<quint64> m_sessionGen{0};
    // Bumped by every disconnectVpn(); beginConnect's delayed start compares it
    // so a disconnect arriving inside the delay window cancels the start.
    std::atomic<quint64> m_disconnectGen{0};
    // Holds the attempt generation (incremented per connect attempt and when a
    // stuck attempt is abandoned) plus this object's liveness, so an abandoned
    // attempt can drop its result without dereferencing a destroyed owner.
    GuardPtr m_guard = std::make_shared<LifetimeGuard>();
    int m_stuckJoinWaitMs = 15000; // join timeout before abandoning (test hook)
    bool m_everConnected = false; // true after first successful connect in this session
    int m_reconnectDelayMs = 1000;
    int m_reconnectMaxMs = 30000;
    ag::LogLevel m_logLevel = ag::LOG_LEVEL_INFO;
    QString m_coreLogPath;
    qint64 m_coreLogOffset = 0;
    QByteArray m_coreLogLineBuffer;
    // Stamped on m_connectThread, read on this object's thread when a core
    // disconnect arrives — atomic for the same reason as m_state.
    std::atomic<std::chrono::steady_clock::time_point> m_lastConnectAttempt{};
#ifdef Q_OS_WIN
    uint32_t m_winPhysicalIfIndex = 0;
#endif
};
