// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

// Returns true when @p remote is strictly newer than @p current.
// Handles numeric semver-ish parts plus suffixes (e.g. "0.6b", "1.2.3-beta").
bool isVersionNewer(const QString &current, const QString &remote);
