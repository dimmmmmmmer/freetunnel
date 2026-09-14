// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

bool platformAutoStartEnabled();
void setPlatformAutoStart(bool enabled);

// The program a macOS LaunchAgent plist launches, XML escaping undone, or an
// empty string when the file names none. Exposed — and compiled on every
// platform — for the same reason as its Linux counterpart below: whether the
// recorded target still exists is what decides if autostart is really on, and
// that has to be checkable somewhere other than a Mac.
QString autoStartProgramFromPlist(const QString &plistXml);

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// The program an autostart .desktop entry launches, with Desktop Entry quoting
// undone, or an empty string when the entry has no Exec= line. Exposed for tests:
// whether the recorded target still exists is what decides if autostart is really
// on, and that logic has to be checkable without a live session.
QString autoStartExecTarget(const QString &desktopEntry);
#endif

} // namespace freetunnel
