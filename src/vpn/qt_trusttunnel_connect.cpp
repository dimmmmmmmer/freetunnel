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
#include <memory>

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

// Join the connect thread within waitMs; returns true when no attempt thread
// is left running. On timeout the stuck thread is ABANDONED, not terminate()d:
// pthread_cancel/TerminateThread can kill it while core/CRT locks are held and
// deadlock the whole process later. An abandoned thread exits by itself when
// the blocking native call finally returns; the attempt-generation guard makes
// it drop its stale result, and the core client/monitor move into its cleanup
// handler so it never touches freed objects. If the native call never returns,
// they leak until process exit — the safer failure mode.
//
// NOTE: the abandoned thread still references this QtTrustTunnelClient. The
// helper process keeps the client alive until exit, which satisfies that.
bool QtTrustTunnelClient::joinOrAbandonConnectThread(int waitMs)
{
    QThread *thread = m_connectThread;
    if (!thread || !thread->isRunning())
        return true;
    thread->quit();
    if (thread->wait(waitMs))
        return true;

    qWarning("[QtTrustTunnelClient] connect attempt stuck in a native call for %d ms — "
             "abandoning the thread",
             waitMs);
    ++m_attemptGen; // the stale attempt must drop its result when it resumes
    ++m_sessionGen; // and events from its core client are no longer ours
    auto client = std::make_shared<std::unique_ptr<ag::TrustTunnelClient>>(std::move(m_client));
    auto monitor =
            std::make_shared<std::unique_ptr<ag::AutoNetworkMonitor>>(std::move(m_networkMonitor));
    connect(thread, &QThread::finished, this, [client, monitor]() {
        if (*monitor) {
            (*monitor)->stop();
            monitor->reset();
        }
        if (*client) {
            (*client)->disconnect();
            client->reset();
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_connectThread = nullptr; // the next attempt gets a fresh thread
    return false;
}

void QtTrustTunnelClient::doConnectAttemptInThread()
{
    joinOrAbandonConnectThread(m_stuckJoinWaitMs);
    if (!m_connectThread)
        m_connectThread = new QThread();
    else
        disconnect(m_connectThread, &QThread::started, nullptr, nullptr);

    const quint64 attemptGen = ++m_attemptGen;
    auto *worker = new QObject();
    worker->moveToThread(m_connectThread);
    QThread *thread = m_connectThread;
    connect(thread, &QThread::started, worker, [this, worker, thread, attemptGen]() {
        doConnectAttempt(attemptGen);
        worker->deleteLater();
        thread->quit();
    });

    thread->start();
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

bool QtTrustTunnelClient::attemptTunnelConnect(quint64 attemptGen)
{
    emit connectProgress(tr("Configuring DNS..."));
    const auto dnsErr = m_client->set_system_dns();
    // A blocking call may have outlived the join timeout — this attempt was
    // then abandoned and must not touch shared state (the core objects were
    // handed to the zombie cleanup; m_client here is already null).
    if (attemptGen != m_attemptGen)
        return false;
    if (dnsErr) {
        teardownClient();
        m_stopRequested = true;
        setState(State::Error);
        emit vpnError(QString("set_system_dns() failed: %1")
                              .arg(QString::fromStdString(dnsErr->str())));
        return false;
    }

    m_lastConnectAttempt = std::chrono::steady_clock::now();
    emit connectProgress(tr("Establishing tunnel..."));
    const auto err = m_client->connect(ag::TrustTunnelClient::AutoSetup{});
    if (attemptGen != m_attemptGen)
        return false; // abandoned while blocked (see above)
    if (err) {
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

void QtTrustTunnelClient::doConnectAttempt(quint64 attemptGen)
{
    if (m_stopRequested || attemptGen != m_attemptGen)
        return;

    const bool isReconnect = (m_client != nullptr);
    if (m_state != State::Connecting)
        setState(isReconnect ? State::Reconnecting : State::Connecting);

    try {
        teardownIfReconnecting(isReconnect);
        if (!ensureClientReady())
            return;
        if (!attemptTunnelConnect(attemptGen))
            return;
    } catch (const std::exception &e) {
        if (attemptGen != m_attemptGen)
            return; // abandoned mid-call — the result is stale
        teardownClient();
        scheduleReconnect(QString::fromUtf8(e.what()));
    }
}
