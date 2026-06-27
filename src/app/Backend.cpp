// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/ControlCommand.h"
#include "core/CredentialStore.h"
#include "core/InstanceControl.h"

Backend::Backend(QObject *parent) : QObject(parent) {
    freetunnel::sweepLegacyPlaintextStorage();
    freetunnel::sweepLegacyInstanceAuthFile();
    m_settings = loadAppSettings();
    reloadConfigs();
    if (!m_settings.last_config_path.isEmpty() && m_paths.contains(m_settings.last_config_path)) {
        m_activePath = m_settings.last_config_path;
    } else if (!m_paths.isEmpty()) {
        m_activePath = m_paths.first();
    }

    wireVpnClientSignals();
    m_client.setSessionLogging(logPath(), m_settings.logging_enabled);

    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, [this]() { onStatsTick(); });
    m_ticker.start();

    wireHotkeyLifecycle();

    if (m_settings.logging_enabled) {
        trimLogFile();
        loadLogTail();
        appendLog(QStringLiteral("INFO"),
                  tr("FreeTunnel %1 started").arg(appVersion()));
    }

    // Background update check: badge Settings when a newer release exists.
    QTimer::singleShot(1200, this, [this] { checkForUpdates(false); });

    // Connect on startup if requested (deferred so the window shows first).
    if (m_settings.auto_connect_on_start && !m_activePath.isEmpty())
        QTimer::singleShot(600, this, [this] { connectVpn(); });
}

void Backend::wireVpnClientSignals()
{
    connect(&m_client, &VpnHelperClient::stateChanged, this, &Backend::onVpnClientStateChanged);
    connect(&m_client, &VpnHelperClient::tunnelStats, this,
            [this](quint64 up, quint64 down) {
                m_accUp += up;
                m_accDown += down;
            });
    connect(&m_client, &VpnHelperClient::connectionInfo, this,
            [this](const QString &m) { appendLog(QStringLiteral("INFO"), m); });
    connect(&m_client, &VpnHelperClient::connectProgress, this,
            [this](const QString &m) { appendLog(QStringLiteral("INFO"), m); });
    connect(&m_client, &VpnHelperClient::coreLogLine, this,
            [this](const QString &line) { appendCoreLog(line); });
    connect(&m_client, &VpnHelperClient::vpnError, this, &Backend::onVpnErrorReceived);
}

void Backend::clearReapplyingIfDone(VpnHelperClient::State st, bool nowConnected)
{
    if (m_reapplying && (nowConnected || st == VpnHelperClient::State::Error))
        m_reapplying = false;
}

void Backend::applyVpnClientState(VpnHelperClient::State st)
{
    const bool nowConnected = st == VpnHelperClient::State::Connected;
    if (nowConnected && !m_connected)
        m_session.restart();
    m_connected = nowConnected;
    m_connecting = st == VpnHelperClient::State::Connecting
                   || st == VpnHelperClient::State::Reconnecting
                   || st == VpnHelperClient::State::WaitingForNetwork;
    clearReapplyingIfDone(st, nowConnected);
    m_disconnecting = st == VpnHelperClient::State::Disconnecting && !m_reapplying;
}

void Backend::onVpnClientStateChanged(VpnHelperClient::State st)
{
    applyVpnClientState(st);
    emit stateChanged();
    appendLog(QStringLiteral("INFO"), statusText());
    // A config switch / live rule reapply disconnects first, then reconnects only
    // once the old tunnel is fully down. Defer so we never re-enter the VPN client
    // from inside its own state callback (disconnectVpn can emit this synchronously).
    if (st == VpnHelperClient::State::Disconnected && m_pendingReconnect)
        QTimer::singleShot(0, this, [this]() { firePendingReconnect(); });
}

bool Backend::isInternalVpnError(const QString &m) const
{
    const QString lower = m.toLower();
    return lower.contains(QLatin1String("recovery:"))
           || lower.contains(QLatin1String("fd watchdog:"))
           || lower.contains(QLatin1String("network wait timeout:"));
}

namespace {

QString classifyVpnErrorMessage(const QString &lower)
{
    if (lower.contains(QLatin1String("disconnect")) || lower.contains(QLatin1String("core"))) {
        return QCoreApplication::translate(
                "Backend",
                "Connection lost — couldn't reach the server. Check the config or your network.");
    }
    if (lower.contains(QLatin1String("timeout")) || lower.contains(QLatin1String("timed out"))) {
        return QCoreApplication::translate("Backend", "Server isn't responding (timed out).");
    }
    if (lower.contains(QLatin1String("auth")) || lower.contains(QLatin1String("credential"))) {
        return QCoreApplication::translate("Backend",
                                           "Authentication failed — check the username and password.");
    }
    return QString();
}

} // namespace

