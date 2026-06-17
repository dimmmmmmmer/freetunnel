#include "app/Backend.h"

#include <QAbstractSocket>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QWindow>
#include <QTime>
#include <QUrl>
#include <QVariantMap>

#include <QHotkey>

#include "core/AppSettings.h"
#include "core/AppUiUtils.h"
#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/ControlCommand.h"
#include "core/UpdateChecker.h"

#ifndef _WIN32
#include <unistd.h>
#endif

Backend::Backend(QObject *parent) : QObject(parent) {
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

// Keep the on-disk log from growing without bound: if it exceeds the cap,
// drop the oldest entries and keep the most recent tail.
void Backend::trimLogFile() {
    const QString p = logPath();
    QFileInfo fi(p);
    if (!fi.exists() || fi.size() <= 5 * 1024 * 1024) // 5 MB cap
        return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return;
    f.seek(fi.size() - 2 * 1024 * 1024); // keep the last ~2 MB
    f.readLine();                        // discard the partial first line
    const QByteArray tail = f.readAll();
    f.close();
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("... (older log entries trimmed) ...\n");
        f.write(tail);
        f.close();
    }
}

void Backend::registerHotkeys() {
    delete m_hkToggle;     m_hkToggle = nullptr;
    delete m_hkConnect;    m_hkConnect = nullptr;
    delete m_hkDisconnect; m_hkDisconnect = nullptr;

    if (!m_settings.hotkeys_enabled) // master switch off — leave everything unbound
        return;
    auto make = [this](const QString &seq, void (Backend::*slot)()) -> QHotkey * {
        const QString s = seq.trimmed();
        if (s.isEmpty())
            return nullptr;
        QKeySequence ks(s);
        if (ks.isEmpty())
            return nullptr;
        auto *hk = new QHotkey(ks, true /*autoRegister*/, this);
        connect(hk, &QHotkey::activated, this, slot);
        return hk;
    };
    m_hkToggle = make(m_settings.hotkey_toggle, &Backend::toggle);
    m_hkConnect = make(m_settings.hotkey_connect, &Backend::connectVpn);
    m_hkDisconnect = make(m_settings.hotkey_disconnect, &Backend::disconnectVpn);
}

