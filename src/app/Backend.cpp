#include "app/Backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QKeySequence>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>
#include <QVariantMap>

#include <QHotkey>

#include "core/AppSettings.h"
#include "core/AppUiUtils.h"
#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ControlCommand.h"
#include "core/NetworkAdapterManager.h"
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

    connect(&m_client, &QtTrustTunnelClient::stateChanged, this,
            [this](QtTrustTunnelClient::State st) {
                const bool nowConnected = st == QtTrustTunnelClient::State::Connected;
                if (nowConnected && !m_connected) {
                    m_session.restart();
                }
                m_connected = nowConnected;
                emit stateChanged();
                appendLog(QStringLiteral("INFO"), statusText());
            });
    connect(&m_client, &QtTrustTunnelClient::tunnelStats, this,
            [this](quint64 up, quint64 down) {
                m_accUp += up;
                m_accDown += down;
            });
    connect(&m_client, &QtTrustTunnelClient::connectionInfo, this,
            [this](const QString &m) { appendLog(QStringLiteral("INFO"), m); });
    connect(&m_client, &QtTrustTunnelClient::vpnError, this, [this](const QString &m) {
        appendLog(QStringLiteral("ERROR"), m);
        emit errorOccurred(m);
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
                    m_updateMessage = tr("Доступна версия %1").arg(info.version);
                    emit updateChanged();
                });
        connect(m_updater, &UpdateChecker::noUpdateAvailable, this,
                [this](const QString &message) {
                    m_updateState = QStringLiteral("current");
                    m_updateMessage = message.isEmpty()
                            ? tr("Установлена последняя версия") : message;
                    emit updateChanged();
                });
    }
    m_updateState = QStringLiteral("checking");
    m_updateMessage = tr("Проверка…");
    emit updateChanged();
    m_updater->checkNow();
}

void Backend::openLatestRelease() {
    const QString url = m_latestUrl.isEmpty()
            ? QStringLiteral("https://github.com/enrvate/freetunnel/releases/latest")
            : m_latestUrl;
    QDesktopServices::openUrl(QUrl(url));
}

// ---------- network adapters ----------

bool Backend::adapterScanSupported() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void Backend::scanAdapters() {
    if (!m_adapterMgr)
        m_adapterMgr = new NetworkAdapterManager(this);
    m_adapters.clear();
    const auto found = m_adapterMgr->scanAdapters();
    const QStringList conflicts = NetworkAdapterManager::knownConflictPatterns();
    for (const AdapterInfo &a : found) {
        bool conflict = false;
        for (const QString &p : conflicts)
            if (a.name.contains(p, Qt::CaseInsensitive)
                || a.description.contains(p, Qt::CaseInsensitive)) { conflict = true; break; }
        QVariantMap m;
        m[QStringLiteral("name")] = a.name;
        m[QStringLiteral("description")] = a.description;
        m[QStringLiteral("ours")] = a.isOurs;
        m[QStringLiteral("enabled")] = a.enabled;
        m[QStringLiteral("conflict")] = conflict && !a.isOurs;
        m_adapters << m;
    }
    emit adaptersChanged();
}

void Backend::setAdapterEnabled(const QString &name, bool enabled) {
    if (!m_adapterMgr)
        m_adapterMgr = new NetworkAdapterManager(this);
    const bool ok = enabled ? m_adapterMgr->enableAdapter(name)
                            : m_adapterMgr->disableAdapter(name);
    if (ok)
        scanAdapters();
    else
        emit errorOccurred(tr("Не удалось изменить адаптер: %1").arg(name));
}

