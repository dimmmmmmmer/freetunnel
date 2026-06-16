#include "app/Backend.h"

#include <QAbstractSocket>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
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
        appendLog(QStringLiteral("ERROR"), m); // always logged
        // Throttle toasts: a flaky connection can spit the same error every few
        // seconds — show it once, then stay quiet for a while unless it changes.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m != m_lastErrorMsg || now - m_lastErrorAt > 30000) {
            m_lastErrorMsg = m;
            m_lastErrorAt = now;
            emit errorOccurred(m);
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
    if (m_connected)
        disconnectVpn();
    else
        connectVpn();
}

void Backend::connectVpn() {
    if (m_activePath.isEmpty()) {
        emit errorOccurred(tr("Select a config first"));
        return;
    }
    // The helper handles elevation; just hand it the config + rules.
    m_client.loadConfigFromFile(m_activePath);
    applySplitRules(); // push domain-bypass rules to the core before connecting
    m_client.setKillSwitch(m_settings.killswitch_enabled);
    m_client.connectVpn();
}

void Backend::disconnectVpn() { m_client.disconnectVpn(); }

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
    m_activePath = m_paths.at(index);
    m_settings.last_config_path = m_activePath;
    persistSettings();
    emit configChanged();
    if (m_connected) { // switch live
        m_client.loadConfigFromFile(m_activePath);
        m_client.connectVpn();
    }
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
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(p)) {
        stored << p;
        saveStoredConfigs(stored);
    }
    reloadConfigs();
    return true;
}

void Backend::appendLog(const QString &level, const QString &msg) {
    QVariantMap e;
    e[QStringLiteral("time")] = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    e[QStringLiteral("level")] = level;
    e[QStringLiteral("msg")] = msg;
    m_log.append(e);
    if (m_log.size() > 500)
        m_log.removeFirst();
    emit logChanged();
}

void Backend::clearLogs() {
    m_log.clear();
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

    QStringList stored = loadStoredConfigs();
    if (!oldPath.isEmpty() && oldPath != target) { // edit + rename
        QFile::remove(oldPath);
        stored.removeAll(oldPath);
        if (m_activePath == oldPath) m_activePath = target;
    }
    if (!stored.contains(target))
        stored << target;
    saveStoredConfigs(stored);
    reloadConfigs();
    m_activePath = target;
    m_settings.last_config_path = target;
    persistSettings();
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
void Backend::addDomain(const QString &domain) {
    const QString d = domain.trimmed();
    if (d.isEmpty() || m_settings.domain_bypass_rules.contains(d)) return;
    m_settings.domain_bypass_rules << d;
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); emit splitChanged();
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
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored << target;
        saveStoredConfigs(stored);
    }
    reloadConfigs();
    return true;
}
