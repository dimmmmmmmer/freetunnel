// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

// Turn whatever a person dropped on the window — or picked in a file dialog —
// into the executable it stands for.
//
// Nobody drags a binary out of /usr/bin. They drag the icon they already have:
// a .desktop entry on Linux, a .lnk shortcut on Windows, an .app bundle on
// macOS. Each of those names a program without being one, so each has to be
// followed to the thing that will actually show up in the system's socket table
// — which is the only name a rule can ever match.
//
// Returns the name a rule should use: an absolute path to an existing file,
// or — for a program that runs inside a sandbox — a bare program name, or — for
// one Squirrel installed — its path without the version directory, which is
// the one name it keeps across updates (see ruleForProgram). Empty when the
// argument is not a program and does not lead to one. Accepts a plain path or
// a file: URL.
//
// The sandbox case is not a nicety. A Flatpak entry launches
// "/usr/bin/flatpak run … com.anydesk.Anydesk", so following it naively yields
// /usr/bin/flatpak — and a bypass rule for that would take EVERY Flatpak
// program out of the tunnel, not the one that was asked for. The process the
// system actually reports is /app/extra/anydesk, inside the sandbox, so a bare
// name is what matches it.
QString resolveApplicationTarget(const QString &pathOrUrl);

// The rule for a program file that exists, given the arguments it is started
// with: a Windows shortcut's, or none when the file was picked on its own.
// Almost always the file itself, spelled the way the system reports a running
// process. Not for a program Squirrel installed — Discord, Slack, GitHub
// Desktop and many other Electron applications on Windows. Its shortcut starts
// Update.exe --processStart Discord.exe, Update.exe starts Discord.exe out of
// an app-<version> directory, and each update replaces that directory. So the
// shortcut, Update.exe, and the program picked out of today's directory all
// answer the program without its version (see unversionedAppPath), which is
// what the rule matches in every version.
//
// Separated from the shell that reads a shortcut, so it is tested where the
// tests run.
QString ruleForProgram(const QString &program, const QString &arguments);

// A rule already stored for a Squirrel updater, <root>\Update.exe, as picking
// Discord from the list or dropping its shortcut stored one before the shortcut's
// arguments were read: the rule for the program that updater installs, as
// ruleForProgram gives it for Update.exe picked on its own. Empty when the rule
// is not one, or that program is not installed there.
QString squirrelProgramForUpdaterRule(const QString &rule);

// The Exec/TryExec program named by the contents of a .desktop entry, without
// its arguments or field codes. Separated from the file handling so the parsing
// can be tested on every platform, not only where .desktop files are native.
// Returns an empty string when the entry names nothing usable.
QString executableFromDesktopEntry(const QString &contents);

// The program a .desktop entry launches through a sandbox runner, as a bare
// name, or empty when the entry is not one of those. Exposed for testing, and
// because the list of installed applications needs the same answer.
QString sandboxedProgramFromDesktopEntry(const QString &contents);

// The value of one key in a .desktop entry's [Desktop Entry] group, or empty.
// Shared rather than reimplemented: InstalledApps had a byte-identical copy, and
// two copies of a parser are two places to fix the next quirk in.
QString desktopEntryValue(const QString &contents, const QString &key);

} // namespace freetunnel
