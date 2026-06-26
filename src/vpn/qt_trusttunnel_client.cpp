// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_platform.h"
#include "qt_trusttunnel_events.h"
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

QtTrustTunnelClient::QtTrustTunnelClient(QObject *parent)
    : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &QtTrustTunnelClient::startConnectAttempt);

    // Periodically check open fd count and force clean reconnect if leaking.
    m_fdWatchdogTimer.setInterval(10000); // every 10 seconds
    connect(&m_fdWatchdogTimer, &QTimer::timeout, this, &QtTrustTunnelClient::checkFdHealth);

    // If we stay stuck in WaitingForNetwork for more than 30 s (common after
    // sleep/wake or a brief network blip where the core doesn't self-recover),
    // force a clean teardown and reconnect from the Qt side.
    m_networkWaitTimer.setSingleShot(true);
    m_networkWaitTimer.setInterval(30000);
    connect(&m_networkWaitTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::WaitingForNetwork && !m_stopRequested && m_autoReconnect) {
            teardownClient();
            scheduleReconnect(QStringLiteral("network wait timeout: forcing clean reconnect"));
        }
    });
}

QtTrustTunnelClient::~QtTrustTunnelClient() {
    // Suppress all signal emission during destruction — connected slots may
    // reference this object which is already being torn down.
    m_stopRequested = true;
    m_reconnectTimer.stop();
    if (m_connectThread.isRunning()) {
        m_connectThread.quit();
        m_connectThread.wait();
    }
    blockSignals(true);
    teardownClient();
}

void QtTrustTunnelClient::teardownClient() {
    // If teardownClient is called from the main thread (e.g. handleCoreStateChanged,
    // checkFdHealth, disconnectVpn) we MUST wait for m_connectThread to finish before
    // touching m_client.  The thread may currently be inside m_client->connect() or
    // m_client->set_system_dns(), so resetting m_client while it runs is a data race.
    //
    // When teardownClient is called FROM the connect thread itself (doConnectAttempt
    // on a reconnect) we must NOT join it — that would deadlock.
    if (QThread::currentThread() != &m_connectThread && m_connectThread.isRunning()) {
        m_connectThread.quit();
        // BUG FIX: wait() without a timeout — a finite wait(5000) returns false
        // after 5 s even when the thread is still alive inside a blocking native
        // call (m_client->connect, set_system_dns).  Calling m_client.reset()
        // while the thread holds a reference is a use-after-free / data race.
        // As a last resort after 15 s we terminate to prevent a complete hang.
        if (!m_connectThread.wait(15000)) {
            qWarning("[teardownClient] connect thread did not exit in 15 s — forcing terminate");
            m_connectThread.terminate();
            m_connectThread.wait();
        }
    }

    if (m_networkMonitor) {
        m_networkMonitor->stop();
        m_networkMonitor.reset();
    }
    if (m_client) {
        m_client->disconnect();
    }
    m_client.reset();
#ifdef Q_OS_WIN
    m_winPhysicalIfIndex = 0;
#endif
}

