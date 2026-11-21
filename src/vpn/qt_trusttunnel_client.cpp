// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_platform.h"
#include "qt_trusttunnel_events.h"
#include "trusttunnel_tls_profiles.h"
#include "core/AppRules.h"
#include "core/ProcessLookup.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>
#include <toml++/toml.h>

#ifdef _WIN32
#ifndef IOVEC_DEFINED_QT
#define IOVEC_DEFINED_QT
struct iovec {
    void *iov_base; // cppcheck-suppress unusedStructMember
    size_t iov_len; // cppcheck-suppress unusedStructMember
};
#endif
#endif

#include "vpn/vpn.h"
#include "net/tls.h"

namespace {

// Test-only overrides for the watchdog/join intervals — the real values (10 s /
// 30 s / 15 s) would make the state-machine tests take minutes. Compiled out of
// release builds.
int testHookMs(const char *name, int defaultMs)
{
#ifdef FT_ENABLE_TEST_HOOKS
    bool ok = false;
    const int v = qEnvironmentVariableIntValue(name, &ok);
    if (ok && v > 0)
        return v;
#else
    Q_UNUSED(name);
#endif
    return defaultMs;
}

} // namespace

QtTrustTunnelClient::QtTrustTunnelClient(QObject *parent)
    : QObject(parent) {
    // Parent the member timers so moveToThread() carries them along with this
    // object. The helper server moves the client to a dedicated VPN thread;
    // an unparented member timer stays on the construction thread and Qt then
    // silently refuses every start() from the VPN thread ("Timers cannot be
    // started from another thread") — auto-reconnect, the fd watchdog and the
    // network-wait recovery never fire.
    m_reconnectTimer.setParent(this);
    m_fdWatchdogTimer.setParent(this);
    m_networkWaitTimer.setParent(this);
    m_uplinkTimer.setParent(this);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &QtTrustTunnelClient::startConnectAttempt);

    m_stuckJoinWaitMs = testHookMs("FT_TEST_STUCK_JOIN_MS", 15000);

    // Periodically check open fd count and force clean reconnect if leaking.
    m_fdWatchdogTimer.setInterval(testHookMs("FT_TEST_FD_WATCHDOG_MS", 10000)); // every 10 s
    connect(&m_fdWatchdogTimer, &QTimer::timeout, this, &QtTrustTunnelClient::checkFdHealth);

    // Windows: follow the network adapter the session runs over
    // (qt_trusttunnel_uplink.cpp). Every 2 s: a docked laptop or an unplugged
    // cable is noticed within a few seconds, and the look itself costs a
    // couple of system calls.
    m_uplinkPollMs = testHookMs("FT_TEST_UPLINK_POLL_MS", 2000);
    connect(&m_uplinkTimer, &QTimer::timeout, this, &QtTrustTunnelClient::followUplink);

    // If we stay stuck in WaitingForNetwork for more than 30 s (common after
    // sleep/wake or a brief network blip where the core doesn't self-recover),
    // force a clean teardown and reconnect from the Qt side.
    m_networkWaitTimer.setSingleShot(true);
    m_networkWaitTimer.setInterval(testHookMs("FT_TEST_NETWORK_WAIT_MS", 30000));
    connect(&m_networkWaitTimer, &QTimer::timeout, this, [this]() {
        if (m_state != State::WaitingForNetwork || m_stopRequested || !m_autoReconnect)
            return;
        // Same trade as the fd watchdog: destroying the core client destroys the
        // kill switch with it, and the backoff that follows leaves the machine
        // unprotected for up to 30 s per round — on the network the user just
        // woke up on, which is exactly when they were relying on it. Waiting
        // longer for a core that may be wedged is the safe direction; the state
        // stays WaitingForNetwork, so the UI keeps saying the tunnel is not up.
        if (m_killSwitch) {
            qWarning("[network watchdog] still waiting for network, but the kill switch is on — "
                     "not tearing the client down, since that would drop the traffic block");
            m_networkWaitTimer.start(); // keep checking; do not give up silently
            return;
        }
        teardownClient();
        scheduleReconnect(QStringLiteral("network wait timeout: forcing clean reconnect"));
    });
}

