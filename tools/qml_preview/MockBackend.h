#pragma once
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Stand-in for the real Backend so the QML UI can be built/previewed without the
// C++ core. Mirrors the Backend property/method surface the QML binds to.
class MockBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString sessionTime READ sessionTime NOTIFY changed)
    Q_PROPERTY(QString downSpeed READ downSpeed NOTIFY changed)
    Q_PROPERTY(QString upSpeed READ upSpeed NOTIFY changed)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY changed)
    Q_PROPERTY(QStringList configs READ configs NOTIFY changed)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY changed)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY changed)
    Q_PROPERTY(bool killSwitch READ killSwitch WRITE setKillSwitch NOTIFY changed)
    Q_PROPERTY(QVariantList logEntries READ logEntries NOTIFY changed)
    Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY changed)
    Q_PROPERTY(QStringList domains READ domains NOTIFY changed)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY changed)
    Q_PROPERTY(QString hotkeyToggle READ hotkeyToggle WRITE setHotkeyToggle NOTIFY changed)
    Q_PROPERTY(QString hotkeyConnect READ hotkeyConnect WRITE setHotkeyConnect NOTIFY changed)
    Q_PROPERTY(QString hotkeyDisconnect READ hotkeyDisconnect WRITE setHotkeyDisconnect NOTIFY changed)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString updateState READ updateState NOTIFY changed)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QVariantList adapters READ adapters NOTIFY changed)
    Q_PROPERTY(bool adapterScanSupported READ adapterScanSupported CONSTANT)
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY changed)
    Q_PROPERTY(QVariantList pings READ pings NOTIFY changed)
