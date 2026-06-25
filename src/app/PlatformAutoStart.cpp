// cppcheck-suppress-file missingIncludeSystem
#include "app/PlatformAutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace freetunnel {

#if defined(Q_OS_WIN)
static const char *kRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool platformAutoStartEnabled()
{
    QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return !r.value(QStringLiteral("FreeTunnel")).toString().isEmpty();
}

void setPlatformAutoStart(bool enabled)
{
    QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        r.setValue(QStringLiteral("FreeTunnel"),
                   QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                           + QLatin1Char('"'));
    } else {
        r.remove(QStringLiteral("FreeTunnel"));
    }
}
#elif defined(Q_OS_MACOS)
static QString autoStartPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/com.freetunnel.app.plist");
}

bool platformAutoStartEnabled()
{
    return QFileInfo::exists(autoStartPath());
}

void setPlatformAutoStart(bool enabled)
{
    const QString p = autoStartPath();
    if (enabled) {
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                "<plist version=\"1.0\"><dict>\n"
                "  <key>Label</key><string>com.freetunnel.app</string>\n"
                "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
                "  <key>RunAtLoad</key><true/>\n"
                "</dict></plist>\n")
                            .arg(QCoreApplication::applicationFilePath())
                            .toUtf8());
        }
    } else {
        QFile::remove(p);
    }
}
#else
static QString autoStartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/autostart/freetunnel.desktop");
}

static void writeDesktopAutostart(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=FreeTunnel\n"
                               "Exec=%1\nTerminal=false\nX-GNOME-Autostart-enabled=true\n")
                        .arg(QCoreApplication::applicationFilePath())
                        .toUtf8());
    }
}

bool platformAutoStartEnabled()
{
    return QFileInfo::exists(autoStartPath());
}

void setPlatformAutoStart(bool enabled)
{
    const QString p = autoStartPath();
    if (enabled)
        writeDesktopAutostart(p);
    else
        QFile::remove(p);
}
#endif

} // namespace freetunnel