QtTrustTunnelClient::~QtTrustTunnelClient() {
    // Only the thread this object lives in may destroy it, with the timers it
    // owns: the helper hands it back to its own thread first (~HelperServer).
    Q_ASSERT(thread() == QThread::currentThread());
    // Suppress all signal emission during destruction — connected slots may
    // reference this object which is already being torn down.
    m_stopRequested = true;
    // Do this FIRST and outside every other teardown step: an abandoned connect
    // thread resumes inside a native call and then re-checks the guard, and the
    // core callbacks of an abandoned client marshal back through it. Once alive
    // is false neither touches this object again. Released immediately — the
    // join below must never run with the guard mutex held.
    {
        std::lock_guard<std::mutex> lk(m_guard->mutex);
        m_guard->alive = false;
    }
    m_reconnectTimer.stop();
    if (joinOrAbandonConnectThread(m_stuckJoinWaitMs)) {
        delete m_connectThread;
        m_connectThread = nullptr;
    }
    // else: abandoned — the thread (and the core objects it may still touch)
    // intentionally leak; we're going down anyway and a terminate() could
    // deadlock the process on locks the native call holds.
    blockSignals(true);
    teardownClient();
}

void QtTrustTunnelClient::teardownClient() {
    ++m_endpointRequest;
    m_pendingEndpoint.reset();
    m_preserveEndpointSession = false;
    // If teardownClient is called from the object's thread (handleCoreStateChanged,
    // checkFdHealth, disconnectVpn) the connect thread may currently be inside
    // m_client->connect() or set_system_dns(), so resetting m_client while it
    // runs would be a use-after-free. Join it — or, if it is stuck in the
    // native call, abandon it (joinOrAbandonConnectThread hands the core
    // objects over to the stale thread's cleanup, leaving m_client null here).
    //
    // When teardownClient is called FROM the connect thread itself
    // (doConnectAttempt on a reconnect) we must NOT join — that would deadlock.
    if (QThread::currentThread() != m_connectThread)
        joinOrAbandonConnectThread(m_stuckJoinWaitMs);

    // Invalidate the session: any core events still queued (or emitted during
    // the disconnect below) belong to the client being destroyed and must not
    // be attributed to a session started afterwards. The attempt generation goes
    // with it — a worker that finished just before this teardown must not get to
    // install its client afterwards and leave a live tunnel behind a
    // "Disconnected" state.
    ++m_sessionGen;
    ++m_guard->attemptGen;

    if (m_networkMonitor) {
        m_networkMonitor->stop();
        m_networkMonitor.reset();
    }
    if (m_client) {
        m_client->disconnect();
    }
    m_client.reset();
    emit sessionPresenceChanged(false);
#ifdef Q_OS_WIN
    m_winPhysicalIfIndex.store(0);
#endif
}

// The core honours more of a config than FreeTunnel ever writes, and this object
// runs in the ELEVATED helper, on whatever TOML the unprivileged GUI hands it.
// That TOML still carries every key an imported file had — ConfigToml keeps what
// it does not understand, so that a round trip loses nothing — and a process that
// has read the IPC token can send anything at all. Most of those keys only shape
// the tunnel. These make root act on a name the file chose, or open the kill
// switch:
//
//  - ssl_session_cache_path: a directory in which the core deletes every file
//    shaped like a cached session when it starts, and writes new ones (following
//    symlinks) when it stops. Unset, it keeps sessions in memory.
//  - killswitch_allow_ports: Windows ports the kill switch lets through, in both
//    directions, around the tunnel. The one exception to "cleared": the user can
//    let the config decide this (setKillSwitchPortsFromConfig), and setConfigLocked
//    then puts the file's ports back. Off by default, so a config cannot open the
//    kill switch on its own.
//  - device_name, use_existing, netns: which interface root creates or attaches
//    to, under what name, and in which network namespace (the last two on Linux).
//
// Only these: the rest of the file still reaches the core, and a key a newer core
// starts reading is caught by scripts/verify_upstream_patch.sh, not here. Cleared
// rather than checked: FreeTunnel writes none of them, and an interface the
// system names works just as well as one the file named.
void QtTrustTunnelClient::clearKeysRootMustNotTakeFromAConfig(ag::TrustTunnelConfig &config)
{
    config.ssl_session_storage_path.reset();
    config.killswitch_allow_ports.clear();
    if (auto *tun = std::get_if<ag::TrustTunnelConfig::TunListener>(&config.listener)) {
        tun->device_name.clear();
        tun->use_existing = false;
        tun->netns.reset();
    }
}

