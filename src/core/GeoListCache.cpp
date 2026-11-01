// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoListCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <utility>

namespace freetunnel {
namespace {
bool parse(const QString &source, GeoListKind kind, GeoDirection direction,
           const QByteArray &data, CachedGeoList *result, QString *error)
{
    if (error)
        error->clear();
    if (data.size() > GeoListCache::maxBytes) {
        if (error)
            *error = QStringLiteral("Geo list is too large");
        return false;
    }
    const auto spec = parseGeoSource(source, error);
    if (!spec.valid())
        return false;
    if (!appendGeoEntries(data, spec, kind, direction, &result->entries, &result->stats, error))
        return false;
    for (const auto &entry : result->entries)
        result->routingBytes += qint64(entry.size()) + 6; // prefix, quotes, comma
    return true;
}
}

GeoListCache::GeoListCache(QString directory) : m_directory(std::move(directory)) {}

QString GeoListCache::path(const QString &source, GeoListKind kind, GeoDirection direction) const
{
    const QByteArray identity = source.toUtf8() + '\0' + QByteArray::number(int(kind))
            + ':' + QByteArray::number(int(direction));
    return QDir(m_directory).filePath(QString::fromLatin1(
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex())
            + QStringLiteral(".list"));
}

bool GeoListCache::load(const QString &source, GeoListKind kind, GeoDirection direction,
                        CachedGeoList *result, QString *error) const
{
    QFile file(path(source, kind, direction));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    CachedGeoList candidate;
    if (!parse(source, kind, direction, file.read(maxBytes + 1), &candidate, error))
        return false;
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = file.errorString();
        return false;
    }
    candidate.updated = QFileInfo(file).lastModified().toUTC();
    *result = std::move(candidate);
    return true;
}

bool GeoListCache::store(const QString &source, GeoListKind kind, GeoDirection direction,
                         const QByteArray &data, CachedGeoList *result, QString *error) const
{
    CachedGeoList candidate;
    if (!parse(source, kind, direction, data, &candidate, error))
        return false;
    if (!QDir().mkpath(m_directory)) {
        if (error)
            *error = QStringLiteral("Cannot create geo list cache directory");
        return false;
    }
    QSaveFile file(path(source, kind, direction));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    candidate.updated = QFileInfo(file.fileName()).lastModified().toUTC();
    *result = std::move(candidate);
    return true;
}
} // namespace freetunnel
