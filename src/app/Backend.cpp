#include "app/Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

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
            });
    connect(&m_client, &QtTrustTunnelClient::tunnelStats, this,
            [this](quint64 up, quint64 down) {
                m_accUp += up;
                m_accDown += down;
            });
    connect(&m_client, &QtTrustTunnelClient::vpnError, this,
            [this](const QString &m) { emit errorOccurred(m); });

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