void Backend::setHotkeysEnabled(bool v) {
    if (m_settings.hotkeys_enabled == v) return;
    m_settings.hotkeys_enabled = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
void Backend::setHotkeyToggle(const QString &v) {
    if (m_settings.hotkey_toggle == v) return;
    m_settings.hotkey_toggle = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
void Backend::setHotkeyConnect(const QString &v) {
    if (m_settings.hotkey_connect == v) return;
    m_settings.hotkey_connect = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
void Backend::setHotkeyDisconnect(const QString &v) {
    if (m_settings.hotkey_disconnect == v) return;
    m_settings.hotkey_disconnect = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}

// ---------- updater ----------

QString Backend::appVersion() const {
#ifdef FREETUNNEL_VERSION
    return QStringLiteral(FREETUNNEL_VERSION);
#else
    return QStringLiteral("1.0.0");
#endif
}

QString Backend::coreVersion() const {
#ifdef FREETUNNEL_CORE_REF
    return QStringLiteral(FREETUNNEL_CORE_REF);
#else
    return QStringLiteral("unknown");
#endif
}

void Backend::checkForUpdates() {
    if (m_updateState == QLatin1String("checking"))
        return;
    if (!m_updater) {
        m_updater = new UpdateChecker(QStringLiteral("enrvate/freetunnel"), appVersion(), this);
        connect(m_updater, &UpdateChecker::updateAvailable, this,
                [this](const UpdateChecker::ReleaseInfo &info) {
                    m_updateState = QStringLiteral("available");
                    m_latestVersion = info.version;
                    m_latestUrl = info.htmlUrl;
                    m_updateMessage = tr("Version %1 is available").arg(info.version);
                    emit updateChanged();
                });
        connect(m_updater, &UpdateChecker::noUpdateAvailable, this,
                [this](const QString &) {
                    // No newer release (incl. 404 when no releases exist yet).
                    m_updateState = QStringLiteral("current");
                    m_updateMessage = tr("You have the latest version");
                    emit updateChanged();
                });
    }
    m_updateState = QStringLiteral("checking");
    m_updateMessage = tr("Checking…");
    emit updateChanged();
    m_updater->checkNow();
}

void Backend::openLatestRelease() {
    const QString url = m_latestUrl.isEmpty()
            ? QStringLiteral("https://github.com/enrvate/freetunnel/releases/latest")
            : m_latestUrl;
    QDesktopServices::openUrl(QUrl(url));
}

void Backend::openUrl(const QString &url) {
    QDesktopServices::openUrl(QUrl(url));
}

void Backend::startWindowDrag(QObject *window) {
    // The QQuickWindow content view eats mouse events, so AppKit's
    // movableByWindowBackground never fires; drive the native move directly.
    if (auto *w = qobject_cast<QWindow *>(window))
        w->startSystemMove();
}

// ---------- misc: log path, autostart, ping, clipboard import ----------

QString Backend::logPath() const {
    if (!m_settings.log_path.isEmpty())
        return m_settings.log_path;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/freetunnel.log");
}

#if defined(Q_OS_WIN)
static const char *kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool Backend::autoStart() const {
    QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return !r.value(QStringLiteral("FreeTunnel")).toString().isEmpty();
}
void Backend::setAutoStart(bool v) {
    QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (v)
        r.setValue(QStringLiteral("FreeTunnel"),
                   QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                           + QLatin1Char('"'));
    else
        r.remove(QStringLiteral("FreeTunnel"));
    emit settingsChanged();
}
#elif defined(Q_OS_MACOS)
static QString autoStartPath() {
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/com.freetunnel.app.plist");
}
bool Backend::autoStart() const { return QFileInfo::exists(autoStartPath()); }
void Backend::setAutoStart(bool v) {
    const QString p = autoStartPath();
    if (v) {
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "  <key>Label</key><string>com.freetunnel.app</string>\n"
                "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
                "  <key>RunAtLoad</key><true/>\n"
                "</dict></plist>\n").arg(QCoreApplication::applicationFilePath()).toUtf8());
    } else {
        QFile::remove(p);
    }
    emit settingsChanged();
}
#else
static QString autoStartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/autostart/freetunnel.desktop");
}
bool Backend::autoStart() const { return QFileInfo::exists(autoStartPath()); }
void Backend::setAutoStart(bool v) {
    const QString p = autoStartPath();
    if (v) {
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=FreeTunnel\n"
                                   "Exec=%1\nTerminal=false\nX-GNOME-Autostart-enabled=true\n")
                            .arg(QCoreApplication::applicationFilePath()).toUtf8());
    } else {
        QFile::remove(p);
    }
    emit settingsChanged();
}
#endif

// Read the first endpoint "host:port" out of a config TOML.
static QString firstAddress(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(QStringLiteral("addresses\\s*=\\s*\\[\\s*\"([^\"]+)\""));
    const auto m = re.match(content);
    return m.hasMatch() ? m.captured(1) : QString();
}

void Backend::pingConfigs() {
    m_pings.clear();
    for (int i = 0; i < m_paths.size(); ++i)
        m_pings << QStringLiteral("…");
    emit pingsChanged();

    for (int i = 0; i < m_paths.size(); ++i) {
        const QString addr = firstAddress(m_paths.at(i));
        const int colon = addr.lastIndexOf(':');
        if (colon < 0) {
            m_pings[i] = QStringLiteral("—");
            emit pingsChanged();
            continue;
        }
        const QString host = addr.left(colon);
        const quint16 port = addr.mid(colon + 1).toUShort();
        auto *sock = new QTcpSocket(this);
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        connect(sock, &QTcpSocket::connected, this, [this, i, sock, t0]() {
            if (i < m_pings.size())
                m_pings[i] = QString::number(QDateTime::currentMSecsSinceEpoch() - t0)
                        + tr(" ms");
            sock->abort();
            sock->deleteLater();
            emit pingsChanged();
        });
        connect(sock, &QTcpSocket::errorOccurred, this,
                [this, i, sock](QAbstractSocket::SocketError) {
                    if (i < m_pings.size() && m_pings[i].toString() == QStringLiteral("…"))
                        m_pings[i] = QStringLiteral("—");
                    sock->deleteLater();
                    emit pingsChanged();
                });
        QTimer::singleShot(3000, sock, [this, i, sock]() {
            if (sock->state() != QAbstractSocket::ConnectedState) {
                if (i < m_pings.size() && m_pings[i].toString() == QStringLiteral("…")) {
                    m_pings[i] = QStringLiteral("—");
                    emit pingsChanged();
                }
                sock->abort();
                sock->deleteLater();
            }
        });
        sock->connectToHost(host, port);
    }
}