void QtTrustTunnelClient::setConfigLocked(ag::TrustTunnelConfig config) {
    m_config = std::move(config);
    m_configAllowPorts = m_config->killswitch_allow_ports;
    clearKeysRootMustNotTakeFromAConfig(*m_config);
    m_config->endpoint_sockets_protected = true;
    // m_logLevel is set from the config TOML's loglevel in the load functions
    // (driven by the GUI's Verbose-logs toggle: warn by default, info when on).
    m_config->loglevel = m_logLevel;
    ag::Logger::set_log_level(m_logLevel);
    if (std::holds_alternative<ag::TrustTunnelConfig::TunListener>(m_config->listener)) {
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(m_config->listener);
        // Kept for the same reason as m_originalExclusions below: a later route
        // list has to replace ours, not pile on top of it.
        m_originalExcludedRoutes = tun.excluded_routes;
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
    // Append extra exclusions (domain bypass rules)
    // Save original exclusions before we touch them so they can be restored
    // if the user changes bypass rules later.
    m_originalExclusions = m_config->exclusions;
    m_originalMode = m_config->mode;
    m_config->exclusions = exclusionsLocked();
    // Routing policy: general = bypass the exclusions, selective = route only them.
    m_config->mode = m_selectiveMode ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
    m_config->killswitch_enabled = m_killSwitch;
    applyKillSwitchPortsToConfigLocked();
}

// The config's own exclusions, then ours. What a session is built with and what
// a running one is handed live, so both come from here.
std::string QtTrustTunnelClient::exclusionsLocked() const {
    // An imported general-mode exclusion means bypass, not "through VPN".
    // Match Android: UI rules replace incompatible imported-mode rules instead
    // of reversing their direction when the mode changes.
    const auto mode = m_selectiveMode ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
    std::string out = mode == m_originalMode ? m_originalExclusions : std::string();
    for (const auto &ex : m_extraExclusions) {
        if (!out.empty() && out.back() != ' ')
            out.push_back(' ');
        out.append(ex);
    }
    return out;
}

void QtTrustTunnelClient::storeExclusionsLocked(std::vector<std::string> exclusions) {
    m_extraExclusions = std::move(exclusions);
    // Recomposed from the config's own list every time, so a removed rule goes
    // and nothing accumulates.
    if (m_config.has_value())
        m_config->exclusions = exclusionsLocked();
}

void QtTrustTunnelClient::storeModeLocked(bool selective) {
    m_selectiveMode = selective;
    if (m_config.has_value()) {
        m_config->mode = selective ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
        m_config->exclusions = exclusionsLocked();
    }
}

void QtTrustTunnelClient::setVpnMode(bool selective) {
    // Publish a complete policy on the owning thread.
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, selective]() { setVpnMode(selective); },
                                  Qt::QueuedConnection);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        storeModeLocked(selective);
    }
    applyRoutingToSession();
}

void QtTrustTunnelClient::setAppRules(const QStringList &rules) {
    // Sanitized here rather than trusted from the IPC: this runs in the
    // elevated helper, and a rule that cannot match is better dropped than
    // carried into a routing decision.
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, rules]() { setAppRules(rules); }, Qt::QueuedConnection);
        return;
    }
    m_appRuleList = freetunnel::sanitizedAppRules(rules);
    applyRoutingToSession();
}

void QtTrustTunnelClient::setSplitRouting(const QStringList &domains, bool selective,
                                          const QStringList &appRules) {
    // The config, application snapshot and version are published by one owner;
    // asynchronous decisions are checked against that version on the core loop.
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
                this, [this, domains, selective, appRules]() {
                    setSplitRouting(domains, selective, appRules);
                },
                Qt::QueuedConnection);
        return;
    }
    std::vector<std::string> exclusions;
    exclusions.reserve(static_cast<size_t>(domains.size()));
    std::transform(domains.cbegin(), domains.cend(), std::back_inserter(exclusions),
                   [](const QString &d) { return d.toStdString(); });
    const QStringList clean = freetunnel::sanitizedAppRules(appRules);
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        storeExclusionsLocked(std::move(exclusions));
        storeModeLocked(selective);
    }
    m_appRuleList = clean;
    applyRoutingToSession();
}