QString Backend::friendlyVpnError(const QString &m) const
{
    if (m.startsWith(QStringLiteral("Connection failed:"), Qt::CaseInsensitive))
        return m;
    const QString classified = classifyVpnErrorMessage(m.toLower());
    return classified.isEmpty() ? m : classified;
}

void Backend::emitDedupedVpnError(const QString &friendly)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (friendly == m_lastErrorMsg && now - m_lastErrorAt <= 30000)
        return;
    m_lastErrorMsg = friendly;
    m_lastErrorAt = now;
    emit errorOccurred(friendly);
}

void Backend::onVpnErrorReceived(const QString &m)
{
    appendLog(QStringLiteral("ERROR"), m);
    if (m_reapplying || m_inConnect || isInternalVpnError(m))
        return;
    emitDedupedVpnError(friendlyVpnError(m));
}

void Backend::onStatsTick()
{
    m_downRate = static_cast<double>(m_accDown);
    m_upRate = static_cast<double>(m_accUp);
    m_accUp = m_accDown = 0;
    emit tick();
}

QString Backend::statusText() const {
    switch (m_client.state()) {
    case VpnHelperClient::State::Connected: return tr("Connected");
    case VpnHelperClient::State::Connecting: return tr("Connecting…");
    case VpnHelperClient::State::Reconnecting: return tr("Reconnecting…");
    case VpnHelperClient::State::WaitingForNetwork: return tr("Waiting for network…");
    case VpnHelperClient::State::Disconnecting: return tr("Disconnecting…");
    case VpnHelperClient::State::Error: return tr("Error");
    default: return tr("Off");
    }
}

