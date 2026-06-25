// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

/// Sanitize a display name / hostname into a safe config filename stem.
QString sanitizeConfigBaseName(const QString &name, const QString &fallbackPrefix = QStringLiteral("imported"));

/// Resolve a unique owner-only config path under AppConfigLocation.
QString uniqueOwnerConfigPath(const QString &stem);

/// Pick a save path for create/edit: reuse @p existingPath when the stem is unchanged.
QString ownerConfigPathForSave(const QString &stem, const QString &existingPath = {});

} // namespace freetunnel