void QtTrustTunnelClient::applyRoutingToSession() {
    // m_client is this thread's alone: it is adopted, retired and torn down here.
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { applyRoutingToSession(); },
                                  Qt::QueuedConnection);
        return;
    }
    Routing wanted;
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        wanted.mode = m_selectiveMode ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
        wanted.exclusions = exclusionsLocked();
    }
    wanted.appRules = m_appRuleList;
    wanted.generation = m_publishedRouting.generation;
    if (wanted != m_publishedRouting) {
        ++wanted.generation;
        // Publish mode, app rules and their version in one critical section.
        // The core validates that version when it consumes the asynchronous
        // decision, so neither an old nor a future snapshot can leak traffic.
        std::lock_guard<std::mutex> lk(m_appRules->mutex);
        m_appRules->rules = wanted.appRules;
        m_appRules->selective = wanted.mode == ag::VPN_MODE_SELECTIVE;
        m_appRules->generation = wanted.generation;
        m_publishedRouting = wanted;
    }
    if (!m_client || wanted == m_sessionRouting)
        return;
    // Always submit the complete snapshot: the native queue coalesces updates.
    // The core compares the applied mode/domains before deciding to reset flows;
    // an app-only edit cannot accidentally cancel a pending domain/mode edit.
    m_client->update_routing(wanted.mode, wanted.exclusions, wanted.generation);
    m_sessionRouting = std::move(wanted);
}

void QtTrustTunnelClient::setKillSwitch(bool enabled) {
    std::lock_guard<std::mutex> lk(m_configMutex);
    m_killSwitch = enabled;
    if (m_config.has_value())
        m_config->killswitch_enabled = enabled;
}

void QtTrustTunnelClient::setKillSwitchPortsFromConfig(bool enabled) {
    std::lock_guard<std::mutex> lk(m_configMutex);
    m_killSwitchPortsFromConfig = enabled;
    if (m_config.has_value())
        applyKillSwitchPortsToConfigLocked();
}

// The file's own ports while the user lets the config decide them, none otherwise.
void QtTrustTunnelClient::applyKillSwitchPortsToConfigLocked() {
    m_config->killswitch_allow_ports =
            m_killSwitchPortsFromConfig ? m_configAllowPorts : std::string();
}

void QtTrustTunnelClient::setSessionLogging(bool enabled)
{
    // The path is derived here and never supplied by the caller. This object
    // runs inside the ELEVATED helper, which creates the log file (and its
    // parent directory) wherever this says — so accepting a path over IPC was a
    // root file-write primitive that had to be fenced in with owner, symlink and
    // canonicality checks. Not offering the choice removes the whole
    // class instead of guarding it: the file below is a transport buffer the
    // helper tails and forwards, and the GUI keeps the durable log.
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        m_loggingEnabled = enabled;
        m_coreLogPath = enabled ? qt_trusttunnel_default_core_log_path() : QString();
        applyCoreLogPathToConfigLocked();
    }
    if (!enabled)
        stopCoreLogTail();
}

// The core's build_config may not surface the TOML `loglevel`, so read it back
// ourselves and use it as the client log level. The GUI sets it from the
// Verbose-logs toggle (warn by default, info when on); without this the core
// stayed pinned at the default level and the toggle did nothing.
static ag::LogLevel logLevelFromTomlTable(const toml::table &t, ag::LogLevel fallback)
{
    if (const auto lvl = t["loglevel"].value<std::string>())
        return qt_trusttunnel_parse_log_level(QString::fromStdString(*lvl));
    return fallback;
}

// The [endpoint] tls_profile the core would refuse the whole config for, as the
// message that says so, or empty when it takes it (or ignores it: it reads the key
// only as a string). Absent or empty means chrome. "default" is boringssl.
// The value is a file's, shown to the user: cut short and cleaned of line breaks.
static QString refusedTlsProfileMessage(const toml::table &t)
{
    const auto profile = t["endpoint"]["tls_profile"].value<std::string>();
    if (!profile || profile->empty() || freetunnel::isKnownTlsProfile(*profile))
        return QString();
    QString shown = QString::fromStdString(*profile).left(40);
    for (QChar &ch : shown) {
        if (!ch.isPrint())
            ch = QLatin1Char(' ');
    }
    QStringList known;
    for (const std::string_view name : freetunnel::kTrustTunnelTlsProfileChoices)
        known << QString::fromUtf8(name.data(), static_cast<int>(name.size()));
    known << QStringLiteral("default");
    return QtTrustTunnelClient::tr("The config's tls_profile \"%1\" is not one TrustTunnel knows. Use one of: %2.")
            .arg(shown.trimmed(), known.join(QStringLiteral(", ")));
}