bool Backend::importFromClipboard() {
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        emit errorOccurred(tr("Clipboard is empty"));
        return false;
    }
    // Accept tt:// links and trusttunnel.org share links (…#tt=… / …?tt=…).
    if (text.startsWith(QStringLiteral("tt://")) || text.contains(QStringLiteral("tt=")))
        return importDeepLink(text);
    emit errorOccurred(tr("No tt:// link in the clipboard"));
    return false;
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
    // The helper handles elevation; just hand it the config + rules.
    m_inConnect = true; // applySplitRules() below must not trigger a reconnect
    m_client.loadConfigFromFile(m_activePath);
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

bool Backend::importFile(const QString &path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file://")))
        p = QUrl(p).toLocalFile();
    if (!QFileInfo::exists(p)) {
        emit errorOccurred(tr("File not found: %1").arg(p));
        return false;
    }
    // Validate it's a usable config TOML before importing.
    {
        QFile vf(p);
        if (!vf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit errorOccurred(tr("Could not read the file"));
            return false;
        }
        const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(vf.readAll()));
        if (c.addresses.trimmed().isEmpty() || c.username.trimmed().isEmpty()) {
            emit errorOccurred(tr("Not a valid TrustTunnel config (missing address or credentials)"));
            return false;
        }
    }
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(p)) {
        stored << p;
        saveStoredConfigs(stored);
    }
    const bool hadNoActive = m_activePath.isEmpty();
    reloadConfigs();
    if (hadNoActive) { // first config in an empty list — make it the active one
        m_activePath = p;
        m_settings.last_config_path = p;
        persistSettings();
        emit configChanged();
    }
    return true;
}

void Backend::appendLog(const QString &level, const QString &msg) {
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QVariantMap e;
    e[QStringLiteral("time")] = time;
    e[QStringLiteral("level")] = level;
    e[QStringLiteral("msg")] = msg;
    m_log.append(e);
    if (m_log.size() > 500)
        m_log.removeFirst();
    // Persist to disk so the log survives restarts and shows up in the folder.
    const QString lp = logPath();
    QDir().mkpath(QFileInfo(lp).absolutePath());
    // The log can contain connection/domain info — keep it owner-only. Set perms
    // only when first creating the file to avoid a syscall on every line.
    const bool logExisted = QFileInfo::exists(lp);
    QFile f(lp);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"))
                + QLatin1Char(' ') + time + QLatin1Char('\t') + level + QLatin1Char('\t') + msg
                + QLatin1Char('\n');
        f.write(line.toUtf8());
        f.close();
        if (!logExisted)
            QFile::setPermissions(lp, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    emit logChanged();
}

// Load the most recent on-disk log lines into the in-memory view at startup so
// history isn't lost across restarts.
void Backend::loadLogTail() {
    QFile f(logPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    f.close();
    const int start = qMax(0, lines.size() - 200);
    for (int i = start; i < lines.size(); ++i) {
        const QStringList parts = lines.at(i).split(QLatin1Char('\t'));
        if (parts.size() < 3) continue;
        const QString dt = parts.at(0); // "yyyy-MM-dd HH:mm:ss"
        QVariantMap e;
        e[QStringLiteral("time")] = dt.section(QLatin1Char(' '), 1, 1);
        e[QStringLiteral("level")] = parts.at(1);
        e[QStringLiteral("msg")] = parts.mid(2).join(QLatin1Char('\t'));
        m_log.append(e);
    }
    if (!m_log.isEmpty())
        emit logChanged();
}

void Backend::copyToClipboard(const QString &text) const {
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

QString Backend::readTextFile(const QString &pathOrUrl) const {
    QString p = pathOrUrl;
    if (p.startsWith(QStringLiteral("file://")))
        p = QUrl(p).toLocalFile();
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

QString Backend::logText() const {
    QStringList out;
    for (const QVariant &v : m_log) {
        const QVariantMap e = v.toMap();
        out << e.value(QStringLiteral("time")).toString() + QLatin1Char(' ')
               + e.value(QStringLiteral("level")).toString() + QLatin1Char(' ')
               + e.value(QStringLiteral("msg")).toString();
    }
    return out.join(QLatin1Char('\n'));
}

void Backend::clearLogs() {
    m_log.clear();
    QFile::remove(logPath()); // also clear the on-disk file
    emit logChanged();
}

void Backend::openLogFolder() {
    QString path = m_settings.log_path;
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/freetunnel.log");
    }
    const QString dir = QFileInfo(path).absolutePath();
#if defined(Q_OS_MACOS)
    if (QFileInfo::exists(path))
        QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
    else
        QProcess::startDetached(QStringLiteral("open"), {dir});
#elif defined(Q_OS_WIN)
    if (QFileInfo::exists(path))
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
    else
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(dir)});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {dir});
#endif
}