public:
    QString appVersion() const { return QStringLiteral("1.0.0"); }
    QString updateState() const { return m_updateState; }
    QString updateMessage() const { return m_updateMessage; }
    QString latestVersion() const { return m_latestVersion; }
    Q_INVOKABLE void checkForUpdates() {
        m_updateState = QStringLiteral("available"); m_latestVersion = QStringLiteral("1.1.0");
        m_updateMessage = QStringLiteral("Доступна версия 1.1.0"); emit changed();
    }
    Q_INVOKABLE void openLatestRelease() {}
    QVariantList adapters() const {
        auto a = [](const char *n, const char *d, bool ours, bool on, bool conflict) {
            QVariantMap v; v["name"] = n; v["description"] = d; v["ours"] = ours;
            v["enabled"] = on; v["conflict"] = conflict; return QVariant(v);
        };
        return QVariantList{
            a("FreeTunnel WinTUN", "Wintun Userspace Tunnel", true, true, false),
            a("Radmin VPN Network Adapter", "Radmin VPN", false, true, true),
            a("TAP-Windows Adapter V9", "TAP-Windows Provider V9", false, true, true),
            a("OpenVPN Wintun", "Wintun Userspace Tunnel", false, false, false)};
    }
    bool adapterScanSupported() const { return true; }
    Q_INVOKABLE void scanAdapters() { emit changed(); }
    Q_INVOKABLE void setAdapterEnabled(const QString &, bool) { emit changed(); }
    QString logPath() const { return QStringLiteral("/Users/me/Library/Application Support/FreeTunnel/freetunnel.log"); }
    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool v) { m_autoStart = v; emit changed(); }
    QVariantList pings() const { return m_pings; }
    Q_INVOKABLE void pingConfigs() {
        m_pings = QVariantList{QStringLiteral("38 мс"), QStringLiteral("52 мс"), QStringLiteral("—")};
        emit changed();
    }
    Q_INVOKABLE bool importFromClipboard() { return true; }
    QString hotkeyToggle() const { return m_hkToggle; }
    QString hotkeyConnect() const { return m_hkConnect; }
    QString hotkeyDisconnect() const { return m_hkDisconnect; }
    void setHotkeyToggle(const QString &v) { m_hkToggle = v; emit changed(); }
    void setHotkeyConnect(const QString &v) { m_hkConnect = v; emit changed(); }
    void setHotkeyDisconnect(const QString &v) { m_hkDisconnect = v; emit changed(); }
    bool splitEnabled() const { return m_split; }
    void setSplitEnabled(bool v) { m_split = v; emit changed(); }
    QStringList domains() const { return m_profiles.value(m_activeProfile); }
    Q_INVOKABLE void addDomain(const QString &d) { if (!d.trimmed().isEmpty()) m_profiles[m_activeProfile] << d.trimmed(); emit changed(); }
    Q_INVOKABLE void removeDomain(int i) { auto &l = m_profiles[m_activeProfile]; if (i >= 0 && i < l.size()) l.removeAt(i); emit changed(); }
    Q_INVOKABLE void clearDomains() { m_profiles[m_activeProfile].clear(); emit changed(); }
    QStringList profiles() const {
        QStringList n = m_profiles.keys(); n.removeAll("Default"); n.sort(); n.prepend("Default"); return n;
    }
    QString activeProfile() const { return m_activeProfile; }
    Q_INVOKABLE void selectProfile(const QString &n) { if (m_profiles.contains(n)) m_activeProfile = n; emit changed(); }
    Q_INVOKABLE void addProfile(const QString &n) { if (!n.trimmed().isEmpty() && !m_profiles.contains(n)) { m_profiles.insert(n, {}); m_activeProfile = n; } emit changed(); }
    Q_INVOKABLE void removeProfile(const QString &n) { if (n != "Default") { m_profiles.remove(n); if (m_activeProfile == n) m_activeProfile = "Default"; } emit changed(); }
    Q_INVOKABLE void renameProfile(const QString &o, const QString &nn) { if (o != "Default" && !nn.trimmed().isEmpty() && m_profiles.contains(o) && !m_profiles.contains(nn)) { m_profiles.insert(nn, m_profiles.take(o)); if (m_activeProfile == o) m_activeProfile = nn; } emit changed(); }
    QVariantList logEntries() const {
        auto e = [](const char *t, const char *l, const char *m) {
            QVariantMap v; v["time"] = t; v["level"] = l; v["msg"] = m; return QVariant(v);
        };
        return m_logCleared ? QVariantList{}
                            : QVariantList{
                                  e("12:04:32", "INFO", "connecting frankfurt.example.com:443 (HTTP/2)"),
                                  e("12:04:33", "WARN", "DNS upstream 1.1.1.1 slow, retrying"),
                                  e("12:04:34", "INFO", "tunnel established (utun5)"),
                                  e("12:06:11", "ERROR", "connection reset, reconnecting (1)"),
                                  e("12:06:13", "INFO", "tunnel re-established") };
    }
    Q_INVOKABLE void clearLogs() { m_logCleared = true; emit changed(); }
    Q_INVOKABLE void openLogFolder() {}
    bool connected() const { return m_on; }
    QString sessionTime() const { return QStringLiteral("01:24:36"); }
    QString downSpeed() const { return QStringLiteral("12.4"); }
    QString upSpeed() const { return QStringLiteral("1.2"); }
    QStringList configs() const { return m_configs; }
    int activeIndex() const { return m_active; }
    QString activeConfig() const {
        return m_active >= 0 && m_active < m_configs.size() ? m_configs.at(m_active)
                                                            : QStringLiteral("Нет конфига");
    }
    QString language() const { return m_lang; }
    QString themeMode() const { return m_theme; }
    bool autoConnect() const { return m_autoConnect; }
    bool killSwitch() const { return m_kill; }
    void setLanguage(const QString &v) { m_lang = v; emit changed(); }
    void setThemeMode(const QString &v) { m_theme = v; emit changed(); }
    void setAutoConnect(bool v) { m_autoConnect = v; emit changed(); }
    void setKillSwitch(bool v) { m_kill = v; emit changed(); }

    Q_INVOKABLE void toggle() { m_on = !m_on; emit changed(); }
    Q_INVOKABLE void selectConfig(int i) { m_active = i; emit changed(); }
    Q_INVOKABLE void removeConfig(int i) {
        if (i >= 0 && i < m_configs.size()) { m_configs.removeAt(i); if (m_active >= m_configs.size()) m_active = m_configs.size() - 1; }
        emit changed();
    }
    Q_INVOKABLE bool importDeepLink(const QString &) { return true; }
    Q_INVOKABLE bool importFile(const QString &) { return true; }
    Q_INVOKABLE bool createConfig(const QVariantMap &) { return true; }
signals:
    void changed();
private:
    bool m_on = true;
    QStringList m_configs{QStringLiteral("Германия · Франкфурт"),
                          QStringLiteral("Нидерланды · Амстердам"),
                          QStringLiteral("США · Нью-Йорк")};
    int m_active = 0;
    QString m_lang = QStringLiteral("en");
    QString m_theme = QStringLiteral("system");
    bool m_autoConnect = false;
    bool m_kill = true;
    bool m_logCleared = false;
    bool m_split = true;
    QString m_activeProfile = QStringLiteral("Default");
    QMap<QString, QStringList> m_profiles{
        {QStringLiteral("Default"), {QStringLiteral("github.com"), QStringLiteral("*.gov.ru"),
                                     QStringLiteral("netflix.com")}},
        {QStringLiteral("Работа"), {QStringLiteral("intranet.corp")}}};
    QString m_hkToggle = QStringLiteral("Ctrl+Alt+T");
    QString m_hkConnect;
    QString m_hkDisconnect;
    QString m_updateState;
    QString m_updateMessage;
    QString m_latestVersion;
    bool m_autoStart = false;
    QVariantList m_pings;
};
