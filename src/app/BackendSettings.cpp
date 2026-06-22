#include "app/Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString Backend::logPath() const {
    if (!m_settings.log_path.isEmpty())
        return m_settings.log_path;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/freetunnel.log");
}

#if defined(Q_OS_WIN)
static const char *kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool Backend::autoStart() const {
    QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return !r.value(QStringLiteral("FreeTunnel")).toString().isEmpty();
}
void Backend::setAutoStart(bool v) {
    QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (v)
        r.setValue(QStringLiteral("FreeTunnel"),
                   QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                           + QLatin1Char('"'));
    else
        r.remove(QStringLiteral("FreeTunnel"));
    emit settingsChanged();
}
#elif defined(Q_OS_MACOS)
static QString autoStartPath() {
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/com.freetunnel.app.plist");
}
bool Backend::autoStart() const { return QFileInfo::exists(autoStartPath()); }
void Backend::setAutoStart(bool v) {
    const QString p = autoStartPath();
    if (v) {
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "  <key>Label</key><string>com.freetunnel.app</string>\n"
                "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
                "  <key>RunAtLoad</key><true/>\n"
                "</dict></plist>\n").arg(QCoreApplication::applicationFilePath()).toUtf8());
    } else {
        QFile::remove(p);
    }
    emit settingsChanged();
}
#else
static QString autoStartPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/autostart/freetunnel.desktop");
}
bool Backend::autoStart() const { return QFileInfo::exists(autoStartPath()); }
void Backend::setAutoStart(bool v) {
    const QString p = autoStartPath();
    if (v) {
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=FreeTunnel\n"
                                   "Exec=%1\nTerminal=false\nX-GNOME-Autostart-enabled=true\n")
                            .arg(QCoreApplication::applicationFilePath()).toUtf8());
    } else {
        QFile::remove(p);
    }
    emit settingsChanged();
}
#endif

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
    m_active->setKillSwitch(v);
    reapplyIfConnected(); // apply on the live tunnel
    emit settingsChanged();
}