bool Backend::createConfig(const QVariantMap &f) {
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    freetunnel::ConfigToml ct;
    ct.hostname = f.value(QStringLiteral("hostname")).toString().trimmed();
    ct.addresses = f.value(QStringLiteral("addresses")).toString().trimmed();
    ct.username = f.value(QStringLiteral("username")).toString().trimmed();
    ct.password = f.value(QStringLiteral("password")).toString();
    if (ct.hostname.isEmpty() || ct.addresses.isEmpty() || ct.username.isEmpty() || ct.password.isEmpty()) {
        emit errorOccurred(tr("Fill in host, address, username and password"));
        return false;
    }
    // Each address must be host:port (e.g. 1.2.3.4:443 or [::1]:443).
    const QStringList addrs = ct.addresses.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : addrs) {
        const QString a = raw.trimmed();
        const int colon = a.lastIndexOf(QLatin1Char(':'));
        bool portOk = false;
        const int port = colon >= 0 ? a.mid(colon + 1).toInt(&portOk) : 0;
        if (colon <= 0 || !portOk || port < 1 || port > 65535) {
            emit errorOccurred(tr("Address must be host:port, e.g. 1.2.3.4:443"));
            return false;
        }
    }
    ct.protocol = f.value(QStringLiteral("protocol"), QStringLiteral("http2")).toString();
    ct.allowIpv6 = f.value(QStringLiteral("allowIpv6"), true).toBool();
    ct.certificate = f.value(QStringLiteral("certificate")).toString().trimmed();
    ct.dns = f.value(QStringLiteral("dns")).toString();
    ct.customSni = f.value(QStringLiteral("customSni")).toString();
    ct.clientRandom = f.value(QStringLiteral("clientRandom")).toString();
    // DNS servers (when given): plain IP, or an encrypted-DNS URL
    // (tls://, https://, quic://, h3://, sdns://, udp://, tcp://).
    const QStringList dnsList = ct.dns.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                            Qt::SkipEmptyParts);
    static const QRegularExpression dnsScheme(
        QStringLiteral("^(tls|https|quic|h3|sdns|udp|tcp)://"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : dnsList) {
        const QString d = raw.trimmed();
        if (dnsScheme.match(d).hasMatch())
            continue; // an encrypted-DNS endpoint URL — accept as-is
        // Otherwise a bare IP (optionally with :port).
        QString host = d;
        const int colon = host.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && !host.contains(QLatin1Char('[')) && host.count(QLatin1Char(':')) == 1)
            host = host.left(colon);
        if (QHostAddress(host).isNull()) {
            emit errorOccurred(tr("DNS must be an IP or DoT/DoH URL (e.g. 1.1.1.1, tls://8.8.8.8)"));
            return false;
        }
    }
    // Client random (when given) must be a hex string.
    const QString cr = ct.clientRandom.trimmed();
    if (!cr.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(cr).hasMatch()) {
        emit errorOccurred(tr("Client random must be hexadecimal"));
        return false;
    }
    const QString t = freetunnel::buildConfigToml(ct);
    const QString hostname = ct.hostname;

    QString safe;
    const QString src = name.isEmpty() ? hostname : name;
    for (const QChar &c : src)
        safe += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('_');
    if (safe.isEmpty())
        safe = QStringLiteral("config-%1").arg(QDateTime::currentSecsSinceEpoch());

    const int editIndex = f.value(QStringLiteral("editIndex"), -1).toInt();
    const QString oldPath = (editIndex >= 0 && editIndex < m_paths.size())
            ? m_paths.at(editIndex) : QString();

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(safe + QStringLiteral(".toml"));
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    file.write(t.toUtf8());
    file.close();
    // The config holds VPN credentials in plaintext; restrict it to the owner so
    // other local users can't read them. The elevated helper runs as root and
    // can still read it. Best-effort (ignored on filesystems without perms).
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QStringList stored = loadStoredConfigs();
    const bool editing = editIndex >= 0;
    const int storedIdx = oldPath.isEmpty() ? -1 : stored.indexOf(oldPath);
    const bool wasActive = !oldPath.isEmpty() && m_activePath == oldPath;
    if (!oldPath.isEmpty() && oldPath != target) { // edit + rename
        QFile::remove(oldPath);
        if (wasActive) m_activePath = target;
    }
    if (storedIdx >= 0) {
        stored[storedIdx] = target; // keep the list position when editing
        stored.removeDuplicates();
    } else if (!stored.contains(target)) {
        stored << target; // a brand-new config is appended
    }
    saveStoredConfigs(stored);
    reloadConfigs();
    if (!editing) {
        // A newly created config becomes the active selection.
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
    } else if (wasActive) {
        m_settings.last_config_path = m_activePath;
        persistSettings();
    }
    emit configChanged();
    return true;
}

