// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

/// Sanitize a display name / hostname into a safe config filename stem.
QString sanitizeConfigBaseName(const QString &name, const QString &fallbackPrefix = QStringLiteral("imported"));

/// Resolve a unique owner-only config path under AppConfigLocation.
QString uniqueOwnerConfigPath(const QString &stem);

} // namespace freetunnel
