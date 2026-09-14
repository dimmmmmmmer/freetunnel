// cppcheck-suppress-file missingIncludeSystem
#include "app/PlatformAutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include "core/AppImagePath.h"

#include <cstring>

namespace freetunnel {

// The program a LaunchAgent plist launches: the first <string> inside the
// ProgramArguments <array>, with the XML escaping undone.
//
// Deliberately outside the platform guards, and compiled everywhere, so the
// parsing can be checked where the tests actually run rather than only on a Mac.
// Read by hand rather than with an XML parser: the file this reads is the one
// written a few lines below, its shape is fixed, and QtXml is not linked.
QString autoStartProgramFromPlist(const QString &plistXml)
{
    const int array = plistXml.indexOf(QLatin1String("<array>"));
    if (array < 0)
        return QString();
    const int open = plistXml.indexOf(QLatin1String("<string>"), array);
    if (open < 0)
        return QString();
    const int from = open + int(strlen("<string>"));
    const int close = plistXml.indexOf(QLatin1String("</string>"), from);
    if (close < 0)
        return QString();
    QString value = plistXml.mid(from, close - from);
    // The inverse of plistEscaped(), and in the inverse order: &amp; last, or it
    // would turn "&amp;lt;" — a literal "&lt;" in the path — into "<".
    value.replace(QLatin1String("&lt;"), QLatin1String("<"));
    value.replace(QLatin1String("&gt;"), QLatin1String(">"));
    value.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return value;
}

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

// The install path goes into an XML <string>; an unescaped '&' or '<' (legal in a
// macOS path) would make launchd reject the whole plist.
static QString plistEscaped(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;")); // first: it introduces the others
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out;
}

bool platformAutoStartEnabled()
{
    // The file existing is not the same as autostart working — the same thing the
    // Linux branch below had to learn. The path baked into the plist is the one
    // the app was run from when the toggle was set, and on macOS that is very
    // often not where it ends up: people launch from the mounted disk image or
    // from Downloads and drag the bundle to /Applications afterwards. Nothing
    // ever rewrites it, and nothing resyncs at startup, so reporting "on" for a
    // plist pointing at a bundle that is gone leaves the toggle lying for good.
    QFile f(autoStartPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString target = autoStartProgramFromPlist(QString::fromUtf8(f.readAll()));
    return !target.isEmpty() && QFileInfo::exists(target);
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
                            .arg(plistEscaped(QCoreApplication::applicationFilePath()))
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

// Exec= is word-split like a shell command line, so an install path containing a
// space would start the wrong program (or nothing). Quote it per the Desktop
// Entry spec: inside double quotes, '"', '\', '$' and '`' are backslash-escaped.
static QString desktopExecQuoted(const QString &path)
{
    QString out = path;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\")); // first: it introduces the others
    out.replace(QLatin1Char('"'), QLatin1String("\\\""));
    out.replace(QLatin1Char('$'), QLatin1String("\\$"));
    out.replace(QLatin1Char('`'), QLatin1String("\\`"));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

// What the autostart entry should launch. Under an AppImage,
// applicationFilePath() points inside the runtime's temporary FUSE mount
// (/tmp/.mount_FreeTuXXXXXX/usr/bin/FreeTunnel), which is unmounted on exit and
// gets a fresh random suffix on every run — an autostart entry pointing there is
// dead the moment it is written. The .AppImage file itself is stable, so record
// that when the kernel confirms we are running from one.
static QString autoStartTarget()
{
    const QString appImage = freetunnel::runningAppImagePath();
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

static void writeDesktopAutostart(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=FreeTunnel\n"
                               "Exec=%1\nTerminal=false\nX-GNOME-Autostart-enabled=true\n")
                        .arg(desktopExecQuoted(autoStartTarget()))
                        .toUtf8());
    }
}

// The path an Exec= line launches, unquoted, or an empty string if the line
// carries no program. Only the first word matters: everything after it is an
// argument, and the entry this app writes never has any.
QString autoStartExecTarget(const QString &desktopEntry)
{
    const QStringList lines = desktopEntry.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1String("Exec=")))
            continue;
        const QString value = line.mid(5).trimmed();
        if (!value.startsWith(QLatin1Char('"'))) // unquoted: the program is the first word
            return value.section(QLatin1Char(' '), 0, 0);
        // Quoted per the Desktop Entry spec: read to the closing quote, undoing
        // the backslash escapes desktopExecQuoted() put in.
        QString out;
        for (int i = 1; i < value.size(); ++i) {
            const QChar c = value.at(i);
            if (c == QLatin1Char('"'))
                break;
            if (c == QLatin1Char('\\') && i + 1 < value.size()) {
                out.append(value.at(++i));
                continue;
            }
            out.append(c);
        }
        return out;
    }
    return QString();
}

bool platformAutoStartEnabled()
{
    // The file existing is not the same as autostart working. An entry written by
    // an earlier AppImage run points at a mount that no longer exists, and
    // reporting that as "on" left the toggle lying to the user forever, with
    // nothing ever rewriting the stale path.
    QFile f(autoStartPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString target = autoStartExecTarget(QString::fromUtf8(f.readAll()));
    return !target.isEmpty() && QFileInfo::exists(target);
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