QVariantMap Backend::configFields(int index) const {
    QVariantMap f;
    if (index < 0 || index >= m_paths.size())
        return f;
    QFile file(m_paths.at(index));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return f;
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(file.readAll()));
    f[QStringLiteral("name")] = nameForPath(m_paths.at(index));
    f[QStringLiteral("hostname")] = c.hostname;
    f[QStringLiteral("addresses")] = c.addresses;
    f[QStringLiteral("username")] = c.username;
    f[QStringLiteral("password")] = c.password;
    f[QStringLiteral("protocol")] = c.protocol;
    f[QStringLiteral("dns")] = c.dns;
    f[QStringLiteral("customSni")] = c.customSni;
    f[QStringLiteral("clientRandom")] = c.clientRandom;
    f[QStringLiteral("allowIpv6")] = c.allowIpv6;
    f[QStringLiteral("certificate")] = c.certificate;
    return f;
}

void Backend::setSplitEnabled(bool v) {
    if (m_settings.domain_bypass_enabled == v) return;
    m_settings.domain_bypass_enabled = v; persistSettings(); applySplitRules(); emit splitChanged();
}
// A bypass rule is valid if it's a domain (optionally wildcard "*.x.y"), or an
// IP / CIDR subnet.
static bool isValidBypassRule(const QString &rule) {
    QString r = rule;
    if (r.startsWith(QLatin1String("*.")))
        r = r.mid(2);
    // IP or subnet?
    const int slash = r.indexOf(QLatin1Char('/'));
    const QString addr = slash >= 0 ? r.left(slash) : r;
    if (!QHostAddress(addr).isNull()) {
        if (slash < 0) return true;
        bool ok = false; const int p = r.mid(slash + 1).toInt(&ok);
        const int max = addr.contains(QLatin1Char(':')) ? 128 : 32;
        return ok && p >= 0 && p <= max;
    }
    // Otherwise a hostname: labels of [A-Za-z0-9-], at least one dot.
    static const QRegularExpression re(
        QStringLiteral("^(?=.{1,253}$)([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\.)+"
                       "[A-Za-z]{2,63}$"));
    return re.match(r).hasMatch();
}