QString Backend::statusText() const {
    switch (m_client.state()) {
    case QtTrustTunnelClient::State::Connected: return tr("Подключено");
    case QtTrustTunnelClient::State::Connecting: return tr("Подключение…");
    case QtTrustTunnelClient::State::Reconnecting: return tr("Переподключение…");
    case QtTrustTunnelClient::State::WaitingForNetwork: return tr("Ожидание сети…");
    case QtTrustTunnelClient::State::Disconnecting: return tr("Отключение…");
    case QtTrustTunnelClient::State::Error: return tr("Ошибка");
    default: return tr("Выключено");
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
    return m_activePath.isEmpty() ? tr("Нет конфига") : nameForPath(m_activePath);
}

QString Backend::nameForPath(const QString &path) const {
    return QFileInfo(path).completeBaseName();
}

void Backend::reloadConfigs() {
    m_paths = loadStoredConfigs();
    m_names.clear();
    for (const QString &p : m_paths)
        m_names << nameForPath(p);
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
        emit errorOccurred(tr("Сначала выберите конфиг"));
        return;
    }
    if (!ensureElevated())
        return; // app is relaunching with privileges
    if (!m_client.loadConfigFromFile(m_activePath)) {
        emit errorOccurred(tr("Не удалось загрузить конфиг: %1").arg(m_activePath));
        return;
    }
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
        emit errorOccurred(tr("Файл не найден: %1").arg(p));
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

static QString tomlEsc(const QString &s) {
    QString o = s;
    o.replace('\\', "\\\\").replace('"', "\\\"");
    return o;
}
static QString csvToTomlArray(const QString &csv) {
    QStringList items;
    for (const QString &raw : csv.split(',', Qt::SkipEmptyParts)) {
        const QString v = raw.trimmed();
        if (!v.isEmpty())
            items << QStringLiteral("\"%1\"").arg(tomlEsc(v));
    }
    return items.join(QStringLiteral(", "));
}

bool Backend::createConfig(const QVariantMap &f) {
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    const QString hostname = f.value(QStringLiteral("hostname")).toString().trimmed();
    const QString addresses = f.value(QStringLiteral("addresses")).toString().trimmed();
    const QString username = f.value(QStringLiteral("username")).toString().trimmed();
    const QString password = f.value(QStringLiteral("password")).toString();
    if (hostname.isEmpty() || addresses.isEmpty() || username.isEmpty() || password.isEmpty()) {
        emit errorOccurred(tr("Заполните имя хоста, адрес, логин и пароль"));
        return false;
    }
    const QString proto = f.value(QStringLiteral("protocol"), QStringLiteral("http2")).toString();
    const bool ipv6 = f.value(QStringLiteral("allowIpv6"), true).toBool();
    const QString cert = f.value(QStringLiteral("certificate")).toString().trimmed();

    QString t;
    t += QStringLiteral("loglevel = \"info\"\n");
    t += QStringLiteral("vpn_mode = \"general\"\n");
    t += QStringLiteral("killswitch_enabled = false\n");
    t += QStringLiteral("post_quantum_group_enabled = true\n");
    t += QStringLiteral("dns_upstreams = [%1]\n")
                 .arg(csvToTomlArray(f.value(QStringLiteral("dns")).toString()));
    t += QStringLiteral("\n[endpoint]\n");
    t += QStringLiteral("hostname = \"%1\"\n").arg(tomlEsc(hostname));
    t += QStringLiteral("addresses = [%1]\n").arg(csvToTomlArray(addresses));
    t += QStringLiteral("username = \"%1\"\n").arg(tomlEsc(username));
    t += QStringLiteral("password = \"%1\"\n").arg(tomlEsc(password));
    t += QStringLiteral("client_random = \"%1\"\n")
                 .arg(tomlEsc(f.value(QStringLiteral("clientRandom")).toString()));
    t += QStringLiteral("custom_sni = \"%1\"\n")
                 .arg(tomlEsc(f.value(QStringLiteral("customSni")).toString()));
    t += QStringLiteral("has_ipv6 = %1\n").arg(ipv6 ? "true" : "false");
    t += QStringLiteral("skip_verification = false\n");
    t += QStringLiteral("upstream_protocol = \"%1\"\n").arg(proto == "http3" ? "http3" : "http2");
    t += QStringLiteral("anti_dpi = false\n");
    if (!cert.isEmpty())
        t += QStringLiteral("certificate = \"\"\"\n%1\n\"\"\"\n").arg(cert);
    else
        t += QStringLiteral("certificate = \"\"\n");
    t += QStringLiteral("\n[listener.tun]\n");
    t += QStringLiteral("bound_if = \"\"\nmtu_size = 1500\nchange_system_dns = true\n");
    t += QStringLiteral("included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n");
    t += QStringLiteral("excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", "
                        "\"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n");

    QString safe;
    const QString src = name.isEmpty() ? hostname : name;
    for (const QChar &c : src)
        safe += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('_');
    if (safe.isEmpty())
        safe = QStringLiteral("config-%1").arg(QDateTime::currentSecsSinceEpoch());

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(safe + QStringLiteral(".toml"));
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Не удалось записать конфиг"));
        return false;
    }
    file.write(t.toUtf8());
    file.close();

    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored << target;
        saveStoredConfigs(stored);
    }
    reloadConfigs();
    m_activePath = target;
    m_settings.last_config_path = target;
    persistSettings();
    emit configChanged();
    return true;
}

void Backend::setSplitEnabled(bool v) {
    if (m_settings.domain_bypass_enabled == v) return;
    m_settings.domain_bypass_enabled = v; persistSettings(); emit splitChanged();
}
void Backend::addDomain(const QString &domain) {
    const QString d = domain.trimmed();
    if (d.isEmpty() || m_settings.domain_bypass_rules.contains(d)) return;
    m_settings.domain_bypass_rules << d; persistSettings(); emit splitChanged();
}
void Backend::removeDomain(int index) {
    if (index < 0 || index >= m_settings.domain_bypass_rules.size()) return;
    m_settings.domain_bypass_rules.removeAt(index); persistSettings(); emit splitChanged();
}
void Backend::clearDomains() {
    if (m_settings.domain_bypass_rules.isEmpty()) return;
    m_settings.domain_bypass_rules.clear(); persistSettings(); emit splitChanged();
}

void Backend::persistSettings() { saveAppSettings(m_settings); }

void Backend::setLanguage(const QString &v) {
    if (m_settings.language == v) return;
    m_settings.language = v; persistSettings(); emit settingsChanged();
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
    m_settings.killswitch_enabled = v; persistSettings(); emit settingsChanged();
}

bool Backend::importDeepLink(const QString &link) {
    QString err;
    auto prepared = freetunnel::prepareDeepLinkImport(link, &err);
    if (!prepared) {
        emit errorOccurred(tr("Ошибка ссылки: %1").arg(err));
        return false;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(prepared->fileName);
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Не удалось записать конфиг"));
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

bool Backend::ensureElevated() {
#ifdef _WIN32
    return true; // the exe requests admin via its UAC manifest at launch
#else
    if (geteuid() == 0)
        return true;
    const QString exe = QCoreApplication::applicationFilePath();
    const QString cmd =
            QStringLiteral("exec %1 >/tmp/freetunnel.relaunch.log 2>&1 &").arg(shellEscape(exe));
    QString err;
    if (!runElevatedShell(cmd, &err)) {
        emit errorOccurred(tr("Не удалось получить права администратора.\n%1").arg(err));
        return true; // don't block; let the connect attempt surface the real error
    }
    QCoreApplication::quit();
    return false;
#endif
}
