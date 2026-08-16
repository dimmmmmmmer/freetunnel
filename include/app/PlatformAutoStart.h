// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

bool platformAutoStartEnabled();
void setPlatformAutoStart(bool enabled);

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// The program an autostart .desktop entry launches, with Desktop Entry quoting
// undone, or an empty string when the entry has no Exec= line. Exposed for tests:
// whether the recorded target still exists is what decides if autostart is really
// on, and that logic has to be checkable without a live session.
QString autoStartExecTarget(const QString &desktopEntry);
#endif

} // namespace freetunnel