bool Backend::addDomain(const QString &domain) {
    // Accept a whole list pasted at once: split on commas / whitespace / newlines.
    const QStringList tokens = domain.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                            Qt::SkipEmptyParts);
    QStringList added, invalid;
    for (const QString &raw : tokens) {
        const QString d = raw.trimmed();
        if (d.isEmpty() || m_settings.domain_bypass_rules.contains(d) || added.contains(d))
            continue;
        if (!isValidBypassRule(d)) { invalid << d; continue; }
        added << d;
    }
    if (!invalid.isEmpty())
        emit errorOccurred(tr("Not a valid domain or subnet: %1").arg(invalid.join(QStringLiteral(", "))));
    if (added.isEmpty())
        return false;
    m_settings.domain_bypass_rules << added;
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); emit splitChanged();
    return true;
}
void Backend::removeDomain(int index) {
    if (index < 0 || index >= m_settings.domain_bypass_rules.size()) return;
    m_settings.domain_bypass_rules.removeAt(index);
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); emit splitChanged();
}
void Backend::clearDomains() {
    if (m_settings.domain_bypass_rules.isEmpty()) return;
    m_settings.domain_bypass_rules.clear();
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); emit splitChanged();
}

// ---------- excluded routes (subnets that bypass the tunnel) ----------

// Valid if it's an IP or CIDR subnet (IPv4/IPv6); the optional /prefix must be sane.
static bool isValidSubnet(const QString &r) {
    const int slash = r.indexOf(QLatin1Char('/'));
    const QString addr = slash >= 0 ? r.left(slash) : r;
    if (QHostAddress(addr).isNull())
        return false;
    if (slash < 0)
        return true;
    bool ok = false; const int p = r.mid(slash + 1).toInt(&ok);
    const int max = addr.contains(QLatin1Char(':')) ? 128 : 32;
    return ok && p >= 0 && p <= max;
}

bool Backend::addExcludedRoute(const QString &route) {
    // Accept a whole list pasted at once.
    const QStringList tokens = route.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                           Qt::SkipEmptyParts);
    QStringList added, invalid;
    for (const QString &raw : tokens) {
        const QString r = raw.trimmed();
        if (r.isEmpty() || m_settings.excluded_routes.contains(r) || added.contains(r))
            continue;
        if (!isValidSubnet(r)) { invalid << r; continue; }
        added << r;
    }
    if (!invalid.isEmpty())
        emit errorOccurred(tr("Enter a valid IP or subnet, e.g. 10.0.0.0/8"));
    if (added.isEmpty())
        return false;
    m_settings.excluded_routes << added;
    persistSettings(); applySplitRules(); emit splitChanged();
    return true;
}