std::optional<ag::TrustTunnelConfig> QtTrustTunnelClient::parseConfig(const QString &text, QString *error, ag::LogLevel fallbackLevel)
{
    if (text.isEmpty()) {
        *error = tr("Empty config");
        return std::nullopt;
    }
    auto parsed = toml::parse(text.toStdString());
    if (!parsed) {
        *error = tr("Failed parsing config: %1").arg(QString::fromStdString(std::string(parsed.error().description())));
        return std::nullopt;
    }
    if (const QString badProfile = refusedTlsProfileMessage(parsed.table()); !badProfile.isEmpty()) {
        *error = badProfile;
        return std::nullopt;
    }
    auto config = ag::TrustTunnelConfig::build_config(parsed.table());
    // An elevated SOCKS listener could bind an arbitrary config-supplied address
    // and has no TUN for routes, DNS or the kill switch. Accept TUN configs only.
    if (!config || !std::holds_alternative<ag::TrustTunnelConfig::TunListener>(config->listener)) {
        *error = tr("Invalid TrustTunnel config structure");
        return std::nullopt;
    }
    config->loglevel = logLevelFromTomlTable(parsed.table(), fallbackLevel);
    return config;
}

bool QtTrustTunnelClient::loadConfigFromToml(const QString &tomlContent) {
    QString error;
    auto config = parseConfig(tomlContent, &error, m_logLevel);
    if (!config) {
        setState(State::Error);
        emit vpnError(error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        m_lastConfigToml = tomlContent;
        m_logLevel = config->loglevel;
        setConfigLocked(std::move(*config));
    }
    setState(State::Disconnected);
    return true;
}

void QtTrustTunnelClient::setReconnectBoundsMs(int initialDelayMs, int maxDelayMs) {
    m_reconnectDelayMs = std::max(250, initialDelayMs);
    m_reconnectMaxMs = std::max(m_reconnectDelayMs, maxDelayMs);
}

void QtTrustTunnelClient::setExtraExclusionDomains(const QStringList &domains)
{
    std::vector<std::string> exclusions;
    exclusions.reserve(static_cast<size_t>(domains.size()));
    std::transform(domains.cbegin(), domains.cend(), std::back_inserter(exclusions),
                   [](const QString &d) { return d.toStdString(); });
    setExtraExclusions(exclusions);
}

void QtTrustTunnelClient::setExcludedRouteStrings(const QStringList &routes)
{
    std::vector<std::string> excluded;
    excluded.reserve(static_cast<size_t>(routes.size()));
    std::transform(routes.cbegin(), routes.cend(), std::back_inserter(excluded),
                     [](const QString &r) { return r.toStdString(); });
    setExcludedRoutes(excluded);
}

void QtTrustTunnelClient::disconnectVpn() {
    m_stopRequested = true;
    // beginConnect()'s delayed start compares this: a disconnect that lands
    // inside the teardown delay must cancel the pending connect instead of
    // letting the tunnel come up after the user explicitly stopped it.
    ++m_disconnectGen;
    m_reconnectTimer.stop();
    m_fdWatchdogTimer.stop();
    m_networkWaitTimer.stop();
    m_uplinkTimer.stop();
    stopCoreLogTail();

    // Stop the in-flight attempt. Bounded: an attempt stuck inside a blocking
    // native call is abandoned after m_stuckJoinWaitMs (see
    // joinOrAbandonConnectThread) so the user's disconnect always completes.
    joinOrAbandonConnectThread(m_stuckJoinWaitMs);

    setState(State::Disconnecting);
    teardownClient();

    m_everConnected = false;
    setState(State::Disconnected);
    emit vpnDisconnected();
    // NOTE: m_stopRequested is intentionally left TRUE so that any stale
    // core state-change callbacks still queued (via Qt::QueuedConnection)
    // are silently discarded by handleCoreStateChanged(). The flag is
    // reset in connectVpn() when the user initiates a new session.
}

bool QtTrustTunnelClient::isConnected() const {
    return m_state == State::Connected;
}

QtTrustTunnelClient::State QtTrustTunnelClient::state() const {
    return m_state;
}

void QtTrustTunnelClient::setLogLevel(const QString &level) {
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        m_logLevel = qt_trusttunnel_parse_log_level(level);
        ag::Logger::set_log_level(m_logLevel);
        if (m_config.has_value()) {
            m_config->loglevel = m_logLevel;
        }
    }
    // The per-connection handler reads this from its own snapshot rather than
    // the config, for the same reason it reads the rules there: it must not
    // depend on this object still existing.
    std::lock_guard<std::mutex> lk(m_appRules->mutex);
    // "info" and not "debug": that is what the Verbose logs switch actually
    // sends (BackendSettings.cpp sends info when on, warn when off). Checking
    // for debug here would have made this dead code.
    const QString l = level.toLower();
    m_appRules->verbose = l == QLatin1String("info") || l == QLatin1String("debug")
            || l == QLatin1String("trace");
}

