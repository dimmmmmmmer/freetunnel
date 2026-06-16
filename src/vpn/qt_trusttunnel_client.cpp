#include "qt_trusttunnel_client.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <toml++/toml.h>

#ifdef _WIN32
// Windows SDK doesn't define POSIX iovec; define a minimal version before vpn.h uses it.
#ifndef IOVEC_DEFINED_QT
#define IOVEC_DEFINED_QT
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif
#endif

#include "vpn/vpn.h" // for ag::iovec on Windows
#include "net/network_manager.h" // for vpn_network_manager_get_outbound_interface

#ifdef _WIN32
#include "net/os_tunnel.h" // for vpn_win_socket_protect
#include <windows.h>

static bool is_process_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}
#endif

static ag::LogLevel parse_log_level(const QString &level) {
    const QString l = level.toLower();
    if (l == "error") return ag::LOG_LEVEL_ERROR;
    if (l == "warn" || l == "warning") return ag::LOG_LEVEL_WARN;
    if (l == "debug") return ag::LOG_LEVEL_DEBUG;
    if (l == "trace") return ag::LOG_LEVEL_TRACE;
    return ag::LOG_LEVEL_INFO;
}

QtTrustTunnelClient::QtTrustTunnelClient(QObject *parent)
    : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &QtTrustTunnelClient::doConnectAttemptInThread);

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

void QtTrustTunnelClient::connectVpn() {
    if (m_state == State::Connecting || m_state == State::Connected
            || m_state == State::Reconnecting || m_state == State::WaitingForNetwork) {
        return;
    }
#ifndef _WIN32
    if (::geteuid() != 0) {
        emit vpnError(QStringLiteral("Root permissions are required to initialize VPN (run app with sudo)."));
        return;
    }
#else
    if (!is_process_elevated()) {
        emit vpnError(QStringLiteral("Administrator privileges are required to initialize VPN. Restart the app as Administrator."));
        return;
    }
#endif
    m_stopRequested = false;
    m_reconnectTimer.stop();
    m_fdWatchdogTimer.start(); // start fd health monitoring
    setState(State::Connecting);
    doConnectAttemptInThread();
}

void QtTrustTunnelClient::doConnectAttemptInThread() {
    // If thread is already running, wait for it to finish
    if (m_connectThread.isRunning()) {
        m_connectThread.quit();
        m_connectThread.wait();
    }

    // BUG FIX: Disconnect any previous QThread::started connections that
    // accumulated from earlier calls to this function.  Without this, every
    // reconnect adds one more connection and on the N-th reconnect the lambda
    // fires N times simultaneously, causing parallel doConnectAttempt() calls
    // and a data race / crash on m_client.
    disconnect(&m_connectThread, &QThread::started, nullptr, nullptr);

    // BUG FIX: Use `worker` (not `this`) as the receiver context object so
    // that Qt routes the lambda to m_connectThread instead of the main thread.
    // With `this` as the context, Qt picks Qt::QueuedConnection across threads
    // and posts the lambda to the main thread's event loop — doConnectAttempt()
    // would then block the entire UI.
    auto *worker = new QObject();
    worker->moveToThread(&m_connectThread);
    connect(&m_connectThread, &QThread::started, worker, [this, worker]() {
        doConnectAttempt();
        worker->deleteLater();
        m_connectThread.quit();
    });

    m_connectThread.start();
}

