// cppcheck-suppress-file missingIncludeSystem
#include "core/ConfigPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace freetunnel {

QString sanitizeConfigBaseName(const QString &name, const QString &fallbackPrefix)
{
    QString safe;
    for (const QChar &c : name) {
        safe += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('_');
    }
    if (safe.isEmpty())
        safe = QStringLiteral("%1-%2").arg(fallbackPrefix).arg(QDateTime::currentSecsSinceEpoch());
    return safe;
}

QString uniqueOwnerConfigPath(const QString &stem)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString target = QDir(base).filePath(stem + QStringLiteral(".toml"));
    if (QFileInfo::exists(target))
        target = QDir(base).filePath(QStringLiteral("%1-%2.toml").arg(stem).arg(QDateTime::currentSecsSinceEpoch()));
    return target;
}

} // namespace freetunnel