void QtTrustTunnelClient::setConfig(ag::TrustTunnelConfig config) {
    m_config = std::move(config);
    m_config->loglevel = m_logLevel;
    ag::Logger::set_log_level(m_logLevel);
    if (std::holds_alternative<ag::TrustTunnelConfig::TunListener>(m_config->listener)) {
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(m_config->listener);
        tun.included_routes.insert(tun.included_routes.end(), m_extraIncludedRoutes.begin(), m_extraIncludedRoutes.end());
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
    // Apply custom DNS overrides
    if (!m_customDns.empty()) {
        m_config->location.dns_upstreams = m_customDns;
    }
    // Append extra exclusions (domain bypass rules)
    // Save original exclusions before we touch them so they can be restored
    // if the user changes bypass rules later.
    m_originalExclusions = m_config->exclusions;
    for (const auto &ex : m_extraExclusions) {
        if (!m_config->exclusions.empty() && m_config->exclusions.back() != ' ') {
            m_config->exclusions.push_back(' ');
        }
        m_config->exclusions.append(ex);
    }
    // Routing policy: general = bypass the exclusions, selective = route only them.
    m_config->mode = m_selectiveMode ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
    m_config->killswitch_enabled = m_killSwitch;
}

void QtTrustTunnelClient::setVpnMode(bool selective) {
    m_selectiveMode = selective;
    if (m_config.has_value())
        m_config->mode = selective ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
}

void QtTrustTunnelClient::setKillSwitch(bool enabled) {
    m_killSwitch = enabled;
    if (m_config.has_value())
        m_config->killswitch_enabled = enabled;
}

void QtTrustTunnelClient::setSessionLogging(const QString &path, bool enabled)
{
    m_loggingEnabled = enabled;
    m_coreLogPath = enabled ? path.trimmed() : QString();
    if (m_coreLogPath.isEmpty() && enabled)
        m_coreLogPath = qt_trusttunnel_default_core_log_path();
    applyCoreLogPathToConfig();
    if (!enabled)
        stopCoreLogTail();
}

bool QtTrustTunnelClient::loadConfigFromFile(const QString &path) {
    const std::string configPath = path.toStdString();
    toml::parse_result parsed = toml::parse_file(configPath);
    if (!parsed) {
        const std::string_view descrView = parsed.error().description();
        const std::string descr{descrView};
        setState(State::Error);
        emit vpnError(QString("Failed parsing config: %1").arg(QString::fromStdString(descr)));
        return false;
    }

    auto config = ag::TrustTunnelConfig::build_config(parsed.table());
    if (!config.has_value()) {
        setState(State::Error);
        emit vpnError(QStringLiteral("Invalid TrustTunnel config structure"));
        return false;
    }

    m_lastConfigPath = path;
    m_lastConfigToml.clear();
    setConfig(std::move(*config));
    setState(State::Disconnected);
    return true;
}

bool QtTrustTunnelClient::loadConfigFromToml(const QString &tomlContent) {
    if (tomlContent.isEmpty()) {
        setState(State::Error);
        emit vpnError(QStringLiteral("Empty config"));
        return false;
    }
    toml::parse_result parsed = toml::parse(tomlContent.toStdString());
    if (!parsed) {
        const std::string_view descrView = parsed.error().description();
        setState(State::Error);
        emit vpnError(QString("Failed parsing config: %1")
                              .arg(QString::fromStdString(std::string{descrView})));
        return false;
    }

    auto config = ag::TrustTunnelConfig::build_config(parsed.table());
    if (!config.has_value()) {
        setState(State::Error);
        emit vpnError(QStringLiteral("Invalid TrustTunnel config structure"));
        return false;
    }

    m_lastConfigPath.clear();
    m_lastConfigToml = tomlContent;
    setConfig(std::move(*config));
    setState(State::Disconnected);
    return true;
}

void QtTrustTunnelClient::setAutoReconnectEnabled(bool enabled) {
    m_autoReconnect = enabled;
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
    setRoutingRules({}, excluded);
}

void QtTrustTunnelClient::disconnectVpn() {
    m_stopRequested = true;
    m_reconnectTimer.stop();
    m_fdWatchdogTimer.stop();
    m_networkWaitTimer.stop();
    stopCoreLogTail();

    // Stop connect thread if running.  No timeout: teardownClient() below calls
    // m_client.reset() and a running thread would still hold a pointer to it.
    if (m_connectThread.isRunning()) {
        m_connectThread.quit();
        if (!m_connectThread.wait(15000)) {
            qWarning("[disconnectVpn] connect thread did not exit in 15 s — forcing terminate");
            m_connectThread.terminate();
            m_connectThread.wait();
        }
    }

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
    m_logLevel = qt_trusttunnel_parse_log_level(level);
    ag::Logger::set_log_level(m_logLevel);
    if (m_config.has_value()) {
        m_config->loglevel = m_logLevel;
    }
}

void QtTrustTunnelClient::setRoutingRules(const std::vector<std::string> &includeRoutes,
        const std::vector<std::string> &excludeRoutes) {
    m_extraIncludedRoutes = includeRoutes;
    m_extraExcludedRoutes = excludeRoutes;
    if (m_config.has_value() && std::holds_alternative<ag::TrustTunnelConfig::TunListener>(m_config->listener)) {
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(m_config->listener);
        tun.included_routes.insert(tun.included_routes.end(), m_extraIncludedRoutes.begin(), m_extraIncludedRoutes.end());
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
}

void QtTrustTunnelClient::setCustomDns(const std::vector<std::string> &dnsServers) {
    m_customDns = dnsServers;
    if (m_config.has_value() && !m_customDns.empty()) {
        m_config->location.dns_upstreams = m_customDns;
    }
}

void QtTrustTunnelClient::setExtraExclusions(const std::vector<std::string> &exclusions) {
    m_extraExclusions = exclusions;
    if (m_config.has_value()) {
        // Restore original config exclusions first, then append new ones.
        // This prevents duplicate/stale entries from accumulating.
        m_config->exclusions = m_originalExclusions;
        for (const auto &ex : m_extraExclusions) {
            if (!m_config->exclusions.empty() && m_config->exclusions.back() != ' ') {
                m_config->exclusions.push_back(' ');
            }
            m_config->exclusions.append(ex);
        }
    }
}

ag::VpnCallbacks QtTrustTunnelClient::makeCallbacks() {
    ag::VpnCallbacks callbacks;
    callbacks.protect_handler = [this](ag::SocketProtectEvent *event) {
        protectOutboundSocket(event);
    };
    callbacks.verify_handler = qt_trusttunnel_verify_server_certificate;
    callbacks.state_changed_handler = [this](ag::VpnStateChangedEvent *event) {
        const StateChangedPayload payload = extractStateChangedPayload(event);
        QMetaObject::invokeMethod(this,
                                  [this, payload]() {
                                      handleCoreStateChanged(payload.state, payload.errCode,
                                                             payload.errText);
                                  },
                                  Qt::QueuedConnection);
    };
    callbacks.client_output_handler = [this](ag::VpnClientOutputEvent *event) {
        const size_t bytes = clientOutputBytes(event);
        QMetaObject::invokeMethod(this, [this, bytes]() { emit clientOutput(QString::number(bytes)); },
                Qt::QueuedConnection);
    };
    callbacks.tunnel_stats_handler = [this](ag::VpnTunnelConnectionStatsEvent *event) {
        if (!event)
            return;
        const quint64 up = event->upload;
        const quint64 down = event->download;
        QMetaObject::invokeMethod(this, [this, up, down]() { emit tunnelStats(up, down); },
                Qt::QueuedConnection);
    };
    callbacks.connection_info_handler = [this](ag::VpnConnectionInfoEvent *event) {
        const QString line = qt_trusttunnel_connection_info_line(event);
        QMetaObject::invokeMethod(this, [this, line]() { emit connectionInfo(line); }, Qt::QueuedConnection);
    };
    return callbacks;
}

void QtTrustTunnelClient::protectOutboundSocket(ag::SocketProtectEvent *event)
{
#ifdef Q_OS_WIN
    pinWindowsPhysicalOutbound(m_winPhysicalIfIndex);
#endif
    qt_trusttunnel_protect_outbound_socket(event);
}

void QtTrustTunnelClient::scheduleReconnect(const QString &reason) {
    const QString message = reason.isEmpty() ? QStringLiteral("connect() failed") : reason;
    if (m_stopRequested || !m_autoReconnect) {
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
    auto sinceLastAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastConnectAttempt).count();
    if (m_lastConnectAttempt != std::chrono::steady_clock::time_point{} && sinceLastAttempt < 10000) {
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
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(m_state);
}

void QtTrustTunnelClient::handleCoreConnected()
{
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();
    m_networkWaitTimer.stop();
    m_everConnected = true;
    m_fdBaseline = countOpenFds();
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
    scheduleReconnect(buildDisconnectReason(errCode, errText, m_everConnected, m_lastConnectAttempt));
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
#ifndef _WIN32
#include <unistd.h>
#include <sys/resource.h>
#include <dirent.h>
#endif

int QtTrustTunnelClient::countOpenFds() {
#if defined(__APPLE__) || defined(__linux__)
    int count = 0;
    DIR *dir = opendir("/dev/fd");
    if (!dir) {
        // Fallback for Linux: /proc/self/fd
        dir = opendir("/proc/self/fd");
    }
    if (dir) {
        while (readdir(dir) != nullptr) {
            ++count;
        }
        closedir(dir);
        count -= 2; // subtract "." and ".."
    }
    return count;
#else
    return -1; // not supported on Windows
#endif
}

int QtTrustTunnelClient::getFdLimit() {
#ifndef _WIN32
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        return static_cast<int>(rl.rlim_cur);
    }
#endif
    return -1;
}

void QtTrustTunnelClient::forceFdReconnect(const QString &logReason, const QString &userReason)
{
    qWarning("%s", qPrintable(logReason));
    emit vpnError(userReason);
    teardownClient();
    if (!m_stopRequested && m_autoReconnect)
        scheduleReconnect(QStringLiteral("fd watchdog: too many open files, clean reconnect"));
}

void QtTrustTunnelClient::checkFdHealth() {
    if (m_state != State::Connected && m_state != State::Reconnecting)
        return;
    const int openFds = countOpenFds();
    if (openFds < 0)
        return;

    constexpr int kFdGrowthThreshold = 64;
    if (m_fdBaseline >= 0 && openFds - m_fdBaseline >= kFdGrowthThreshold) {
        forceFdReconnect(
                QStringLiteral("[fd watchdog] Open fds grew by %1 since connect (%2 -> %3)")
                        .arg(openFds - m_fdBaseline)
                        .arg(m_fdBaseline)
                        .arg(openFds),
                QStringLiteral("fd watchdog: reconnecting after fd growth"));
        return;
    }

    const int fdLimit = getFdLimit();
    if (fdLimit < 0)
        return;
    const double usage = static_cast<double>(openFds) / static_cast<double>(fdLimit);
    if (usage > 0.85) {
        forceFdReconnect(
                QStringLiteral("[fd watchdog] Open fds: %1 / %2 (%3%)")
                        .arg(openFds)
                        .arg(fdLimit)
                        .arg(static_cast<int>(usage * 100.0)),
                QStringLiteral("fd watchdog: reconnecting near fd limit"));
    }
}

void QtTrustTunnelClient::applyCoreLogPathToConfig()
{
    if (!m_loggingEnabled) {
        if (m_config.has_value())
            m_config->log_file_path.clear();
        return;
    }
    if (m_coreLogPath.isEmpty())
        m_coreLogPath = qt_trusttunnel_default_core_log_path();
    if (m_config.has_value())
        m_config->log_file_path = m_coreLogPath.toStdString();
}

void QtTrustTunnelClient::startCoreLogTail()
{
    if (!m_loggingEnabled)
        return;
    applyCoreLogPathToConfig();
    if (m_coreLogPath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_coreLogPath).absolutePath());
    m_coreLogOffset = QFileInfo(m_coreLogPath).size();

    if (!m_coreLogPoll) {
        m_coreLogPoll = new QTimer(this);
        m_coreLogPoll->setInterval(800);
        connect(m_coreLogPoll, &QTimer::timeout, this, &QtTrustTunnelClient::pollCoreLogFile);
    }
    if (!m_coreLogPoll->isActive())
        m_coreLogPoll->start();
    pollCoreLogFile();
}

void QtTrustTunnelClient::stopCoreLogTail()
{
    if (m_coreLogPoll)
        m_coreLogPoll->stop();
    m_coreLogLineBuffer.clear();
}

void QtTrustTunnelClient::pollCoreLogFile()
{
    if (m_coreLogPath.isEmpty())
        return;
    QFile f(m_coreLogPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    if (!f.seek(m_coreLogOffset))
        return;
    const QByteArray chunk = f.readAll();
    m_coreLogOffset = f.pos();
    f.close();
    if (chunk.isEmpty())
        return;

    constexpr int kMaxLinesPerPoll = 24;
    drainCoreLogTailBytes(&m_coreLogLineBuffer, chunk, kMaxLinesPerPoll,
            [this](const QString &line) { emit coreLogLine(line); });
}
