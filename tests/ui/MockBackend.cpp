// cppcheck-suppress-file missingIncludeSystem
#include "ui/MockBackend.h"

#include <QFile>
#include <QStandardPaths>

MockBackend::MockBackend(QObject *parent) : QObject(parent)
{
    m_logModel.append(QStringLiteral("12:00:00"), QStringLiteral("INFO"),
                      QStringLiteral("Mock backend ready"));
}

// Mirrors Backend::readBundledText (qrc-only) so Icon.qml renders in UI tests.
QString MockBackend::readBundledText(const QUrl &url) const
{
    QString path;
    if (url.scheme() == QLatin1String("qrc"))
        path = QLatin1Char(':') + url.path();
    else if (url.toString().startsWith(QLatin1String(":/")))
        path = url.toString();
    else
        return QString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
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

void MockBackend::setHotkeyToggle(const QString &v)
{
    if (m_hotkeyToggle == v)
        return;
    m_hotkeyToggle = v;
    emit hotkeysChanged();
}

void MockBackend::setHotkeyConnect(const QString &v)
{
    if (m_hotkeyConnect == v)
        return;
    m_hotkeyConnect = v;
    emit hotkeysChanged();
}

void MockBackend::setHotkeyDisconnect(const QString &v)
{
    if (m_hotkeyDisconnect == v)
        return;
    m_hotkeyDisconnect = v;
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

// Both list mutations keep the active slot in step and announce it, like
// Backend does. Leaving activeIndex/activeConfig stale made the "connected"
// badge and the selection highlight point at the wrong row in the test scene —
// exactly the desync these UI tests exist to catch.
void MockBackend::removeConfig(int index)
{
    if (index < 0 || index >= m_configs.size())
        return;
    m_configs.removeAt(index);
    if (m_configs.isEmpty()) {
        m_activeIndex = -1;
        m_activeConfig.clear();
    } else if (index < m_activeIndex) {
        --m_activeIndex; // rows below shifted up
    } else if (index == m_activeIndex) {
        m_activeIndex = qMin(m_activeIndex, m_configs.size() - 1);
        m_activeConfig = m_configs.at(m_activeIndex);
    }
    emit configsChanged();
    emit configChanged();
}

void MockBackend::moveConfig(int from, int to)
{
    if (from < 0 || from >= m_configs.size() || to < 0 || to >= m_configs.size() || from == to)
        return;
    m_configs.move(from, to);
    // The active config did not change, only where it sits.
    m_activeIndex = m_configs.indexOf(m_activeConfig);
    emit configsChanged();
    emit configChanged();
}

// Deep links model the REAL contract: nothing is imported until the user
// confirms, so this asks and returns false. Modelling it as an immediate
// success made the mandatory-confirmation path unreachable in UI tests — the
// dialog Main.qml wires up would never be exercised.
bool MockBackend::importDeepLink(const QString &link)
{
    // Model both shapes: a link naming an existing config offers "replace".
    const QString existing = m_configs.contains(QStringLiteral("Imported"))
            ? QStringLiteral("Imported")
            : QString();
    emit deepLinkImportConfirmationRequired(QStringLiteral("Add a VPN server from this link?"),
                                            link, existing);
    return false;
}

bool MockBackend::confirmDeepLinkImport(const QString &, bool)
{
    emit configImported(QStringLiteral("Imported"));
    return true;
}

bool MockBackend::importFile(const QString &)
{
    emit configImported(QStringLiteral("Imported"));
    return true;
}
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
    return f;
}
