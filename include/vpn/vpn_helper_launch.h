// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QtGlobal>

#include <QString>
#include <QStringList>

class QProcess;

namespace freetunnel {

struct HelperLaunchConfig {
    quint16 port = 0;
    QString token;
    bool ok() const { return port != 0 && !token.isEmpty(); }
};

/// Parse `--helper --port P --token-file F` arguments (reads the token file).
HelperLaunchConfig parseHelperLaunchArgs(const QStringList &args);

/// Read a one-time token from a helper launch file. Empty unless @p path names
/// what the GUI writes — a regular `.fthelper-*` file of at most 128 bytes —
/// and the file is left in place: the GUI removes it, as the user.
QString readHelperTokenFile(const QString &path);

// Build the argv that pkexec is asked to run AS ROOT on Linux.
//
// @p appImage must come from the kernel (runningAppImagePath()), never from
// $APPIMAGE or $APPDIR: an attacker who can set the GUI's environment sets both
// sides of any check between them, and this string names the binary the user is
// about to authorize as root. Empty means "not an AppImage build" and the running
// executable is used instead. An AppImage is started through /bin/sh, which gives
// the AppImage runtime a directory of root's own to unpack it into.
//
// Declared here, beside the parser that reads these arguments back, because it
// used to be file-local in vpn_helper_client.cpp and therefore untested — the one
// function in the codebase whose output is executed with full privilege.
QStringList linuxHelperCommand(const QString &exe, const QString &appImage, quint16 port,
                               const QString &tokenPath);

// Start `elevator` (pkexec or sudo) on @p helperCmd in @p proc. True once the
// elevator is running; for pkexec, only if it has not already given up a second
// later. The helper's stdout and stderr go to the null device: nothing reads them,
// and as pipes they piled up in this process's memory for the helper's lifetime.
bool startLinuxElevation(QProcess *proc, const QString &elevator, const QStringList &helperCmd);

// The AppleScript osascript runs to start the helper on macOS:
// `do shell script "…" with administrator privileges`, the command inside quoted
// once for /bin/sh and again for the AppleScript string. Both paths are the
// user's to choose (the app runs from wherever it was put), and this text runs
// as root.
QString macHelperElevationScript(const QString &exe, quint16 port, const QString &tokenPath);

// The command line ShellExecuteExW ("runas") hands the elevated helper on
// Windows, read back by CommandLineToArgvW. The token path lies under the user's
// profile folder, whose name may hold spaces.
QString windowsHelperParameters(quint16 port, const QString &tokenPath);

} // namespace freetunnel
