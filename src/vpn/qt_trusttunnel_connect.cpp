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
#include <mutex>

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

static bool privilegeCheckPasses()
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Test-only: unit tests drive the state machine against a mock core and
    // don't run elevated. Compiled out of release builds.
    if (qEnvironmentVariableIsSet("FT_TEST_SKIP_PRIVILEGE_CHECK"))
        return true;
#endif
#ifndef _WIN32
    return ::geteuid() == 0;
#else
    return qt_trusttunnel_is_process_elevated();
#endif
}

void QtTrustTunnelClient::connectVpn()
{
    if (m_state == State::Connecting || m_state == State::Connected
            || m_state == State::Reconnecting || m_state == State::WaitingForNetwork)
        return;
    if (!privilegeCheckPasses()) {
#ifndef _WIN32
        emit vpnError(QStringLiteral("Root permissions are required to initialize VPN (run app with sudo)."));
#else
        emit vpnError(QStringLiteral(
                "Administrator privileges are required to initialize VPN. Restart the app as Administrator."));
#endif
        return;
    }
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
        // The queued "disconnect" command from the GUI is processed before this
        // timer fires, so a user who cancels inside the window would otherwise
        // get the tunnel anyway: connectVpn() clears m_stopRequested
        // unconditionally. Snapshot the disconnect generation and drop the
        // start if anything asked to stop in the meantime.
        const quint64 disconnectGen = m_disconnectGen.load();
        QTimer::singleShot(150, this, [this, disconnectGen, startConnect]() {
            if (m_disconnectGen.load() != disconnectGen)
                return;
            startConnect();
        });
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
// The cleanup handler is deliberately bound to the THREAD, not to this object:
// with `this` as the context, ~QObject tore the connection down and destroyed
// the captured owners, deleting the core client while the abandoned thread was
// still blocked inside its connect() — a use-after-free in a root process every
// time the app quit during an unreachable-server attempt.
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
    ++m_guard->attemptGen; // the stale attempt must drop its result when it resumes
    ++m_sessionGen;        // and events from its core client are no longer ours
    auto client = std::make_shared<std::unique_ptr<ag::TrustTunnelClient>>(std::move(m_client));
    auto monitor =
            std::make_shared<std::unique_ptr<ag::AutoNetworkMonitor>>(std::move(m_networkMonitor));
    connect(thread, &QThread::finished, thread, [client, monitor]() {
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

    const quint64 attemptGen = ++m_guard->attemptGen;
    auto *worker = new QObject();
    worker->moveToThread(m_connectThread);
    QThread *thread = m_connectThread;
    const GuardPtr guard = m_guard;
    connect(thread, &QThread::started, worker, [this, thread, guard, attemptGen]() {
        doConnectAttempt(guard, attemptGen);
        thread->quit();
    });
    // NOT deleteLater: quit() above lands before QThread::run() reaches exec(),
    // so the thread's event loop never runs and a DeferredDelete posted to it is
    // never delivered — that leaked a QObject per attempt, every auto-reconnect
    // included. Destroy it from the thread object's own thread once the worker
    // thread is provably finished. Single-shot: the QThread is REUSED across
    // attempts, so a plain connection would accumulate and re-delete every
    // previous attempt's worker on the next finish.
    connect(
            thread, &QThread::finished, thread, [worker]() { delete worker; },
            Qt::SingleShotConnection);

    thread->start();
}

bool QtTrustTunnelClient::reloadStoredConfigIfNeeded()
{
    QString stored;
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        if (m_config.has_value())
            return true;
        stored = m_lastConfigToml;
    }
    if (!stored.isEmpty())
        return loadConfigFromToml(stored);
    setState(State::Error);
    emit vpnError(QStringLiteral("TrustTunnel config is not set"));
    return false;
}

bool QtTrustTunnelClient::ensureClientReady(const GuardPtr &guard, quint64 attemptGen)
{
    if (m_client)
        return true;
    if (!reloadStoredConfigIfNeeded())
        return false;
    emit connectProgress(tr("Initializing VPN core..."));
#if defined(Q_OS_WIN)
    m_winPhysicalIfIndex = captureWindowsPhysicalOutbound();
    const std::string boundIf =
            m_winPhysicalIfIndex != 0 ? std::to_string(m_winPhysicalIfIndex) : std::string{};
#else
    const std::string boundIf;
#endif
    // Take the config out under the lock: the GUI's setKillSwitch / split-rule
    // setters write into the same optional from the object's thread, and moving
    // out from under them corrupted the heap of an elevated process.
    ag::TrustTunnelConfig config;
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        if (!m_config.has_value()) {
            setState(State::Error);
            emit vpnError(QStringLiteral("TrustTunnel config is not set"));
            return false;
        }
        applyCoreLogPathToConfigLocked();
        config = std::move(*m_config);
        m_config.reset();
    }
    // Start each session from an empty core log. The GUI keeps the durable copy
    // (every line reaches it over IPC), so this file is only a hand-off buffer —
    // and truncating it here, BEFORE the core opens it, avoids doing so behind a
    // descriptor the core already holds.
    resetCoreLogFile();
    // Build into locals and publish only after re-checking: an abandonment
    // during the core constructor or the monitor's start would otherwise hand
    // these to an owner that has already moved on (and moved m_client away).
    auto client = std::make_unique<ag::TrustTunnelClient>(std::move(config), makeCallbacks(guard));
    if (attemptIsStale(guard, attemptGen))
        return false;
    m_client = std::move(client);
    startCoreLogTail();
    emit connectProgress(tr("Starting network monitor..."));
    auto monitor = std::make_unique<ag::AutoNetworkMonitor>(m_client.get(), boundIf);
    if (attemptIsStale(guard, attemptGen))
        return false;
    m_networkMonitor = std::move(monitor);
    if (m_networkMonitor->start())
        return true;
    if (attemptIsStale(guard, attemptGen))
        return false;
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

bool QtTrustTunnelClient::attemptTunnelConnect(const GuardPtr &guard, quint64 attemptGen)
{
    emit connectProgress(tr("Configuring DNS..."));
    // ensureClientReady() can return true on the "already have a client" path
    // without re-checking staleness, and an abandonment moves m_client away — so
    // this first dereference needs the same guard as the one below it.
    ag::TrustTunnelClient *dnsClient = m_client.get();
    if (!dnsClient || attemptIsStale(guard, attemptGen))
        return false;
    const auto dnsErr = dnsClient->set_system_dns();
    // A blocking call may have outlived the join timeout — this attempt was
    // then abandoned and must not touch shared state (the core objects were
    // handed to the zombie cleanup; m_client here is already null), or the
    // owner itself may be gone.
    if (attemptIsStale(guard, attemptGen))
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
    // Re-read after the guard above: an abandonment between the two checks
    // moves m_client away, and dereferencing it here was a null crash.
    ag::TrustTunnelClient *client = m_client.get();
    if (!client)
        return false;
    const auto err = client->connect(ag::TrustTunnelClient::AutoSetup{});
    if (attemptIsStale(guard, attemptGen))
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

void QtTrustTunnelClient::doConnectAttempt(const GuardPtr &guard, quint64 attemptGen)
{
    if (m_stopRequested || attemptIsStale(guard, attemptGen))
        return;

    const bool isReconnect = (m_client != nullptr);
    if (m_state != State::Connecting)
        setState(isReconnect ? State::Reconnecting : State::Connecting);

    try {
        teardownIfReconnecting(isReconnect);
        if (!ensureClientReady(guard, attemptGen))
            return;
        if (!attemptTunnelConnect(guard, attemptGen))
            return;
    } catch (const std::exception &e) {
        if (attemptIsStale(guard, attemptGen))
            return; // abandoned mid-call — the result is stale
        teardownClient();
        scheduleReconnect(QString::fromUtf8(e.what()));
    }
}