QString Backend::sessionTime() const {
    if (!m_connected)
        return QString();
    qint64 secs = m_session.elapsed() / 1000;
    return QStringLiteral("%1:%2:%3")
            .arg(secs / 3600, 2, 10, QChar('0'))
            .arg((secs % 3600) / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0'));
}

static QString fmtRate(double bytesPerSec) {
    return QString::number(bytesPerSec / 1.0e6, 'f', 1);
}
QString Backend::downSpeed() const { return fmtRate(m_downRate); }
QString Backend::upSpeed() const { return fmtRate(m_upRate); }

QString Backend::activeConfig() const {
    return m_activePath.isEmpty() ? tr("No config") : nameForPath(m_activePath);
}

QString Backend::nameForPath(const QString &path) const {
    return QFileInfo(path).completeBaseName();
}

void Backend::reloadConfigs() {
    m_paths = loadStoredConfigs();
    m_names.clear();
    for (const QString &p : m_paths)
        m_names << nameForPath(p);
    m_pings.clear(); // indices shifted — stale pings would mislabel servers
    emit pingsChanged();
    emit configsChanged();
}

void Backend::toggle() {
    // A click while connecting cancels the attempt; while connected it
    // disconnects; otherwise it starts connecting.
    if (m_connected || m_connecting)
        disconnectVpn();
    else
        connectVpn();
}

bool Backend::shouldSkipConnectAttempt() const
{
    return !m_reapplying && (m_connected || m_connecting || m_disconnecting);
}

void Backend::logConnectAttempt()
{
    QFile f(m_activePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(f.readAll()));
    const bool h3 = c.protocol == QLatin1String("http3");
    appendLog(QStringLiteral("INFO"),
              tr("Connecting to %1 [%2] · %3 over %4")
                      .arg(c.hostname.isEmpty() ? nameForPath(m_activePath) : c.hostname,
                           c.addresses,
                           h3 ? QStringLiteral("HTTP/3") : QStringLiteral("HTTP/2"),
                           h3 ? QStringLiteral("UDP/QUIC") : QStringLiteral("TCP")));
}

bool Backend::loadConnectTomlOrFail(QString *tomlOut)
{
    const QString connectToml = freetunnel::buildConnectConfigToml(m_activePath);
    if (!connectToml.isEmpty()) {
        *tomlOut = connectToml;
        return true;
    }
    m_connecting = false;
    emit stateChanged();
    emit errorOccurred(tr("Config has no password — edit it and try again"));
    return false;
}

void Backend::connectVpn() {
    if (m_activePath.isEmpty()) {
        emit errorOccurred(tr("Select a config first"));
        return;
    }
    // Connect hotkey / deep link: no-op when already up or a session is in flight.
    // reconnectActiveConfig() sets m_reapplying so live rule edits can still rebuild.
    if (shouldSkipConnectAttempt())
        return;
    // Show "Connecting…" immediately — the helper handshake (and any elevation
    // prompt) can take a few seconds before the core reports a real state.
    // A subsequent state change (Connected / Error / Disconnected) overrides it.
    if (!m_connecting) {
        m_connecting = true;
        emit stateChanged();
    }
    logConnectAttempt();
    QString connectToml;
    if (!loadConnectTomlOrFail(&connectToml))
        return;
    m_inConnect = true;
    m_client.loadConfigFromToml(connectToml);
    applySplitRules(); // push domain-bypass rules to the core before connecting
    m_client.setKillSwitch(m_settings.killswitch_enabled);
    m_client.setSessionLogging(logPath(), m_settings.logging_enabled);
    m_client.connectVpn();
    m_inConnect = false;
}

void Backend::disconnectVpn() {
    if (!m_connected && !m_connecting)
        return; // nothing to disconnect or cancel
    m_reapplying = false;
    m_pendingReconnect = false; // an explicit disconnect cancels a config-switch reconnect
    // Show "Disconnecting…" right away; clear the optimistic "Connecting…".
    m_connecting = false;
    m_disconnecting = true;
    emit stateChanged();
    m_client.disconnectVpn(); // aborts a pending startup, or stops a live tunnel
}

void Backend::prepareQuit() {
    if (m_shutdownPrepared)
        return;
    m_shutdownPrepared = true;
    if (!m_quitting) {
        m_quitting = true;
        emit aboutToShutdown();
    }
    unregisterHotkeys();
    m_ticker.stop();
    m_client.shutdown();
    freetunnel::sweepStaleMaterializedConfigs();
}

bool Backend::applicationClosingDown() const
{
    return m_quitting || QCoreApplication::closingDown();
}

void Backend::quitApplication() {
    if (m_quitting)
        return;
    m_quitting = true;
    emit aboutToShutdown();
    // Let QML hide the tray icon before exit (macOS keeps running while status items exist).
    QMetaObject::invokeMethod(qApp, []() { QCoreApplication::exit(0); }, Qt::QueuedConnection);
}

QString Backend::credentialStorageWarning() const
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (!freetunnel::CredentialStore::secureStorageAvailable()) {
        return tr("Secure credential storage is unavailable. Install gnome-keyring or "
                  "KWallet (with secret-tool) before saving VPN passwords.");
    }
#endif
    return QString();
}

void Backend::handleControl(const QString &command) {
    using freetunnel::ControlAction;
    const auto cmd = freetunnel::parseControlCommand(command);
    switch (cmd.action) {
    case ControlAction::ImportLink: importDeepLink(cmd.payload); break;
    case ControlAction::Toggle:     toggle(); break;
    case ControlAction::Connect:    connectVpn(); break;
    case ControlAction::Disconnect: disconnectVpn(); break;
    case ControlAction::None:       break; // window raise handled by the caller
    }
}

void Backend::selectConfig(int index) {
    if (index < 0 || index >= m_paths.size())
        return;
    if (m_paths.at(index) == m_activePath) // already active — don't reconnect
        return;
    m_activePath = m_paths.at(index);
    m_settings.last_config_path = m_activePath;
    persistSettings();
    emit configChanged();
    if (m_connected || m_connecting)
        reconnectActiveConfig();
}

void Backend::removeConfig(int index) {
    if (index < 0 || index >= m_paths.size())
        return;
    const QString path = m_paths.at(index);
    freetunnel::CredentialStore::deletePassword(freetunnel::CredentialStore::keyForConfigPath(path));
    QStringList stored = loadStoredConfigs();
    stored.removeAll(path);
    saveStoredConfigs(stored);
    if (m_settings.config_profiles.remove(path) > 0)
        persistSettings();
    if (m_activePath == path)
        m_activePath.clear();
    reloadConfigs();
    if (m_activePath.isEmpty() && !m_paths.isEmpty())
        m_activePath = m_paths.first();
    emit configChanged();
}