void QtTrustTunnelClient::doConnectAttempt() {
    if (m_stopRequested) {
        return;
    }

    const bool isReconnect = (m_client != nullptr);
    // If called from scheduleReconnect, update state (connectVpn already set it).
    if (m_state != State::Connecting) {
        setState(isReconnect ? State::Reconnecting : State::Connecting);
    }

    try {
        // If the client already exists, properly tear down the old VPN session
        // before reconnecting. Without this, connect_impl() creates a new Vpn
        // instance via vpn_open() and overwrites the pointer, leaking the
        // previous session and all its resources.
        if (isReconnect) {
            emit connectProgress(tr("Disconnecting previous session..."));
            teardownClient();  // Full cleanup including networkMonitor
        }

        if (!m_client) {
            // TrustTunnelConfig is move-only (contains unique_ptr), so we
            // cannot copy it.  If m_config was already consumed by a previous
            // client session, reload it from the saved file path.
            if (!m_config.has_value()) {
                if (!m_lastConfigPath.isEmpty()) {
                    if (!loadConfigFromFile(m_lastConfigPath)) {
                        // loadConfigFromFile already emits vpnError / sets Error state.
                        return;
                    }
                } else {
                    setState(State::Error);
                    emit vpnError(QStringLiteral("TrustTunnel config is not set"));
                    return;
                }
            }
            emit connectProgress(tr("Initializing VPN core..."));

            m_client = std::make_unique<ag::TrustTunnelClient>(std::move(*m_config), makeCallbacks());
            m_config.reset();

            emit connectProgress(tr("Starting network monitor..."));

            m_networkMonitor = std::make_unique<ag::AutoNetworkMonitor>(m_client.get(), "");
            if (!m_networkMonitor->start()) {
                m_networkMonitor.reset();
                teardownClient();
                setState(State::Error);
                emit vpnError(QStringLiteral("Failed to start network monitor"));
                return;
            }
        }

        emit connectProgress(tr("Configuring DNS..."));

        if (auto dnsErr = m_client->set_system_dns()) {
            const std::string errText = dnsErr->str();
            const QString qErr = QString::fromStdString(errText);
            // DNS setup failure is usually fatal until privileges change; stop auto-retry.
            teardownClient();
            m_stopRequested = true;
            setState(State::Error);
            emit vpnError(QString("set_system_dns() failed: %1").arg(qErr));
            return;
        }

        m_lastConnectAttempt = std::chrono::steady_clock::now();

        emit connectProgress(tr("Establishing tunnel..."));

        if (auto err = m_client->connect(ag::TrustTunnelClient::AutoSetup{})) {
            const std::string errText = err->str();
            QString qErr = QString::fromStdString(errText);
            if (qErr.contains("Failed to create listener", Qt::CaseInsensitive)) {
                qErr += QStringLiteral(" (likely needs sudo/admin privileges)");
                // Fatal until privileges/config change: stop reconnect loop.
                // The core may have started background threads — tear everything down.
                teardownClient();
                m_stopRequested = true;
                setState(State::Error);
                emit vpnError(QString("connect() failed: %1").arg(qErr));
                return;
            }
            scheduleReconnect(QString("connect() failed: %1").arg(qErr));
            return;
        }
    } catch (const std::exception &e) {
        teardownClient();
        scheduleReconnect(QString::fromUtf8(e.what()));
    }
}

