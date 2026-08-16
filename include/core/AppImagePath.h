// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

// Answers one question: which file on disk backs the process that is running
// right now, when that file is an AppImage?
//
// Two call sites need it and both used to answer it from the environment, by
// reading $APPIMAGE and sanity-checking it against $APPDIR. That is not a check
// at all — both are environment variables, so anything able to set the GUI's
// environment (a shell profile, a .desktop file, a wrapper script) picks both
// sides of the comparison. The elevation path made this a privilege escalation:
// $APPDIR only had to be a *path prefix* of the running executable, so
// APPDIR=/usr passed for an ordinary /usr/bin/FreeTunnel install, and whatever
// $APPIMAGE named was then handed to pkexec and executed as root.
//
// So ask the kernel instead. The AppImage runtime mounts the payload over FUSE
// and runs the executable from inside that mount; /proc/self/mountinfo names the
// file backing the mount, and no amount of environment control changes what the
// kernel reports there.
namespace freetunnel {

// The AppImage file this process is running out of, or an empty string when the
// process is not running from an AppImage — including the case where it does sit
// in a FUSE mount whose backing file the kernel does not name, because a caller
// that is about to elevate must have a definite answer or none at all.
// Always empty off Linux.
QString runningAppImagePath();

// The mount source (the "what is this mounted from" field) of the mount that
// contains `path`, or an empty string when no mount matches or the mount is not
// a FUSE mount. Split out from the /proc reading above so the parsing — nested
// mounts, longest-prefix matching, the optional-fields section, octal escapes —
// is unit-testable against synthetic mountinfo text on any platform.
// `mountinfo` is the verbatim content of a /proc/<pid>/mountinfo file.
QString fuseMountSourceForPath(const QString &mountinfo, const QString &path);

} // namespace freetunnel
