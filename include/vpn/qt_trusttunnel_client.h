// cppcheck-suppress-file missingIncludeSystem
#pragma once
#include <QObject>
#include <QString>
#include <QList>
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

    // The only way in for a config. This object runs in the elevated helper, and
    // what it must refuse or clear is done here, so there is deliberately no
    // setter that takes a config already built.
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
    // Per-application split tunnelling. The list means the same as the routes
    // and domains lists: in general mode these apps leave the tunnel, in
    // selective mode they are the only ones that enter it.
    Q_INVOKABLE void setAppRules(const QStringList &rules);
    // The three split-tunnelling settings as one change: the domain and address
    // rules, the mode and the program rules. A running session takes the rules and
    // the mode live (vendor patch 03); each of the setters above also does, but
    // one at a time, so between two of them the session would route by a mix of
    // old and new that is neither what the user had nor what they asked for.
    Q_INVOKABLE void setSplitRouting(const QStringList &domains, bool selective,
                                     const QStringList &appRules);
    Q_INVOKABLE void setKillSwitch(bool enabled);
    // Whether the config's own killswitch_allow_ports reach the core. They do not
    // unless the user turned this on; see clearKeysRootMustNotTakeFromAConfig.
    Q_INVOKABLE void setKillSwitchPortsFromConfig(bool enabled);
    // Whether the core writes a session log at all. The PATH is ours to choose —
    // it is never accepted from outside, see the note in the .cpp.
    Q_INVOKABLE void setSessionLogging(bool enabled);

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
    void followUplink(); // qt_trusttunnel_uplink.cpp

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
    // What the core routes by, and the part of a session that can change while it
    // runs: the mode, and the exclusion list (the config's own entries, then the
    // domain and address rules).
    struct Routing {
        ag::VpnMode mode = ag::VPN_MODE_GENERAL;
        std::string exclusions;
        bool operator==(const Routing &) const = default;
    };
    // One connect attempt, owned entirely by the worker thread that runs it.
    // Everything the attempt needs travels in here, and the worker touches the
    // owner ONLY under the guard mutex — so an abandoned attempt cannot reach a
    // destroyed QtTrustTunnelClient at all. The previous design checked
    // staleness between touches, which left a narrow window where the owner
    // could be destroyed after a check had already passed.
    struct ConnectAttempt {
        enum class Outcome { Connected, Retry, FatalStop, FatalKeepGoing };

        QtTrustTunnelClient *owner = nullptr;
        GuardPtr guard;
        quint64 attemptGen = 0;
        ag::TrustTunnelConfig config;
        Routing routing; // what `config` routes by, kept after it moves into the core
        std::string boundIf;
        ag::VpnCallbacks callbacks;
        // The previous session, retired by the worker: disconnecting a live core
        // client blocks, and must not run on the owner's event loop.
        std::unique_ptr<ag::TrustTunnelClient> retiredClient;
        std::unique_ptr<ag::AutoNetworkMonitor> retiredMonitor;
        // Built by the worker, adopted by the owner only if it still wants them.
        std::unique_ptr<ag::TrustTunnelClient> client;
        std::unique_ptr<ag::AutoNetworkMonitor> monitor;
        std::chrono::steady_clock::time_point startedAt{};
        Outcome outcome = Outcome::Retry;
        QString error;
        bool privilegeHint = false;
    };
    using AttemptPtr = std::shared_ptr<ConnectAttempt>;

    AttemptPtr prepareAttempt(quint64 attemptGen);      // owner thread
    static void runAttempt(const AttemptPtr &ctx);      // worker thread, no `this`
    static bool applySystemDns(const AttemptPtr &ctx);  // worker thread
    static void establishTunnel(const AttemptPtr &ctx); // worker thread
    void adoptAttempt(const AttemptPtr &ctx);           // owner thread
    // Hop a core event back onto this object's thread, dropping it if its
    // session has been superseded. Called with the guard mutex held.
    void postCoreStateChanged(quint64 session, int coreState, int errCode, const QString &errText);
    void postTunnelStats(quint64 session, quint64 up, quint64 down);
    void postConnectionInfo(quint64 session, const QString &line);

    ag::VpnCallbacks makeCallbacks(const GuardPtr &guard);
    // Split out of makeCallbacks: a self-contained callback with its own
    // captures, which is where the seam already was.
    std::function<void(const ag::VpnConnectRequestSnapshot &, ag::VpnConnectDecision *)>
    makeConnectRequestHandler(const GuardPtr &guard, quint64 session);
    // How that handler reaches back to log: under the guard, and only while
    // `self` is alive. Static, so it is never a call on a destroyed object.
    static void postConnectionInfoIfAlive(QtTrustTunnelClient *self, const GuardPtr &guard,
                                          quint64 session, const QString &line);
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
    std::string exclusionsLocked() const;
    void storeExclusionsLocked(std::vector<std::string> exclusions);
    void storeModeLocked(bool selective);
    // Hand the running session the current rules and mode, if they differ from
    // what it routes by. Owner thread, like everything else that touches m_client.
    void applyRoutingToSession();
    void reportFirstConnectFailure(int errCode, const QString &errText);
    void applyKillSwitchPortsToConfigLocked();
    void applyCoreLogPathToConfig();
    void applyCoreLogPathToConfigLocked();
    void resetCoreLogFile();
    void startCoreLogTail();
    void stopCoreLogTail();
    void teardownClient();
    void checkFdHealth();
    bool reloadStoredConfigIfNeeded();
    void failConnectFatal(const QString &qErr, bool privilegeHint);
    void forceFdReconnect(const QString &logReason, const QString &userReason);
    void protectOutboundSocket(ag::SocketProtectEvent *event);
    static int countOpenFds();
    static int getFdLimit();
    // qt_trusttunnel_uplink.cpp: on Windows, nothing in the core notices the
    // network adapter changing, so these (and the followUplink slot) tell it.
    void startFollowingUplink();
    void resetUplinkTracking();
    void reportUplink(uint32_t ifIndex);
    // What a failed set_system_dns() means for the attempt; may reword `error`.
    static ConnectAttempt::Outcome dnsFailureOutcome(const QString &coreError,
                                                     QString *error); // worker thread

    // The connect-request handler runs on the core wrapper's own thread and may
    // still be running while this object is being destroyed. It DOES capture
    // `this`, to write its decision to the log, and is safe only because it
    // reaches back the way every other callback here does: under the liveness
    // guard, checking alive. Do not remove that check.
    //
    // The rules live in this shared snapshot rather than behind m_configMutex
    // because the handler reads them on every connection, before and outside
    // that guard — a lookup can be slow, and holding the object's lock across
    // it would stall teardown.
    struct AppRuleSnapshot {
        std::mutex mutex;
        QStringList rules;
        bool selective = false;
        // Every connection asks the handler something, so logging all of them
        // buries the log in programs nobody wrote a rule for. Decisions that
        // actually routed something are always logged; the rest only when the
        // user has asked for verbose logs, which is exactly when "what program
        // is this connection?" is the question being investigated.
        bool verbose = false;
    };
    std::shared_ptr<AppRuleSnapshot> m_appRules = std::make_shared<AppRuleSnapshot>();

    std::unique_ptr<ag::TrustTunnelClient> m_client;
    std::unique_ptr<ag::AutoNetworkMonitor> m_networkMonitor;
    // What m_client routes by: what it was built with, then every live update.
    // Read only while m_client is set, and set whenever it is adopted.
    Routing m_sessionRouting;
    // The reasons already given this session for not having connected yet, as the
    // core goes round its recovery loop: each is said once, not once per round,
    // even when two of them take turns.
    QStringList m_firstConnectFailuresSaid;
    // Guards the config working set: m_config, m_lastConfigToml,
    // m_extraExcludedRoutes, m_originalExcludedRoutes, m_extraExclusions,
    // m_originalExclusions, m_selectiveMode, m_killSwitch,
    // m_killSwitchPortsFromConfig, m_configAllowPorts, m_loggingEnabled,
    // m_logLevel and m_coreLogPath. The IPC setters run on this object's
    // thread while the connect thread moves the config into the core client — the
    // unsynchronised move-out used to corrupt the heap in a root process.
    // Never held across a blocking core call, so it cannot deadlock the join.
    mutable std::mutex m_configMutex;
    std::optional<ag::TrustTunnelConfig> m_config;
    QString m_lastConfigToml; // in-memory config for reconnect without on-disk secrets
    std::vector<std::string> m_extraExcludedRoutes;
    std::vector<std::string> m_originalExcludedRoutes; // routes from config file before our additions
    std::vector<std::string> m_extraExclusions;
    std::string m_originalExclusions; // exclusions from config file before our additions
    bool m_selectiveMode = false;     // route only the exclusions (vs bypass them)
    bool m_killSwitch = false;
    bool m_killSwitchPortsFromConfig = false;
    // The config's killswitch_allow_ports, kept aside before they are cleared, so
    // that turning the setting on applies them without the config being sent again.
    std::string m_configAllowPorts;
    bool m_loggingEnabled = true;
    QTimer m_reconnectTimer;
    QTimer m_fdWatchdogTimer;
    int m_fdBaseline = -1; // open fd count right after connect (for leak detection)
    // The last few counts. What distinguishes a leak from load is not the peak
    // but whether the count ever comes back down: a leak never gives its
    // descriptors back, so even the LOWEST reading in a recent window keeps
    // climbing, while load pushes the peak up and lets it fall again.
    //
    // Comparing the current count to the connect-time baseline could not tell
    // those apart, and per-application split tunnelling turned that from a
    // theoretical flaw into a daily false alarm: a program routed around the
    // tunnel opens its connections directly from this process, so a browser on
    // the bypass list legitimately holds dozens of sockets here — and the user
    // was told the connection was using an unusual number of system resources
    // for working exactly as asked.
    QList<int> m_fdSamples;
    QTimer m_networkWaitTimer;   // fires if we stay in WaitingForNetwork too long
    // Looks at the machine's active network adapter while a session is up
    // (Windows; see qt_trusttunnel_uplink.cpp). Seen is the last look, reported
    // what the core was last told — 0 for "no network". Quiet looks are those
    // taken offline without asking the core (followUplink()).
    QTimer m_uplinkTimer;
    int m_uplinkPollMs = 2000;
    uint32_t m_uplinkSeen = 0;
    uint32_t m_uplinkReported = 0;
    int m_quietOfflineLooks = 0;
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
    // Written on the thread that starts a connect attempt, read from the core's
    // socket-protect callback, which runs on a core thread — so a plain uint32_t
    // was a data race. Aligned 32-bit loads happen not to tear on the platforms
    // this ships to, which is exactly why it would never show up as a bug; it is
    // still undefined behaviour and the fix costs nothing. The uplink follower
    // moves it when the network adapter changes.
    std::atomic<uint32_t> m_winPhysicalIfIndex{0};
#endif
};
