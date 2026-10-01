// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

bool openHttpUrl(const QString &url);
QString shellEscape(QString s);
QString appleScriptEscape(QString s);

// Why safeReadUserTextFile() read nothing, so the caller can say so instead of
// handing back an empty text as if the file were empty.
enum class UserFileRefusal { None, Unreadable, OutsideUserFolders, SymLink, TooLarge };

// Read a user-selected text file (e.g. a PEM certificate). Rejects symlinks,
// paths outside common user directories, and files larger than maxBytes, and
// sets *refusal (when given) to the reason, or to None when the file was read.
inline constexpr qint64 kMaxUserTextFileBytes = 1024 * 1024;
QString safeReadUserTextFile(const QString &pathOrUrl, qint64 maxBytes = kMaxUserTextFileBytes,
                             UserFileRefusal *refusal = nullptr);