void QtTrustTunnelClient::disconnectVpn() {
    m_stopRequested = true;
    m_reconnectTimer.stop();
    m_fdWatchdogTimer.stop();
    m_networkWaitTimer.stop();

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
    m_logLevel = parse_log_level(level);
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
    callbacks.protect_handler = [](ag::SocketProtectEvent *event) {
        if (!event) return;
        event->result = 0;
#ifdef __APPLE__
        // Bind the socket to the physical outbound interface so it bypasses
        // the TUN routing table.  Without this, bypass connections loop back
        // through the TUN and never reach the destination.
        uint32_t idx = ag::vpn_network_manager_get_outbound_interface();
        if (idx == 0) return;
        if (event->peer->sa_family == AF_INET) {
            if (setsockopt(event->fd, IPPROTO_IP, IP_BOUND_IF, &idx, sizeof(idx)) != 0) {
                event->result = -1;
            }
        } else if (event->peer->sa_family == AF_INET6) {
            if (setsockopt(event->fd, IPPROTO_IPV6, IPV6_BOUND_IF, &idx, sizeof(idx)) != 0) {
                event->result = -1;
            }
        }
#endif
#ifdef __linux__
        // On Linux, SO_BINDTODEVICE requires root and a known interface name.
        // For now we rely on routing rules; if bound_if is needed, it can be
        // configured via the TunListener config.
        (void) event;
#endif
#ifdef _WIN32
        if (!ag::vpn_win_socket_protect(event->fd, event->peer)) {
            event->result = -1;
        }
#endif
    };
    callbacks.verify_handler = [](ag::VpnVerifyCertificateEvent *event) {
        if (event) {
            event->result = 0;
        }
    };
    callbacks.state_changed_handler = [this](ag::VpnStateChangedEvent *event) {
        ag::VpnSessionState state = event ? event->state : ag::VPN_SS_DISCONNECTED;
        QMetaObject::invokeMethod(this, [this, state]() { handleCoreStateChanged(state); }, Qt::QueuedConnection);
    };
    callbacks.client_output_handler = [this](ag::VpnClientOutputEvent *event) {
        size_t bytes = 0;
        if (event) {
            for (size_t i = 0; i < event->packet.chunks_num; ++i) {
                bytes += event->packet.chunks[i].iov_len;
            }
        }
        QMetaObject::invokeMethod(this, [this, bytes]() { emit clientOutput(QString::number(bytes)); },
                Qt::QueuedConnection);
    };
    // tunnel_stats_handler is added to ag::VpnCallbacks by our CI wrapper patch
    // (scripts/patch_core_wrapper.py), which routes VPN_EVENT_TUNNEL_CONNECTION_STATS
    // to it so the TrafficGraph gets live per-connection upload/download deltas.
    callbacks.tunnel_stats_handler = [this](ag::VpnTunnelConnectionStatsEvent *event) {
        if (event) {
            quint64 up = event->upload;
            quint64 down = event->download;
            QMetaObject::invokeMethod(this, [this, up, down]() { emit tunnelStats(up, down); },
                    Qt::QueuedConnection);
        }
    };
    callbacks.connection_info_handler = [this](ag::VpnConnectionInfoEvent *event) {
        QString line = QStringLiteral("connection info");
        if (event) {
            QString action;
            switch (event->action) {
            case ag::VPN_FCA_BYPASS: action = QStringLiteral("bypass"); break;
            case ag::VPN_FCA_TUNNEL: action = QStringLiteral("tunnel"); break;
            case ag::VPN_FCA_REJECT: action = QStringLiteral("reject"); break;
            default: action = QStringLiteral("unknown"); break;
            }
            const QString domain = event->domain ? QString::fromUtf8(event->domain) : QStringLiteral("-");
            line = QStringLiteral("%1 %2").arg(action, domain);
        }
        QMetaObject::invokeMethod(this, [this, line]() { emit connectionInfo(line); }, Qt::QueuedConnection);
    };
    return callbacks;
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

void QtTrustTunnelClient::handleCoreStateChanged(ag::VpnSessionState state) {
    if (m_stopRequested) {
        return;
    }

    switch (state) {
    case ag::VPN_SS_CONNECTED:
        m_reconnectDelayMs = 1000;
        m_reconnectTimer.stop();
        m_networkWaitTimer.stop(); // no longer waiting for network
        m_everConnected = true;
        setState(State::Connected);
        emit vpnConnected();
        break;
    case ag::VPN_SS_CONNECTING:
        m_networkWaitTimer.stop();
        setState(m_everConnected ? State::Reconnecting : State::Connecting);
        break;
    case ag::VPN_SS_RECOVERING:
        // The core's internal recovery leaks TCP/IP sockets (the old tcpip
        // stack and its buffered connections are not fully closed before
        // new ones are created).  Force a clean teardown + reconnect from
        // the Qt layer to avoid exhausting file descriptors.
        m_networkWaitTimer.stop();
        if (!m_stopRequested && m_autoReconnect) {
            teardownClient();
            scheduleReconnect(QStringLiteral("recovery: full reconnect to avoid fd leak"));
        } else {
            setState(m_everConnected ? State::Reconnecting : State::Connecting);
        }
        break;
    case ag::VPN_SS_WAITING_RECOVERY:
        // Same as RECOVERING — don't let the core attempt its own recovery;
        // do a clean reconnect from our side.
        m_networkWaitTimer.stop();
        if (!m_stopRequested && m_autoReconnect) {
            teardownClient();
            scheduleReconnect(QStringLiteral("waiting recovery: full reconnect to avoid fd leak"));
        } else {
            setState(m_everConnected ? State::Reconnecting : State::Connecting);
        }
        break;
    case ag::VPN_SS_WAITING_FOR_NETWORK:
        // Network connectivity lost (internet disconnect, sleep mode, etc.).
        // Show a distinct state so the user knows the issue is local.
        // Start a watchdog: if the core does not self-recover within 30 s,
        // we force a full teardown + reconnect.  This handles the common
        // sleep/wake scenario where the core gets stuck after waking up.
        setState(State::WaitingForNetwork);
        if (!m_stopRequested && m_autoReconnect) {
            m_networkWaitTimer.start();
        }
        break;
    case ag::VPN_SS_DISCONNECTED:
        // With VPN_CRP_FALL_INTO_RECOVERY the core only reaches DISCONNECTED
        // on fatal errors (auth failure, cert error, location unavailable).
        // Schedule a Qt-level reconnect anyway, which will create a fresh
        // client and reload the config.
        m_networkWaitTimer.stop();
        if (!m_stopRequested) {
            scheduleReconnect(QStringLiteral("core disconnected"));
        }
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

void QtTrustTunnelClient::checkFdHealth() {
    if (m_state != State::Connected && m_state != State::Reconnecting) {
        return;
    }
    const int openFds = countOpenFds();
    const int fdLimit = getFdLimit();
    if (openFds < 0 || fdLimit < 0) {
        return; // platform doesn't support fd counting
    }

    // If we're using more than 70% of the fd limit, force a clean reconnect
    // to release leaked sockets from the VPN core.
    const double usage = static_cast<double>(openFds) / static_cast<double>(fdLimit);
    if (usage > 0.70) {
        qWarning("[fd watchdog] Open fds: %d / %d (%.0f%%) — forcing clean reconnect",
                openFds, fdLimit, usage * 100.0);
        emit vpnError(QString("fd watchdog: %1/%2 fds used, reconnecting...")
                .arg(openFds).arg(fdLimit));
        teardownClient();
        if (!m_stopRequested && m_autoReconnect) {
            scheduleReconnect(QStringLiteral("fd watchdog: too many open files, clean reconnect"));
        }
    }
}