void Backend::removeExcludedRoute(int index) {
    if (index < 0 || index >= m_settings.excluded_routes.size()) return;
    m_settings.excluded_routes.removeAt(index);
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::clearExcludedRoutes() {
    if (m_settings.excluded_routes.isEmpty()) return;
    m_settings.excluded_routes.clear();
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::restoreDefaultExcludedRoutes() {
    m_settings.excluded_routes = defaultExcludedRoutes();
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::addRecommendedRussia() {
    QStringList rules = m_settings.domain_bypass_rules;
    int added = 0;
    for (const QString &d : recommendedRussiaDomains()) {
        if (!rules.contains(d)) { rules << d; ++added; }
    }
    if (added == 0)
        return;
    m_settings.domain_bypass_rules = rules;
    m_settings.profiles[m_settings.active_profile] = rules;
    persistSettings(); applySplitRules(); emit splitChanged();
}

// ---------- split-tunnel profiles ----------

QStringList Backend::profiles() const {
    return m_settings.profile_order; // creation order, Default first
}

void Backend::selectProfile(const QString &name) {
    if (!m_settings.profiles.contains(name) || m_settings.active_profile == name) return;
    m_settings.active_profile = name;
    m_settings.domain_bypass_rules = m_settings.profiles.value(name);
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::addProfile(const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty() || m_settings.profiles.contains(n)) return;
    m_settings.profiles.insert(n, {});
    m_settings.profile_order << n;
    m_settings.active_profile = n;
    m_settings.domain_bypass_rules.clear();
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::removeProfile(const QString &name) {
    if (name == QLatin1String("Default") || !m_settings.profiles.contains(name)) return;
    m_settings.profiles.remove(name);
    m_settings.profile_order.removeAll(name);
    if (m_settings.active_profile == name) {
        m_settings.active_profile = QStringLiteral("Default");
        m_settings.domain_bypass_rules = m_settings.profiles.value(QStringLiteral("Default"));
    }
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::renameProfile(const QString &oldName, const QString &newName) {
    const QString n = newName.trimmed();
    if (oldName == QLatin1String("Default") || n.isEmpty()
            || !m_settings.profiles.contains(oldName) || m_settings.profiles.contains(n))
        return;
    m_settings.profiles.insert(n, m_settings.profiles.take(oldName));
    const int i = m_settings.profile_order.indexOf(oldName);
    if (i >= 0) m_settings.profile_order[i] = n;
    if (m_settings.active_profile == oldName)
        m_settings.active_profile = n;
    persistSettings(); emit splitChanged();
}

// Push the active profile's domain-bypass list to the core (C2). Applied on
// connect and whenever rules change; takes effect on the next (re)connect.
void Backend::applySplitRules() {
    const bool on = m_settings.domain_bypass_enabled;
    std::vector<std::string> ex;
    if (on)
        for (const QString &d : m_settings.domain_bypass_rules)
            ex.push_back(d.toStdString());
    m_client.setExtraExclusions(ex);
    // When split is off, force general mode (route everything) — otherwise a
    // leftover "selective" mode with no rules would route NOTHING through the VPN.
    m_client.setVpnMode(on && m_settings.vpn_mode == QLatin1String("selective"));
    // Excluded routes (subnets) are a separate, always-on routing rule.
    std::vector<std::string> routes;
    for (const QString &r : m_settings.excluded_routes)
        routes.push_back(r.toStdString());
    m_client.setExcludedRoutes(routes);
    reapplyIfConnected(); // make the change take effect on a live tunnel
}

// Routing/exclusion changes only bind when the tunnel is (re)built. If we're
// connected, seamlessly rebuild it so edits apply immediately rather than only
// after a manual reconnect. No-op (and no re-elevation) when disconnected.
void Backend::reapplyIfConnected() {
    if (!m_connected || m_reapplying || m_inConnect)
        return;
    m_reapplying = true; // connectVpn() below calls applySplitRules() again — don't recurse
    m_client.disconnectVpn();
    QTimer::singleShot(400, this, [this]() {
        if (!m_activePath.isEmpty())
            connectVpn();
        m_reapplying = false;
    });
}

void Backend::setVpnMode(const QString &mode) {
    if (m_settings.vpn_mode == mode) return;
    m_settings.vpn_mode = mode;
    persistSettings();
    applySplitRules();
    emit splitChanged();
}

void Backend::persistSettings() { saveAppSettings(m_settings); }

void Backend::setLanguage(const QString &v) {
    if (m_settings.language == v) return;
    m_settings.language = v; persistSettings(); emit settingsChanged(); emit languageChanged(v);
}
void Backend::setThemeMode(const QString &v) {
    if (m_settings.theme_mode == v) return;
    m_settings.theme_mode = v; persistSettings(); emit settingsChanged();
}
void Backend::setAutoConnect(bool v) {
    if (m_settings.auto_connect_on_start == v) return;
    m_settings.auto_connect_on_start = v; persistSettings(); emit settingsChanged();
}
void Backend::setKillSwitch(bool v) {
    if (m_settings.killswitch_enabled == v) return;
    m_settings.killswitch_enabled = v; persistSettings();
    m_client.setKillSwitch(v);
    reapplyIfConnected(); // apply on the live tunnel
    emit settingsChanged();
}

bool Backend::importDeepLink(const QString &link) {
    QString err;
    auto prepared = freetunnel::prepareDeepLinkImport(link, &err);
    if (!prepared) {
        emit errorOccurred(tr("Link error: %1").arg(err));
        return false;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(prepared->fileName);
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    f.write(prepared->tomlContent.toUtf8());
    f.close();
    // Imported config carries credentials in plaintext — owner-only (see createConfig).
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored << target;
        saveStoredConfigs(stored);
    }
    const bool hadNoActive = m_activePath.isEmpty();
    reloadConfigs();
    if (hadNoActive) { // first config in an empty list — make it the active one
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
        emit configChanged();
    }
    return true;
}
