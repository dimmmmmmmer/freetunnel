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
#include <thread>

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

namespace {

// Bring a core client/monitor pair down in the order the core expects. Both
// calls block, so this must never run on the owner's event loop.
void retireCore(std::unique_ptr<ag::TrustTunnelClient> &client,
                std::unique_ptr<ag::AutoNetworkMonitor> &monitor)
{
    if (monitor) {
        monitor->stop();
        monitor.reset();
    }
    if (client) {
        client->disconnect();
        client.reset();
    }
}

// Same, for a pair that reached the owner's thread anyway: hand it to a detached
// thread rather than paying for the blocking teardown on the event loop. Same
// trade as the abandoned-attempt path — a thread we do not join beats a stalled
// helper — and unlike simply dropping the pointers, the tunnel actually comes
// down instead of being left installed.
void retireCoreDetached(std::unique_ptr<ag::TrustTunnelClient> client,
                        std::unique_ptr<ag::AutoNetworkMonitor> monitor)
{
    if (!client && !monitor)
        return;
    std::thread([client = std::move(client), monitor = std::move(monitor)]() mutable {
        retireCore(client, monitor);
    }).detach();
}

} // namespace

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
// the blocking native call finally returns, and everything it is working on
// lives in its ConnectAttempt — which dies with the worker, so nothing has to be
// rescued here. If the native call never returns, it leaks until process exit —
// the safer failure mode.
//
// The rescue below covers the narrow window where an attempt has already been
// adopted (m_client is ours again) but the thread has not finished signalling
// yet. It is bound to the THREAD, not to this object: with `this` as the
// context, ~QObject tore the connection down and destroyed the captured owners,
// deleting the core client while a thread was still blocked inside connect() —
// a use-after-free in a root process every time the app quit during an
// unreachable-server attempt.
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
    if (m_stopRequested)
        return;
    if (!m_connectThread)
        m_connectThread = new QThread();
    else
        disconnect(m_connectThread, &QThread::started, nullptr, nullptr);

    const quint64 attemptGen = ++m_guard->attemptGen;
    const AttemptPtr ctx = prepareAttempt(attemptGen);
    if (!ctx)
        return; // prepareAttempt already reported why

    if (m_state != State::Connecting)
        setState(ctx->retiredClient ? State::Reconnecting : State::Connecting);

    auto *worker = new QObject();
    worker->moveToThread(m_connectThread);
    QThread *thread = m_connectThread;
    connect(thread, &QThread::started, worker, [ctx, thread]() {
        runAttempt(ctx);
        // Hand the result back under the guard — but only if anyone still wants
        // it. A successful attempt carries a LIVE core client, and letting that
        // just fall out of scope would leave the tunnel, its routes and the DNS
        // override installed while the UI says Disconnected.
        bool wanted = false;
        {
            std::lock_guard<std::mutex> lk(ctx->guard->mutex);
            wanted = ctx->guard->alive && ctx->attemptGen == ctx->guard->attemptGen;
            if (wanted) {
                QMetaObject::invokeMethod(
                        ctx->owner, [ctx]() { ctx->owner->adoptAttempt(ctx); },
                        Qt::QueuedConnection);
            }
        }
        if (!wanted)
            retireCore(ctx->client, ctx->monitor); // outside the lock: this blocks
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

// Owner thread: assemble everything the attempt will need, so the worker never
// has to read our state.
QtTrustTunnelClient::AttemptPtr QtTrustTunnelClient::prepareAttempt(quint64 attemptGen)
{
    if (!reloadStoredConfigIfNeeded())
        return nullptr;

    auto ctx = std::make_shared<ConnectAttempt>();
    ctx->owner = this;
    ctx->guard = m_guard;
    ctx->attemptGen = attemptGen;
#if defined(Q_OS_WIN)
    const uint32_t ifIndex = captureWindowsPhysicalOutbound();
    m_winPhysicalIfIndex.store(ifIndex);
    ctx->boundIf = ifIndex != 0 ? std::to_string(ifIndex) : std::string{};
#endif
    {
        // Take the config out under the lock: the GUI's setKillSwitch /
        // split-rule setters write into the same optional from this thread, and
        // moving out from under them corrupted the heap of an elevated process.
        std::lock_guard<std::mutex> lk(m_configMutex);
        if (!m_config.has_value()) {
            setState(State::Error);
            emit vpnError(QStringLiteral("TrustTunnel config is not set"));
            return nullptr;
        }
        applyCoreLogPathToConfigLocked();
        ctx->config = std::move(*m_config);
        m_config.reset();
    }
    // Start each session from an empty core log. The GUI keeps the durable copy
    // (every line reaches it over IPC), so this file is only a hand-off buffer —
    // and truncating it here, BEFORE the core opens it, avoids doing so behind a
    // descriptor the core already holds.
    resetCoreLogFile();
    // Start tailing before the attempt, not after it succeeds: a connect that
    // FAILS is precisely when the core's own diagnostics are worth having, and
    // adopting the tail only on success discarded them.
    startCoreLogTail();
    ctx->callbacks = makeCallbacks(m_guard);
    // The previous session goes with the attempt: retiring it blocks, and the
    // worker is where blocking belongs.
    ctx->retiredClient = std::move(m_client);
    ctx->retiredMonitor = std::move(m_networkMonitor);
    return ctx;
}

// Worker thread. Both of these end the attempt on failure, retiring whatever
// they built so the owner is never handed a live core object to destroy.
bool QtTrustTunnelClient::applySystemDns(const AttemptPtr &ctx)
{
    const auto dnsErr = ctx->client->set_system_dns();
    if (!dnsErr)
        return true;
    ctx->outcome = ConnectAttempt::Outcome::FatalStop;
    ctx->error = QString("set_system_dns() failed: %1").arg(QString::fromStdString(dnsErr->str()));
    retireCore(ctx->client, ctx->monitor);
    return false;
}

void QtTrustTunnelClient::establishTunnel(const AttemptPtr &ctx)
{
    ctx->startedAt = std::chrono::steady_clock::now();
    const auto err = ctx->client->connect(ag::TrustTunnelClient::AutoSetup{});
    if (!err) {
        ctx->outcome = ConnectAttempt::Outcome::Connected;
        return;
    }
    const QString qErr = QString::fromStdString(err->str());
    // "Failed to create listener" almost always means not elevated — retrying
    // that forever helps nobody.
    const bool notElevated = qErr.contains("Failed to create listener", Qt::CaseInsensitive);
    ctx->outcome = notElevated ? ConnectAttempt::Outcome::FatalStop
                               : ConnectAttempt::Outcome::Retry;
    ctx->privilegeHint = notElevated;
    ctx->error = QString("connect() failed: %1").arg(qErr);
    retireCore(ctx->client, ctx->monitor);
}

// Worker thread. Deliberately static: there is no `this` to reach for, and the
// only contact with the owner is the guarded progress post below.
void QtTrustTunnelClient::runAttempt(const AttemptPtr &ctx)
{
    const auto progress = [&ctx](const QString &text) {
        std::lock_guard<std::mutex> lk(ctx->guard->mutex);
        if (!ctx->guard->alive)
            return;
        QtTrustTunnelClient *owner = ctx->owner;
        QMetaObject::invokeMethod(
                owner, [owner, text]() { emit owner->connectProgress(text); },
                Qt::QueuedConnection);
    };
    try {
        if (ctx->retiredClient || ctx->retiredMonitor) {
            progress(tr("Disconnecting previous session..."));
            retireCore(ctx->retiredClient, ctx->retiredMonitor);
        }

        progress(tr("Initializing VPN core..."));
        ctx->client = std::make_unique<ag::TrustTunnelClient>(std::move(ctx->config),
                                                              std::move(ctx->callbacks));

        progress(tr("Starting network monitor..."));
        ctx->monitor = std::make_unique<ag::AutoNetworkMonitor>(ctx->client.get(), ctx->boundIf);
        if (!ctx->monitor->start()) {
            ctx->outcome = ConnectAttempt::Outcome::FatalKeepGoing;
            ctx->error = QStringLiteral("Failed to start network monitor");
            retireCore(ctx->client, ctx->monitor);
            return;
        }

        progress(tr("Configuring DNS..."));
        if (!applySystemDns(ctx))
            return;

        progress(tr("Establishing tunnel..."));
        establishTunnel(ctx);
    } catch (const std::exception &e) {
        ctx->outcome = ConnectAttempt::Outcome::Retry;
        ctx->error = QString::fromUtf8(e.what());
        retireCore(ctx->client, ctx->monitor);
        retireCore(ctx->retiredClient, ctx->retiredMonitor);
    }
}

// Owner thread: take the result only if we still want it.
void QtTrustTunnelClient::adoptAttempt(const AttemptPtr &ctx)
{
    // Superseded by a newer attempt, a disconnect or a teardown — every one of
    // those bumps the generation. The worker checks this too and retires on its
    // own thread when it can, but teardownClient() can bump the generation AFTER
    // that check and before this runs, so a live, connected client can still
    // arrive here. Dropping it would leave the tunnel up behind a "Disconnected"
    // state; destroying it inline would block this event loop. Neither.
    if (ctx->attemptGen != m_guard->attemptGen) {
        retireCoreDetached(std::move(ctx->client), std::move(ctx->monitor));
        return;
    }
    if (ctx->startedAt != std::chrono::steady_clock::time_point{})
        m_lastConnectAttempt = ctx->startedAt;

    switch (ctx->outcome) {
    case ConnectAttempt::Outcome::Connected:
        m_client = std::move(ctx->client);
        m_networkMonitor = std::move(ctx->monitor);
        return;
    case ConnectAttempt::Outcome::Retry:
        scheduleReconnect(ctx->error);
        return;
    case ConnectAttempt::Outcome::FatalStop:
        if (ctx->privilegeHint) {
            failConnectFatal(ctx->error, true);
            return;
        }
        m_stopRequested = true;
        setState(State::Error);
        emit vpnError(ctx->error);
        return;
    case ConnectAttempt::Outcome::FatalKeepGoing:
        setState(State::Error);
        emit vpnError(ctx->error);
        return;
    }
}

void QtTrustTunnelClient::failConnectFatal(const QString &qErr, bool privilegeHint)
{
    QString msg = qErr;
    if (privilegeHint)
        msg += QStringLiteral(" (likely needs sudo/admin privileges)");
    teardownClient();
    m_stopRequested = true;
    setState(State::Error);
    emit vpnError(msg);
}
