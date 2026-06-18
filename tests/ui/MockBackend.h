#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Lightweight Backend stand-in for headless QML/UI tests (no VPN core).
class MockBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY stateChanged)
    Q_PROPERTY(bool connecting READ connecting WRITE setConnecting NOTIFY stateChanged)
    Q_PROPERTY(bool disconnecting READ disconnecting WRITE setDisconnecting NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText CONSTANT)
    Q_PROPERTY(QString sessionTime READ sessionTime CONSTANT)
    Q_PROPERTY(QString downSpeed READ downSpeed CONSTANT)
    Q_PROPERTY(QString upSpeed READ upSpeed CONSTANT)
    Q_PROPERTY(QString activeConfig READ activeConfig CONSTANT)
    Q_PROPERTY(QStringList configs READ configs CONSTANT)
    Q_PROPERTY(int activeIndex READ activeIndex CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool killSwitch READ killSwitch WRITE setKillSwitch NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList logEntries READ logEntries CONSTANT)
    Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY splitChanged)
    Q_PROPERTY(QString vpnMode READ vpnMode WRITE setVpnMode NOTIFY splitChanged)
    Q_PROPERTY(QStringList domains READ domains CONSTANT)
    Q_PROPERTY(QStringList excludedRoutes READ excludedRoutes CONSTANT)
    Q_PROPERTY(QStringList profiles READ profiles CONSTANT)
    Q_PROPERTY(QString activeProfile READ activeProfile CONSTANT)
    Q_PROPERTY(bool hotkeysEnabled READ hotkeysEnabled WRITE setHotkeysEnabled NOTIFY hotkeysChanged)
    Q_PROPERTY(QString hotkeyToggle READ hotkeyToggle CONSTANT)
    Q_PROPERTY(QString hotkeyConnect READ hotkeyConnect CONSTANT)
    Q_PROPERTY(QString hotkeyDisconnect READ hotkeyDisconnect CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(QString updateState READ updateState CONSTANT)
    Q_PROPERTY(QString updateMessage READ updateMessage CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion CONSTANT)
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList pings READ pings CONSTANT)

