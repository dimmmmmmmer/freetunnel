// cppcheck-suppress-file missingIncludeSystem
#include "core/ConfigPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace freetunnel {

namespace {

// The config directory holds the .toml files (hostnames, usernames, certificates)
// and the credentials subdirectory. mkpath() creates it 0755, so tighten it to
// owner-only — every file inside is already 0600, but the directory listing
// itself shouldn't be readable by other local users either.
QString ensureOwnerConfigDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QFile::setPermissions(base, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner);
    return base;
}

} // namespace

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
    const QString base = ensureOwnerConfigDir();
    const QString first = QDir(base).filePath(stem + QStringLiteral(".toml"));
    if (!QFileInfo::exists(first))
        return first;
    // Count up rather than stamping the time: a one-second-resolution timestamp
    // is not unique, so two imports inside the same second resolved to the SAME
    // path and the second silently overwrote the first — precisely the clobbering
    // this function exists to prevent. "Work-2.toml" also reads better than an
    // epoch suffix in the config list.
    for (int i = 2; i < 1000; ++i) {
        const QString candidate =
                QDir(base).filePath(QStringLiteral("%1-%2.toml").arg(stem).arg(i));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    // Absurd number of same-named configs: fall back to something collision-proof
    // rather than returning a path we know is taken.
    return QDir(base).filePath(QStringLiteral("%1-%2-%3.toml")
                                       .arg(stem)
                                       .arg(QDateTime::currentSecsSinceEpoch())
                                       .arg(QRandomGenerator::system()->generate(), 8, 16,
                                            QLatin1Char('0')));
}


QString ownerConfigPathForSave(const QString &stem, const QString &existingPath)
{
    if (!existingPath.isEmpty()) {
        const QFileInfo existing(existingPath);
        if (existing.completeBaseName() == stem)
            return existingPath;
    }
    return uniqueOwnerConfigPath(stem);
}

} // namespace freetunnel
