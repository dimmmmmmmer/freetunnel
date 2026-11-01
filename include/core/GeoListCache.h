// cppcheck-suppress-file missingIncludeSystem
#pragma once
#include "core/GeoRules.h"
#include <QDateTime>

namespace freetunnel {

struct CachedGeoList {
    std::vector<std::string> entries;
    GeoListStats stats;
    QDateTime updated;
};

// A download only replaces the last usable cache after successful parsing.
// Source, category, kind and direction are part of its identity: switching a
// source or direction must never temporarily apply another list's old rules.
class GeoListCache {
public:
    static constexpr qint64 maxBytes = 128 * 1024 * 1024;
    explicit GeoListCache(QString directory);
    bool load(const QString &source, GeoListKind kind, GeoDirection direction,
              CachedGeoList *result, QString *error) const;
    bool store(const QString &source, GeoListKind kind, GeoDirection direction,
               const QByteArray &data, CachedGeoList *result, QString *error) const;
private:
    QString path(const QString &source, GeoListKind kind, GeoDirection direction) const;
    QString m_directory;
};

} // namespace freetunnel
