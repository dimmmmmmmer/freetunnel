// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_platform.h"

#include <QCoreApplication>
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

void QtTrustTunnelClient::connectVpn()
{
    if (m_state == State::Connecting || m_state == State::Connected
            || m_state == State::Reconnecting || m_state == State::WaitingForNetwork)
        return;
#ifndef _WIN32
    if (::geteuid() != 0) {
        emit vpnError(QStringLiteral("Root permissions are required to initialize VPN (run app with sudo)."));
        return;
    }
#else
    if (!qt_trusttunnel_is_process_elevated()) {
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
    if (thread() != QCoreApplication::instance()->thread()) {
        doConnectAttempt();
        return;
    }
    doConnectAttemptInThread();
}

void QtTrustTunnelClient::beginConnect(const QString &configToml, const QString &configPath)
{
    const auto st = state();
    const bool needsTeardown = st != State::Disconnected && st != State::Error;
    auto startConnect = [this, configToml, configPath]() {
        const bool loaded = !configToml.isEmpty() ? loadConfigFromToml(configToml)
                                                  : loadConfigFromFile(configPath);
        if (!loaded) {
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
        m_connectThread.wait();
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
    if (!m_lastConfigPath.isEmpty())
        return loadConfigFromFile(m_lastConfigPath);
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
    emit connectProgress(tr("Initializing VPN core..."));
    m_client = std::make_unique<ag::TrustTunnelClient>(std::move(*m_config), makeCallbacks());
    m_config.reset();
    emit connectProgress(tr("Starting network monitor..."));
    m_networkMonitor = std::make_unique<ag::AutoNetworkMonitor>(m_client.get(), "");
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
