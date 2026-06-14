#include "app/Backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>
#include <QVariantMap>

#include "core/AppSettings.h"
#include "core/AppUiUtils.h"
#include "core/ConfigImport.h"
#include "core/ConfigStore.h"

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
    QString c = command.trimmed();
    if (c.isEmpty() || c == QLatin1String("focus"))
        return; // window raise handled by the caller
    if (c.startsWith(QLatin1String("tt://"))) {
        importDeepLink(c);
        return;
    }
    if (c.startsWith(QLatin1String("freetunnel://")))
        c = c.mid(QStringLiteral("freetunnel://").size());
    c = c.remove('/').toLower();
    if (c == QLatin1String("toggle"))
        toggle();
    else if (c == QLatin1String("connect"))
        connectVpn();
    else if (c == QLatin1String("disconnect"))
        disconnectVpn();
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
