// cppcheck-suppress-file missingIncludeSystem
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

class QHostAddress;

#include "core/AppSettings.h"
#include "vpn/vpn_helper_client.h"

class QHotkey;
class UpdateChecker;

namespace freetunnel {
struct PreparedImport;
}

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY stateChanged) // connecting/reconnecting
    Q_PROPERTY(bool disconnecting READ disconnecting NOTIFY stateChanged) // tearing the tunnel down
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString sessionTime READ sessionTime NOTIFY tick)
    Q_PROPERTY(QString downSpeed READ downSpeed NOTIFY tick)
    Q_PROPERTY(QString upSpeed READ upSpeed NOTIFY tick)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY configChanged)
    Q_PROPERTY(QStringList configs READ configs NOTIFY configsChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY configChanged)
    // settings (read/write; persisted on set)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool killSwitch READ killSwitch WRITE setKillSwitch NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList logEntries READ logEntries NOTIFY logChanged)
    Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY splitChanged)
    Q_PROPERTY(QString vpnMode READ vpnMode WRITE setVpnMode NOTIFY splitChanged) // general|selective
    Q_PROPERTY(QStringList domains READ domains NOTIFY splitChanged)
    Q_PROPERTY(QStringList excludedRoutes READ excludedRoutes NOTIFY splitChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY splitChanged)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY splitChanged)
    // Global hotkeys (portable key sequences, e.g. "Ctrl+Alt+T"; empty = unbound)
    Q_PROPERTY(bool hotkeysEnabled READ hotkeysEnabled WRITE setHotkeysEnabled NOTIFY hotkeysChanged)
    Q_PROPERTY(QString hotkeyToggle READ hotkeyToggle WRITE setHotkeyToggle NOTIFY hotkeysChanged)
    Q_PROPERTY(QString hotkeyConnect READ hotkeyConnect WRITE setHotkeyConnect NOTIFY hotkeysChanged)
    Q_PROPERTY(QString hotkeyDisconnect READ hotkeyDisconnect WRITE setHotkeyDisconnect NOTIFY hotkeysChanged)
    // Updater (GitHub Releases)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(QString updateState READ updateState NOTIFY updateChanged) // ""|checking|current|available|error
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateChanged)
    // Misc
    Q_PROPERTY(QString logPath READ logPath CONSTANT)
    Q_PROPERTY(bool loggingEnabled READ loggingEnabled WRITE setLoggingEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList pings READ pings NOTIFY pingsChanged) // per-config latency text
    Q_PROPERTY(QString credentialStorageWarning READ credentialStorageWarning NOTIFY credentialStorageChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    bool connecting() const { return m_connecting; }
    bool disconnecting() const { return m_disconnecting; }
    QString statusText() const;
    QString sessionTime() const;
    QString downSpeed() const;
    QString upSpeed() const;
    QString activeConfig() const;
    const QStringList &configs() const { return m_names; }
    int activeIndex() const { return m_paths.indexOf(m_activePath); }

    const QString &language() const { return m_settings.language; }
    const QString &themeMode() const { return m_settings.theme_mode; }
    bool autoConnect() const { return m_settings.auto_connect_on_start; }
    bool killSwitch() const { return m_settings.killswitch_enabled; }
    bool loggingEnabled() const { return m_settings.logging_enabled; }
    void setLanguage(const QString &v);
    void setThemeMode(const QString &v);
    void setAutoConnect(bool v);
    void setKillSwitch(bool v);
    void setLoggingEnabled(bool v);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void connectVpn();
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE void prepareQuit();
    Q_INVOKABLE void quitApplication();
    Q_INVOKABLE bool applicationClosingDown() const;
    // Handle a control command from a deep link / second instance:
    // "freetunnel://toggle|connect|disconnect" or a "tt://" config import.
    void handleControl(const QString &command);
    Q_INVOKABLE void selectConfig(int index);
    Q_INVOKABLE void removeConfig(int index);
    Q_INVOKABLE void moveConfig(int from, int to); // manual reorder (drag in the list)
    Q_INVOKABLE bool importDeepLink(const QString &link);
    Q_INVOKABLE bool confirmDeepLinkImport(const QString &link);
    Q_INVOKABLE bool importFile(const QString &path);
    Q_INVOKABLE bool createConfig(const QVariantMap &fields);
    Q_INVOKABLE QVariantMap configFields(int index) const; // parse a config for editing
    // Export: a shareable tt:// deep link, or a full .toml written to disk
    // (both carry the password — pulled from the OS keychain).
    Q_INVOKABLE QString configDeepLink(int index) const;
    Q_INVOKABLE bool exportConfigToml(int index, const QString &fileUrl) const;

    QVariantList logEntries() const { return m_log; }
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE QString logText() const; // whole log as plain text (for Copy)
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE QString readTextFile(const QString &pathOrUrl) const; // for cert load

    bool splitEnabled() const { return m_settings.domain_bypass_enabled; }
    const QStringList &domains() const { return m_settings.domain_bypass_rules; }
    void setSplitEnabled(bool v);
    const QString &vpnMode() const { return m_settings.vpn_mode; }
    void setVpnMode(const QString &mode);
    Q_INVOKABLE bool addDomain(const QString &domain); // accepts a list; true if any added
    Q_INVOKABLE void removeDomain(int index);
    Q_INVOKABLE void clearDomains();

    const QStringList &excludedRoutes() const { return m_settings.excluded_routes; }
    Q_INVOKABLE bool addExcludedRoute(const QString &route);
    Q_INVOKABLE void removeExcludedRoute(int index);
    Q_INVOKABLE void clearExcludedRoutes();
    Q_INVOKABLE void restoreDefaultExcludedRoutes();

    // Add the built-in "Recommended for Russia" domain set to the active profile.
    Q_INVOKABLE void addRecommendedRussia();

    const QStringList &profiles() const;
    const QString &activeProfile() const { return m_settings.active_profile; }
    Q_INVOKABLE void selectProfile(const QString &name);
    Q_INVOKABLE void addProfile(const QString &name);
    Q_INVOKABLE void removeProfile(const QString &name);
    Q_INVOKABLE void renameProfile(const QString &oldName, const QString &newName);

    bool hotkeysEnabled() const { return m_settings.hotkeys_enabled; }
    void setHotkeysEnabled(bool v);
    const QString &hotkeyToggle() const { return m_settings.hotkey_toggle; }
    const QString &hotkeyConnect() const { return m_settings.hotkey_connect; }
    const QString &hotkeyDisconnect() const { return m_settings.hotkey_disconnect; }
    void setHotkeyToggle(const QString &v);
    void setHotkeyConnect(const QString &v);
    void setHotkeyDisconnect(const QString &v);

    QString appVersion() const;
    QString coreVersion() const;
    const QString &updateState() const { return m_updateState; }
    const QString &updateMessage() const { return m_updateMessage; }
    const QString &latestVersion() const { return m_latestVersion; }
    Q_INVOKABLE void checkForUpdates(bool userInitiated = true);
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void openLatestRelease();
    Q_INVOKABLE void openUrl(const QString &url);
    // Begin a native window move (drag). `window` is the QML Window root.
    Q_INVOKABLE void startWindowDrag(QObject *window);

    QString logPath() const;
    bool autoStart() const;
    void setAutoStart(bool v);
    QVariantList pings() const { return m_pings; }
    Q_INVOKABLE void pingConfigs();
    Q_INVOKABLE bool importFromClipboard();

    QString credentialStorageWarning() const;

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
    void credentialStorageChanged();
    void errorOccurred(const QString &msg);
    void deepLinkImportConfirmationRequired(const QString &message, const QString &link);
    void configImported(const QString &name); // a config was added via file/clipboard/deep-link
    void aboutToShutdown();

private:
    void reloadConfigs();
    void persistSettings();
    void applySplitRules(); // push the active CONFIG's profile rules to the core
    QString activeConfigProfile() const; // split profile assigned to the active config
    void reconnectActiveConfig(); // disconnect then reconnect (config switch / live rule apply)
    void reapplyIfConnected(); // rebuild the tunnel so rule changes take effect live
    void reapplyIfEditingActiveProfile(); // live-apply only if the edited profile is the active config's
    void trimLogFile();     // cap the log file size so it never grows unbounded
    void loadLogTail();     // restore recent on-disk log lines into the view at startup
    void registerHotkeys(); // (re)bind global hotkeys from current settings
    void unregisterHotkeys();
    void wireHotkeyLifecycle();
    void ensureHotkeysRegistered();
    void appendLog(const QString &level, const QString &msg);
    QString nameForPath(const QString &path) const;
    void ensureUpdater();
    void wireUpdaterSignals();
    void wireVpnClientSignals();
    void onVpnClientStateChanged(VpnHelperClient::State st);
    void onVpnErrorReceived(const QString &m);
    void onStatsTick();
    bool isInternalVpnError(const QString &m) const;
    QString friendlyVpnError(const QString &m) const;
    void emitDedupedVpnError(const QString &friendly);
    struct CreatedConfigFinalize {
        QVariantMap form;
        QString oldPath;
        QString target;
        QString password;
        QString tomlBody;
        QString editContent;
        QString editPassword;
        QString editProfile;
        bool editingSnapshot = false;
        int editIndex = -1;
    };
    void emitCreateConfigError(const QString &parseErr);
    bool finalizeCreatedConfig(const CreatedConfigFinalize &ctx);
    void markConfigPingFailed(int index);
    void runConfigPing(int index, const QHostAddress &ip, quint16 port);
    void pingConfigAtIndex(int index);
    bool finalizeImportedConfig(const QString &target, bool hadNoActive);
    bool importPreparedDeepLink(const freetunnel::PreparedImport &prepared);
    void persistCreatedConfigPaths(const QString &oldPath, const QString &target, bool editing,
                                   bool wasActive);
    void maybeReapplyCreatedConfig(const CreatedConfigFinalize &ctx);
    void applyVpnClientState(VpnHelperClient::State st);
    void clearReapplyingIfDone(VpnHelperClient::State st, bool nowConnected);
    bool shouldSkipConnectAttempt() const;
    void logConnectAttempt();
    bool loadConnectTomlOrFail(QString *tomlOut);

    VpnHelperClient m_client;
    AppSettings m_settings;
    QHotkey *m_hkToggle = nullptr;
    QHotkey *m_hkConnect = nullptr;
    QHotkey *m_hkDisconnect = nullptr;

    UpdateChecker *m_updater = nullptr;
    bool m_updateCheckUserInitiated = false;
    QString m_updateState, m_updateMessage, m_latestVersion, m_latestUrl;
    QVariantList m_pings;
    QStringList m_paths;       // config file paths
    QStringList m_names;       // display names, parallel to m_paths
    QString m_activePath;
    bool m_connected = false;
    bool m_connecting = false; // Connecting / Reconnecting / WaitingForNetwork
    bool m_disconnecting = false; // Disconnecting (tearing down / cancelling)
    QVariantList m_log;

    QElapsedTimer m_session;
    QTimer m_ticker;
    QTimer m_coreLogCoalesceTimer;
    bool m_coreLogPending = false;
    quint64 m_accUp = 0, m_accDown = 0; // bytes accumulated since last tick
    double m_upRate = 0, m_downRate = 0; // bytes/sec

    QString m_lastErrorMsg;       // last error shown as a toast (for de-duping)
    qint64 m_lastErrorAt = 0;     // ms epoch of that toast
    bool m_reapplying = false;    // guard against re-entrant reconnect (see reapplyIfConnected)
    bool m_inConnect = false;     // inside connectVpn(): suppress live-reapply
    bool m_quitting = false;      // user requested quit — allow window close on macOS
    bool m_shutdownPrepared = false; // prepareQuit() already ran
};
