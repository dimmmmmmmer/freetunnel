#include "AppUiUtils.h"

#include <QDesktopServices>
#include <QUrl>

bool openHttpUrl(const QString &urlStr)
{
    const QUrl url(urlStr);
    if (!url.isValid())
        return false;
    const auto scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    return QDesktopServices::openUrl(url);
}

// Wrap a string as a single-quoted shell literal (used to build the elevated
// helper command for osascript on macOS).
QString shellEscape(QString s) {
    s.replace("'", "'\"'\"'");
    return "'" + s + "'";
}

// Escape a string for embedding inside an AppleScript double-quoted literal.
QString appleScriptEscape(QString s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    return s;
}
