// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_platform.h"

#include "core/NetBind.h"
#include "net/network_manager.h"

#include <QMetaObject>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <exception>

#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(Q_OS_WIN)
static uint32_t captureWindowsPhysicalOutbound()
{
    const freetunnel::PhysicalRoute route = freetunnel::physicalOutboundRoute();
    if (route.index <= 0)
        return 0;
    const auto idx = static_cast<uint32_t>(route.index);
    ag::vpn_network_manager_set_outbound_interface(idx);
    return idx;
}
#endif

static bool skipPrivilegeCheckForTests()
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Test-only: unit tests drive the state machine against a mock core and
    // don't run elevated. Compiled out of release builds.
    return qEnvironmentVariableIsSet("FT_TEST_SKIP_PRIVILEGE_CHECK");
#else
    return false;
#endif
}

void QtTrustTunnelClient::connectVpn()
{
    if (m_state == State::Connecting || m_state == State::Connected
            || m_state == State::Reconnecting || m_state == State::WaitingForNetwork)
        return;
#ifndef _WIN32
    if (::geteuid() != 0 && !skipPrivilegeCheckForTests()) {
        emit vpnError(QStringLiteral("Root permissions are required to initialize VPN (run app with sudo)."));
        return;
    }
#else
    if (!qt_trusttunnel_is_process_elevated() && !skipPrivilegeCheckForTests()) {
        emit vpnError(QStringLiteral(
                "Administrator privileges are required to initialize VPN. Restart the app as Administrator."));
        return;
    }
#endif
    m_stopRequested = false;
    m_reconnectTimer.stop();
    m_fdWatchdogTimer.start();
    setState(State::Connecting);
    startConnectAttempt();
}

void QtTrustTunnelClient::startConnectAttempt()
{
    // Always run the attempt on the dedicated worker thread. Running it inline
    // used to block this object's event loop for the whole (potentially very
    // long) native connect — in the helper process that meant queued
    // "disconnect" / "connect new config" commands from the GUI sat unprocessed
    // until the attempt finished, which looked like a hung Disconnecting state
    // or a config switch that never connects.
    doConnectAttemptInThread();
}

void QtTrustTunnelClient::beginConnect(const QString &configToml)
{
    const auto st = state();
    const bool needsTeardown = st != State::Disconnected && st != State::Error;
    auto startConnect = [this, configToml]() {
        if (!loadConfigFromToml(configToml)) {
            emit vpnError(QStringLiteral("Failed to load config"));
            return;
        }
        connectVpn();
    };
    if (needsTeardown) {
        disconnectVpn();
        QTimer::singleShot(150, this, startConnect);
    } else {
        startConnect();
    }
}

void QtTrustTunnelClient::doConnectAttemptInThread()
{
    if (m_connectThread.isRunning()) {
        m_connectThread.quit();
        // Bounded wait: a previous attempt stuck inside a blocking native call
        // must not freeze this thread's event loop forever (same policy as
        // teardownClient / disconnectVpn).
        if (!m_connectThread.wait(15000)) {
            qWarning("[doConnectAttemptInThread] previous connect attempt did not exit in 15 s — "
                     "forcing terminate");
            m_connectThread.terminate();
            m_connectThread.wait();
        }
    }
    disconnect(&m_connectThread, &QThread::started, nullptr, nullptr);

    auto *worker = new QObject();
    worker->moveToThread(&m_connectThread);
    connect(&m_connectThread, &QThread::started, worker, [this, worker]() {
        doConnectAttempt();
        worker->deleteLater();
        m_connectThread.quit();
    });

    m_connectThread.start();
}

bool QtTrustTunnelClient::reloadStoredConfigIfNeeded()
{
    if (m_config.has_value())
        return true;
    if (!m_lastConfigToml.isEmpty())
        return loadConfigFromToml(m_lastConfigToml);
    setState(State::Error);
    emit vpnError(QStringLiteral("TrustTunnel config is not set"));
    return false;
}

bool QtTrustTunnelClient::ensureClientReady()
{
    if (m_client)
        return true;
    if (!reloadStoredConfigIfNeeded())
        return false;
    applyCoreLogPathToConfig();
    emit connectProgress(tr("Initializing VPN core..."));
#if defined(Q_OS_WIN)
    m_winPhysicalIfIndex = captureWindowsPhysicalOutbound();
    const std::string boundIf =
            m_winPhysicalIfIndex != 0 ? std::to_string(m_winPhysicalIfIndex) : std::string{};
#else
    const std::string boundIf;
#endif
    m_client = std::make_unique<ag::TrustTunnelClient>(std::move(*m_config), makeCallbacks());
    m_config.reset();
    startCoreLogTail();
    emit connectProgress(tr("Starting network monitor..."));
    m_networkMonitor = std::make_unique<ag::AutoNetworkMonitor>(m_client.get(), boundIf);
    if (m_networkMonitor->start())
        return true;
    m_networkMonitor.reset();
    teardownClient();
    setState(State::Error);
    emit vpnError(QStringLiteral("Failed to start network monitor"));
    return false;
}

void QtTrustTunnelClient::failConnectFatal(const QString &qErr, bool privilegeHint)
{
    QString msg = qErr;
    if (privilegeHint)
        msg += QStringLiteral(" (likely needs sudo/admin privileges)");
    teardownClient();
    m_stopRequested = true;
    setState(State::Error);
    emit vpnError(QString("connect() failed: %1").arg(msg));
}

bool QtTrustTunnelClient::attemptTunnelConnect()
{
    emit connectProgress(tr("Configuring DNS..."));
    if (auto dnsErr = m_client->set_system_dns()) {
        teardownClient();
        m_stopRequested = true;
        setState(State::Error);
        emit vpnError(QString("set_system_dns() failed: %1")
                              .arg(QString::fromStdString(dnsErr->str())));
        return false;
    }

    m_lastConnectAttempt = std::chrono::steady_clock::now();
    emit connectProgress(tr("Establishing tunnel..."));
    if (auto err = m_client->connect(ag::TrustTunnelClient::AutoSetup{})) {
        const QString qErr = QString::fromStdString(err->str());
        if (qErr.contains("Failed to create listener", Qt::CaseInsensitive)) {
            failConnectFatal(qErr, true);
            return false;
        }
        scheduleReconnect(QString("connect() failed: %1").arg(qErr));
        return false;
    }
    return true;
}

void QtTrustTunnelClient::teardownIfReconnecting(bool isReconnect)
{
    if (!isReconnect)
        return;
    emit connectProgress(tr("Disconnecting previous session..."));
    teardownClient();
}

void QtTrustTunnelClient::doConnectAttempt()
{
    if (m_stopRequested)
        return;

    const bool isReconnect = (m_client != nullptr);
    if (m_state != State::Connecting)
        setState(isReconnect ? State::Reconnecting : State::Connecting);

    try {
        teardownIfReconnecting(isReconnect);
        if (!ensureClientReady())
            return;
        if (!attemptTunnelConnect())
            return;
    } catch (const std::exception &e) {
        teardownClient();
        scheduleReconnect(QString::fromUtf8(e.what()));
    }
}