void QtTrustTunnelClient::setExcludedRoutes(const std::vector<std::string> &excludeRoutes) {
    std::lock_guard<std::mutex> lk(m_configMutex);
    m_extraExcludedRoutes = excludeRoutes;
    if (m_config.has_value() && std::holds_alternative<ag::TrustTunnelConfig::TunListener>(m_config->listener)) {
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(m_config->listener);
        // Restore the config's own routes first, exactly as setExtraExclusions()
        // does for domains. Appending to what was already there kept every route
        // of the previous list, so a config loaded before an edit would build its
        // session with a route the user had just removed. Today the helper uses a
        // loaded config straight away, which is luck this setter should not need.
        tun.excluded_routes = m_originalExcludedRoutes;
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
}

void QtTrustTunnelClient::setExtraExclusions(const std::vector<std::string> &exclusions) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, exclusions]() { setExtraExclusions(exclusions); }, Qt::QueuedConnection);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        storeExclusionsLocked(exclusions);
    }
    applyRoutingToSession();
}

void QtTrustTunnelClient::scheduleReconnect(const QString &reason) {
    // Failed connect attempts call this from m_connectThread; the timers live
    // on this object's thread and QTimer::start() from any other thread is a
    // silent no-op — the retry would never fire. Marshal onto our thread.
    if (QThread::currentThread() != thread()) {
        const QString reasonCopy = reason;
        QMetaObject::invokeMethod(
                this, [this, reasonCopy]() { scheduleReconnect(reasonCopy); }, Qt::QueuedConnection);
        return;
    }

    const QString message = reason.isEmpty() ? QStringLiteral("connect() failed") : reason;
    if (m_stopRequested) {
        // The user asked to disconnect while an attempt was in flight — the
        // session is over, don't overwrite Disconnected with a spurious Error.
        return;
    }
    if (!m_autoReconnect) {
        setState(State::Error);
        emit vpnError(message);
        return;
    }

    // If the timer is already running, don't restart it — avoids resetting the
    // countdown and double-incrementing the delay.
    if (m_reconnectTimer.isActive()) {
        return;
    }

    // If the connection was very short-lived (<10s), the issue is likely
    // persistent — increase the backoff faster to avoid a rapid reconnect loop.
    auto now = std::chrono::steady_clock::now();
    const auto lastAttempt = m_lastConnectAttempt.load();
    auto sinceLastAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAttempt).count();
    if (lastAttempt != std::chrono::steady_clock::time_point{} && sinceLastAttempt < 10000) {
        m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, m_reconnectMaxMs);
    }

    // Add jitter (±20%) to avoid thundering-herd reconnects.
    int jitter = static_cast<int>(m_reconnectDelayMs * 0.2);
    int jitteredDelay = m_reconnectDelayMs
            + (jitter > 0 ? QRandomGenerator::global()->bounded(-jitter, jitter + 1) : 0);
    jitteredDelay = std::max(250, jitteredDelay);

    setState(State::Reconnecting);
    emit vpnError(message);
    m_reconnectTimer.start(jitteredDelay);
    m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, m_reconnectMaxMs);
}

void QtTrustTunnelClient::setState(State s) {
    if (m_state.exchange(s) == s) {
        return;
    }
    emit stateChanged(s);
}