public:
    explicit MockBackend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    void setConnected(bool v);
    bool connecting() const { return m_connecting; }
    void setConnecting(bool v);
    bool disconnecting() const { return m_disconnecting; }
    void setDisconnecting(bool v);
    QString statusText() const;
    QString sessionTime() const { return m_sessionTime; }
    QString downSpeed() const { return m_downSpeed; }
    QString upSpeed() const { return m_upSpeed; }
    QString activeConfig() const { return m_activeConfig; }
    QStringList configs() const { return m_configs; }
    int activeIndex() const { return m_activeIndex; }

    QString language() const { return m_language; }
    void setLanguage(const QString &v);
    QString themeMode() const { return m_themeMode; }
    void setThemeMode(const QString &v);
    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool v);
    bool killSwitch() const { return m_killSwitch; }
    void setKillSwitch(bool v);

    QVariantList logEntries() const { return m_logEntries; }
    bool splitEnabled() const { return m_splitEnabled; }
    void setSplitEnabled(bool v);
    QString vpnMode() const { return m_vpnMode; }
    void setVpnMode(const QString &v);
    QStringList domains() const { return m_domains; }
    QStringList excludedRoutes() const { return m_excludedRoutes; }
    QStringList profiles() const { return m_profiles; }
    QString activeProfile() const { return m_activeProfile; }

    bool hotkeysEnabled() const { return m_hotkeysEnabled; }
    void setHotkeysEnabled(bool v);
    QString hotkeyToggle() const { return QStringLiteral("Ctrl+Alt+T"); }
    QString hotkeyConnect() const { return QString(); }
    QString hotkeyDisconnect() const { return QString(); }

    QString appVersion() const { return QStringLiteral("1.0.0-test"); }
    QString coreVersion() const { return QStringLiteral("test-core"); }
    QString updateState() const { return QString(); }
    QString updateMessage() const { return QString(); }
    QString latestVersion() const { return QString(); }
    QString logPath() const;
    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool v);
    QVariantList pings() const { return m_pings; }

    int toggleCount() const { return m_toggleCount; }

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void connectVpn() { m_connecting = true; emit stateChanged(); }
    Q_INVOKABLE void disconnectVpn() { m_connected = false; emit stateChanged(); }
    Q_INVOKABLE void selectConfig(int index);
    Q_INVOKABLE void removeConfig(int index);
    Q_INVOKABLE bool importDeepLink(const QString &link);
    Q_INVOKABLE bool importFile(const QString &path);
    Q_INVOKABLE bool createConfig(const QVariantMap &fields);
    Q_INVOKABLE QVariantMap configFields(int index) const;
    Q_INVOKABLE void clearLogs() {}
    Q_INVOKABLE void openLogFolder() {}
    Q_INVOKABLE QString logText() const { return QString(); }
    Q_INVOKABLE void copyToClipboard(const QString &) const {}
    Q_INVOKABLE QString readTextFile(const QString &) const { return QString(); }
    Q_INVOKABLE bool addDomain(const QString &) { return false; }
    Q_INVOKABLE void removeDomain(int) {}
    Q_INVOKABLE void clearDomains() {}
    Q_INVOKABLE bool addExcludedRoute(const QString &) { return false; }
    Q_INVOKABLE void removeExcludedRoute(int) {}
    Q_INVOKABLE void clearExcludedRoutes() {}
    Q_INVOKABLE void restoreDefaultExcludedRoutes() {}
    Q_INVOKABLE void addRecommendedRussia() {}
    Q_INVOKABLE void selectProfile(const QString &) {}
    Q_INVOKABLE void addProfile(const QString &) {}
    Q_INVOKABLE void removeProfile(const QString &) {}
    Q_INVOKABLE void renameProfile(const QString &, const QString &) {}
    Q_INVOKABLE void checkForUpdates() {}
    Q_INVOKABLE void downloadUpdate() {}
    Q_INVOKABLE void openLatestRelease() {}
    Q_INVOKABLE void openUrl(const QString &) {}
    Q_INVOKABLE void startWindowDrag(QObject *) {}
    Q_INVOKABLE void pingConfigs() {}
    Q_INVOKABLE bool importFromClipboard() { return false; }

signals:
    void stateChanged();
    void tick();
    void configChanged();
    void configsChanged();
    void settingsChanged();
    void logChanged();
    void splitChanged();
    void hotkeysChanged();
    void updateChanged();
    void pingsChanged();
    void languageChanged(const QString &lang);
    void errorOccurred(const QString &msg);

private:
    bool m_connected = false;
    bool m_connecting = false;
    bool m_disconnecting = false;
    QString m_sessionTime = QStringLiteral("0:00:01");
    QString m_downSpeed = QStringLiteral("1.2");
    QString m_upSpeed = QStringLiteral("0.4");
    QString m_activeConfig = QStringLiteral("Test Config");
    QStringList m_configs = {QStringLiteral("Test Config"), QStringLiteral("Backup")};
    int m_activeIndex = 0;
    QString m_language = QStringLiteral("en");
    QString m_themeMode = QStringLiteral("dark");
    bool m_autoConnect = false;
    bool m_killSwitch = false;
    QVariantList m_logEntries;
    bool m_splitEnabled = false;
    QString m_vpnMode = QStringLiteral("general");
    QStringList m_domains;
    QStringList m_excludedRoutes = {QStringLiteral("10.0.0.0/8")};
    QStringList m_profiles = {QStringLiteral("Default")};
    QString m_activeProfile = QStringLiteral("Default");
    bool m_hotkeysEnabled = true;
    bool m_autoStart = false;
    QVariantList m_pings = {QStringLiteral("42 ms"), QStringLiteral("—")};
    int m_toggleCount = 0;
};
