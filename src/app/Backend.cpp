#include "app/Backend.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/ControlCommand.h"
#include "core/CredentialStore.h"

Backend::Backend(QObject *parent) : QObject(parent) {
    freetunnel::sweepStaleMaterializedConfigs(); // clear any password temp files left by a crash
    m_settings = loadAppSettings();
    reloadConfigs();
    if (!m_settings.last_config_path.isEmpty() && m_paths.contains(m_settings.last_config_path)) {
        m_activePath = m_settings.last_config_path;
    } else if (!m_paths.isEmpty()) {
        m_activePath = m_paths.first();
    }

    connect(&m_client, &VpnHelperClient::stateChanged, this,
            [this](VpnHelperClient::State st) {
                const bool nowConnected = st == VpnHelperClient::State::Connected;
                if (nowConnected && !m_connected) {
                    m_session.restart();
                }
                m_connected = nowConnected;
                m_connecting = st == VpnHelperClient::State::Connecting
                               || st == VpnHelperClient::State::Reconnecting
                               || st == VpnHelperClient::State::WaitingForNetwork;
                // Don't surface "Disconnecting…" during a silent live re-apply
                // (rule changes briefly tear the tunnel down and back up).
                m_disconnecting = st == VpnHelperClient::State::Disconnecting && !m_reapplying;
                if (st == VpnHelperClient::State::Disconnected || st == VpnHelperClient::State::Error) {
                    freetunnel::removeMaterializedConfig(m_materializedConfigPath);
                    m_materializedConfigPath.clear();
                }
                emit stateChanged();
                appendLog(QStringLiteral("INFO"), statusText());
            });
    connect(&m_client, &VpnHelperClient::tunnelStats, this,
            [this](quint64 up, quint64 down) {
                m_accUp += up;
                m_accDown += down;
            });
    connect(&m_client, &VpnHelperClient::connectionInfo, this,
            [this](const QString &m) { appendLog(QStringLiteral("INFO"), m); });
    connect(&m_client, &VpnHelperClient::vpnError, this, [this](const QString &m) {
        appendLog(QStringLiteral("ERROR"), m); // raw message always logged
        // While we're intentionally tearing the tunnel down to switch config or
        // re-apply rules, suppress the scary "disconnected" pop-up.
        if (m_reapplying || m_inConnect)
            return;
        // Turn low-level core messages into something a user can act on.
        const QString lower = m.toLower();
        QString friendly = m;
        if (lower.contains(QLatin1String("disconnect")) || lower.contains(QLatin1String("core")))
            friendly = tr("Connection lost — couldn't reach the server. Check the config or your network.");
        else if (lower.contains(QLatin1String("timeout")) || lower.contains(QLatin1String("timed out")))
            friendly = tr("Server isn't responding (timed out).");
        else if (lower.contains(QLatin1String("auth")) || lower.contains(QLatin1String("credential")))
            friendly = tr("Authentication failed — check the username and password.");
        // Throttle toasts: a flaky connection can spit the same error every few
        // seconds — show it once, then stay quiet for a while unless it changes.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (friendly != m_lastErrorMsg || now - m_lastErrorAt > 30000) {
            m_lastErrorMsg = friendly;
            m_lastErrorAt = now;
            emit errorOccurred(friendly);
        }
    });

    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, [this]() {
        m_downRate = static_cast<double>(m_accDown);
        m_upRate = static_cast<double>(m_accUp);
        m_accUp = m_accDown = 0;
        emit tick();
    });
    m_ticker.start();

    registerHotkeys();
    trimLogFile();
    loadLogTail(); // show previous session's log instead of starting blank
    appendLog(QStringLiteral("INFO"),
              tr("FreeTunnel %1 started").arg(appVersion())); // also ensures the log file exists

    // Connect on startup if requested (deferred so the window shows first).
    if (m_settings.auto_connect_on_start && !m_activePath.isEmpty())
        QTimer::singleShot(600, this, [this] { connectVpn(); });
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

void Backend::connectVpn() {
    if (m_activePath.isEmpty()) {
        emit errorOccurred(tr("Select a config first"));
        return;
    }
    // Show "Connecting…" immediately — the helper handshake (and any elevation
    // prompt) can take a few seconds before the core reports a real state.
    // A subsequent state change (Connected / Error / Disconnected) overrides it.
    if (!m_connecting) {
        m_connecting = true;
        emit stateChanged();
    }
    // Log the endpoint we're dialing (host, address:port, protocol, transport)
    // so the log is useful even before the core chimes in.
    {
        QFile f(m_activePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const freetunnel::ConfigToml c =
                    freetunnel::parseConfigToml(QString::fromUtf8(f.readAll()));
            const bool h3 = c.protocol == QLatin1String("http3");
            appendLog(QStringLiteral("INFO"),
                      tr("Connecting to %1 [%2] · %3 over %4")
                          .arg(c.hostname.isEmpty() ? nameForPath(m_activePath) : c.hostname,
                               c.addresses,
                               h3 ? QStringLiteral("HTTP/3") : QStringLiteral("HTTP/2"),
                               h3 ? QStringLiteral("UDP/QUIC") : QStringLiteral("TCP")));
        }
    }
    // The helper handles elevation; materialize a config the core can read.
    freetunnel::removeMaterializedConfig(m_materializedConfigPath);
    m_materializedConfigPath = freetunnel::materializeConfigForConnect(m_activePath);
    if (m_materializedConfigPath.isEmpty()) {
        m_connecting = false;
        emit stateChanged();
        emit errorOccurred(tr("Config has no password — edit it and try again"));
        return;
    }
    m_inConnect = true; // applySplitRules() below must not trigger a reconnect
    m_client.loadConfigFromFile(m_materializedConfigPath);
    applySplitRules(); // push domain-bypass rules to the core before connecting
    m_client.setKillSwitch(m_settings.killswitch_enabled);
    m_client.connectVpn();
    m_inConnect = false;
}

void Backend::disconnectVpn() {
    if (!m_connected && !m_connecting)
        return; // nothing to disconnect or cancel
    // Show "Disconnecting…" right away; clear the optimistic "Connecting…".
    m_connecting = false;
    m_disconnecting = true;
    emit stateChanged();
    m_client.disconnectVpn(); // aborts a pending startup, or stops a live tunnel
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
    if (m_connected) // switch live — reconnect to the new server (shows "reconnecting")
        connectVpn();
}

void Backend::removeConfig(int index) {
    if (index < 0 || index >= m_paths.size())
        return;
    const QString path = m_paths.at(index);
    freetunnel::CredentialStore::deletePassword(freetunnel::CredentialStore::keyForConfigPath(path));
    QStringList stored = loadStoredConfigs();
    stored.removeAll(path);
    saveStoredConfigs(stored);
    if (m_activePath == path)
        m_activePath.clear();
    reloadConfigs();
    if (m_activePath.isEmpty() && !m_paths.isEmpty())
        m_activePath = m_paths.first();
    emit configChanged();
}