void QtTrustTunnelClient::handleCoreConnected()
{
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();
    m_networkWaitTimer.stop();
    m_everConnected = true;
    m_fdBaseline = countOpenFds();
    m_fdSamples.clear();
    setState(State::Connected);
    emit vpnConnected();
}

void QtTrustTunnelClient::handleCoreConnecting()
{
    m_networkWaitTimer.stop();
    setState(m_everConnected ? State::Reconnecting : State::Connecting);
}

void QtTrustTunnelClient::handleCoreRecovery(const QString &reason)
{
    // Core handles in-process recovery; tearing down here races DNS/network restarts
    // and leaves the tunnel stuck in VPN_SS_WAITING_RECOVERY until vpn_stop.
    m_networkWaitTimer.stop();
    m_reconnectTimer.stop();
    setState(m_everConnected ? State::Reconnecting : State::Connecting);
    if (!reason.isEmpty())
        emit connectionInfo(reason);
}

// A session that has not connected yet waits for recovery only when the kill
// switch has the core keep trying (ConnectOptions::fall_into_recovery, which
// establishTunnel() sets with the kill switch). Without it the core ends the
// session and handleCoreDisconnected() says why; with it the session carries on,
// rightly still Connecting, and this is where the user learns that the server is
// failing rather than slow. Once per reason, not once per round of retries.
void QtTrustTunnelClient::reportFirstConnectFailure(int errCode, const QString &errText)
{
    if (m_everConnected)
        return;
    const QString reason = qt_trusttunnel_format_vpn_error(errCode, errText);
    if (reason.isEmpty() || m_firstConnectFailuresSaid.contains(reason))
        return;
    m_firstConnectFailuresSaid.append(reason);
    emit vpnError(QStringLiteral("Connection failed: %1").arg(reason));
}

void QtTrustTunnelClient::handleCoreWaitingForNetwork()
{
    setState(State::WaitingForNetwork);
    if (!m_stopRequested && m_autoReconnect)
        m_networkWaitTimer.start();
}

void QtTrustTunnelClient::handleCoreDisconnected(int errCode, const QString &errText)
{
    m_networkWaitTimer.stop();
    if (m_stopRequested)
        return;
    const QString reason = buildDisconnectReason(errCode, errText, m_everConnected, m_lastConnectAttempt.load());
    if (m_preserveEndpointSession && m_client) {
        // A fatal endpoint error ends the VPN handshake, not the direct side of
        // this session. Keep the TUN and kill switch until an explicit stop.
        setState(State::Error);
        emit vpnError(reason);
        return;
    }
    scheduleReconnect(reason);
}

void QtTrustTunnelClient::handleCoreStateChanged(ag::VpnSessionState coreState, int errCode,
                                                 const QString &errText) {
    if (m_stopRequested)
        return;

    switch (coreState) {
    case ag::VPN_SS_CONNECTED:
        handleCoreConnected();
        break;
    case ag::VPN_SS_CONNECTING:
        handleCoreConnecting();
        break;
    case ag::VPN_SS_RECOVERING:
        handleCoreRecovery(recoveryReason(QStringLiteral("recovery"), errCode, errText));
        break;
    case ag::VPN_SS_WAITING_RECOVERY:
        handleCoreRecovery(recoveryReason(QStringLiteral("waiting recovery"), errCode, errText));
        reportFirstConnectFailure(errCode, errText);
        break;
    case ag::VPN_SS_WAITING_FOR_NETWORK:
        handleCoreWaitingForNetwork();
        break;
    case ag::VPN_SS_DISCONNECTED:
        handleCoreDisconnected(errCode, errText);
        break;
    default:
        break;
    }
}
// countOpenFds / getFdLimit / checkFdHealth / forceFdReconnect live in
// qt_trusttunnel_fdwatch.cpp.

// applyCoreLogPathToConfig / resetCoreLogFile / startCoreLogTail /
// stopCoreLogTail / pollCoreLogFile live in qt_trusttunnel_corelog.cpp.

// startFollowingUplink / resetUplinkTracking / followUplink / reportUplink live
// in qt_trusttunnel_uplink.cpp.

// postCoreStateChanged / postTunnelStats / postConnectionInfo /
// makeConnectRequestHandler / makeCallbacks / protectOutboundSocket live in
// qt_trusttunnel_callbacks.cpp.
