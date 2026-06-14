#pragma once

// Bridge object exposed to QML. Wraps the VPN client + config/settings storage
// so the QML UI never touches the C++ core directly. Phase 1 wires the home
// screen (connect/disconnect, state, live speeds, active config); other screens
// are wired incrementally.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "core/AppSettings.h"
#include "vpn/qt_trusttunnel_client.h"

class QHotkey;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString sessionTime READ sessionTime NOTIFY tick)
    Q_PROPERTY(QString downSpeed READ downSpeed NOTIFY tick)
    Q_PROPERTY(QString upSpeed READ upSpeed NOTIFY tick)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY configChanged)
    Q_PROPERTY(QStringList configs READ configs NOTIFY configsChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY configChanged)
    // settings (read/write; persisted on set)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool killSwitch READ killSwitch WRITE setKillSwitch NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList logEntries READ logEntries NOTIFY logChanged)
    Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY splitChanged)
    Q_PROPERTY(QStringList domains READ domains NOTIFY splitChanged)
    // Global hotkeys (portable key sequences, e.g. "Ctrl+Alt+T"; empty = unbound)
    Q_PROPERTY(QString hotkeyToggle READ hotkeyToggle WRITE setHotkeyToggle NOTIFY hotkeysChanged)
    Q_PROPERTY(QString hotkeyConnect READ hotkeyConnect WRITE setHotkeyConnect NOTIFY hotkeysChanged)
    Q_PROPERTY(QString hotkeyDisconnect READ hotkeyDisconnect WRITE setHotkeyDisconnect NOTIFY hotkeysChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString statusText() const;
    QString sessionTime() const;
    QString downSpeed() const;
    QString upSpeed() const;
    QString activeConfig() const;
    QStringList configs() const { return m_names; }
    int activeIndex() const { return m_paths.indexOf(m_activePath); }

    QString language() const { return m_settings.language; }
    QString themeMode() const { return m_settings.theme_mode; }
    bool autoConnect() const { return m_settings.auto_connect_on_start; }
    bool killSwitch() const { return m_settings.killswitch_enabled; }
    void setLanguage(const QString &v);
    void setThemeMode(const QString &v);
    void setAutoConnect(bool v);
    void setKillSwitch(bool v);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void connectVpn();
    Q_INVOKABLE void disconnectVpn();
    // Handle a control command from a deep link / second instance:
    // "freetunnel://toggle|connect|disconnect" or a "tt://" config import.
    void handleControl(const QString &command);
    Q_INVOKABLE void selectConfig(int index);
    Q_INVOKABLE void removeConfig(int index);
    Q_INVOKABLE bool importDeepLink(const QString &link);
    Q_INVOKABLE bool importFile(const QString &path);
    Q_INVOKABLE bool createConfig(const QVariantMap &fields);

    QVariantList logEntries() const { return m_log; }
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void openLogFolder();

    bool splitEnabled() const { return m_settings.domain_bypass_enabled; }
    QStringList domains() const { return m_settings.domain_bypass_rules; }
    void setSplitEnabled(bool v);
    Q_INVOKABLE void addDomain(const QString &domain);
    Q_INVOKABLE void removeDomain(int index);
    Q_INVOKABLE void clearDomains();

    QString hotkeyToggle() const { return m_settings.hotkey_toggle; }
    QString hotkeyConnect() const { return m_settings.hotkey_connect; }
    QString hotkeyDisconnect() const { return m_settings.hotkey_disconnect; }
    void setHotkeyToggle(const QString &v);
    void setHotkeyConnect(const QString &v);
    void setHotkeyDisconnect(const QString &v);

signals:
    void stateChanged();
    void tick();
    void configChanged();
    void configsChanged();
    void settingsChanged();
    void logChanged();
    void splitChanged();
    void hotkeysChanged();
    void errorOccurred(const QString &msg);

private:
    void reloadConfigs();
    void persistSettings();
    void registerHotkeys(); // (re)bind global hotkeys from current settings
    void appendLog(const QString &level, const QString &msg);
    bool ensureElevated(); // returns false (and relaunches) if elevation needed
    QString nameForPath(const QString &path) const;

    QtTrustTunnelClient m_client;
    AppSettings m_settings;
    QHotkey *m_hkToggle = nullptr;
    QHotkey *m_hkConnect = nullptr;
    QHotkey *m_hkDisconnect = nullptr;
    QStringList m_paths;       // config file paths
    QStringList m_names;       // display names, parallel to m_paths
    QString m_activePath;
    bool m_connected = false;
    QVariantList m_log;

    QElapsedTimer m_session;
    QTimer m_ticker;
    quint64 m_accUp = 0, m_accDown = 0; // bytes accumulated since last tick
    double m_upRate = 0, m_downRate = 0; // bytes/sec
};
