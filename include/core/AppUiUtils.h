#pragma once

#include <QString>

bool openHttpUrl(const QString &url);
QString shellEscape(QString s);
QString appleScriptEscape(QString s);

// Read a user-selected text file (e.g. a PEM certificate). Rejects symlinks,
// paths outside common user directories, and files larger than maxBytes.
QString safeReadUserTextFile(const QString &pathOrUrl, qint64 maxBytes = 1024 * 1024);
