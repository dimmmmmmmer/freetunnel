#include "ui/MockBackend.h"

#include <QStandardPaths>

MockBackend::MockBackend(QObject *parent) : QObject(parent)
{
    QVariantMap logLine;
    logLine[QStringLiteral("time")] = QStringLiteral("12:00:00");
    logLine[QStringLiteral("level")] = QStringLiteral("INFO");
    logLine[QStringLiteral("msg")] = QStringLiteral("Mock backend ready");
    m_logEntries.append(logLine);
}

void MockBackend::setConnected(bool v)
{
    if (m_connected == v)
        return;
    m_connected = v;
    emit stateChanged();
}

void MockBackend::setConnecting(bool v)
{
    if (m_connecting == v)
        return;
    m_connecting = v;
    emit stateChanged();
}

void MockBackend::setDisconnecting(bool v)
{
    if (m_disconnecting == v)
        return;
    m_disconnecting = v;
    emit stateChanged();
}

QString MockBackend::statusText() const
{
    if (m_disconnecting)
        return QStringLiteral("Disconnecting…");
    if (m_connecting)
        return QStringLiteral("Connecting…");
    if (m_connected)
        return QStringLiteral("Connected");
    return QStringLiteral("Off");
}

void MockBackend::setLanguage(const QString &v)
{
    if (m_language == v)
        return;
    m_language = v;
    emit settingsChanged();
    emit languageChanged(v);
}

void MockBackend::setThemeMode(const QString &v)
{
    if (m_themeMode == v)
        return;
    m_themeMode = v;
    emit settingsChanged();
}

void MockBackend::setAutoConnect(bool v)
{
    if (m_autoConnect == v)
        return;
    m_autoConnect = v;
    emit settingsChanged();
}

void MockBackend::setKillSwitch(bool v)
{
    if (m_killSwitch == v)
        return;
    m_killSwitch = v;
    emit settingsChanged();
}

void MockBackend::setSplitEnabled(bool v)
{
    if (m_splitEnabled == v)
        return;
    m_splitEnabled = v;
    emit splitChanged();
}

void MockBackend::setVpnMode(const QString &v)
{
    if (m_vpnMode == v)
        return;
    m_vpnMode = v;
    emit splitChanged();
}

void MockBackend::setHotkeysEnabled(bool v)
{
    if (m_hotkeysEnabled == v)
        return;
    m_hotkeysEnabled = v;
    emit hotkeysChanged();
}

QString MockBackend::logPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/freetunnel-mock.log");
}

void MockBackend::setAutoStart(bool v)
{
    if (m_autoStart == v)
        return;
    m_autoStart = v;
    emit settingsChanged();
}

void MockBackend::toggle()
{
    ++m_toggleCount;
    if (m_connected || m_connecting)
        disconnectVpn();
    else
        connectVpn();
}

void MockBackend::selectConfig(int index)
{
    if (index < 0 || index >= m_configs.size())
        return;
    m_activeIndex = index;
    m_activeConfig = m_configs.at(index);
    emit configChanged();
}

void MockBackend::removeConfig(int index)
{
    if (index < 0 || index >= m_configs.size())
        return;
    m_configs.removeAt(index);
    emit configsChanged();
}

bool MockBackend::importDeepLink(const QString &) { return true; }
bool MockBackend::importFile(const QString &) { return true; }
bool MockBackend::createConfig(const QVariantMap &) { return true; }

QVariantMap MockBackend::configFields(int index) const
{
    QVariantMap f;
    if (index < 0 || index >= m_configs.size())
        return f;
    f[QStringLiteral("name")] = m_configs.at(index);
    f[QStringLiteral("hostname")] = QStringLiteral("vpn.example.com");
    f[QStringLiteral("addresses")] = QStringLiteral("1.2.3.4:443");
    f[QStringLiteral("username")] = QStringLiteral("user");
    f[QStringLiteral("password")] = QStringLiteral("pass");
    f[QStringLiteral("protocol")] = QStringLiteral("http2");
    f[QStringLiteral("allowIpv6")] = true;
    f[QStringLiteral("splitProfile")] = QStringLiteral("Default");
    f[QStringLiteral("socks5")] = false;
    f[QStringLiteral("socksListen")] = QStringLiteral("127.0.0.1:1080");
    f[QStringLiteral("socksUser")] = QString();
    f[QStringLiteral("socksPass")] = QString();
    return f;
}
