// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QCoreApplication>
#include <QStandardPaths>

#include "app/PlatformAutoStart.h"

QString Backend::logPath() const {
    if (!m_settings.log_path.isEmpty())
        return m_settings.log_path;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/freetunnel.log");
}

void Backend::setLoggingEnabled(bool v) {
    if (m_settings.logging_enabled == v)
        return;
    m_settings.logging_enabled = v;
    persistSettings();
    m_client.setSessionLogging(logPath(), v);
    emit settingsChanged();
}

void Backend::setVerboseLogs(bool v) {
    if (m_settings.verbose_logs == v)
        return;
    m_settings.verbose_logs = v;
    persistSettings();
    emit settingsChanged();
    // The core's log level is baked into the config at connect time; reconnect
    // seamlessly so the change takes effect right away if a tunnel is up.
    reapplyIfConnected();
}

// cppcheck-suppress functionStatic
bool Backend::autoStart() const {
    return freetunnel::platformAutoStartEnabled();
}

void Backend::setAutoStart(bool v) {
    freetunnel::setPlatformAutoStart(v);
    emit settingsChanged();
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
    reapplyIfConnected();
    emit settingsChanged();
}
