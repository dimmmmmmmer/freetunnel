// cppcheck-suppress-file missingIncludeSystem
#include "AppUiUtils.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

bool pathUnderRoot(const QString &canonical, const QString &root)
{
    if (root.isEmpty())
        return false;
    const QString rootCanon = QFileInfo(root).canonicalFilePath();
    if (rootCanon.isEmpty())
        return false;
    return canonical == rootCanon || canonical.startsWith(rootCanon + QLatin1Char('/'));
}

bool isAllowedUserFile(const QFileInfo &fi)
{
    if (!fi.exists() || !fi.isFile() || fi.isSymLink())
        return false;

    const QString canonical = fi.canonicalFilePath();
    if (canonical.isEmpty())
        return false;

    const QStringList roots = {
        QDir::homePath(),
        QStandardPaths::writableLocation(QStandardPaths::TempLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
    };
    return std::any_of(roots.cbegin(), roots.cend(),
                       [&](const QString &root) { return pathUnderRoot(canonical, root); });
}

} // namespace

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

QString safeReadUserTextFile(const QString &pathOrUrl, qint64 maxBytes)
{
    QString p = pathOrUrl.trimmed();
    if (p.startsWith(QStringLiteral("file://")))
        p = QUrl(p).toLocalFile();
    if (p.isEmpty())
        return QString();

    const QFileInfo fi(p);
    if (!isAllowedUserFile(fi))
        return QString();
    if (fi.size() > maxBytes)
        return QString();

    QFile f(fi.canonicalFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
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
