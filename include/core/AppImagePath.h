// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>
#include <QStringList>

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
//
// Without FUSE (--appimage-extract-and-run, or APPIMAGE_EXTRACT_AND_RUN=1) there
// is no mount. The runtime unpacks into a directory named after the MD5 of the
// AppImage and stays behind as this process's parent, so the kernel still names
// the file — /proc/<parent>/exe — and its content has to match that name.
namespace freetunnel {

// The AppImage this process came from, and how the runtime started it.
struct RunningAppImage {
    QString path;           // the .AppImage file; empty when not running from one
    bool extracted = false; // unpacked by extract-and-run rather than mounted

    // What to start `path` with so it runs the way this process did. Unpacking
    // again matters: an AppImage run that way may be on a system without FUSE,
    // where starting it plainly fails.
    QStringList launchArguments() const
    {
        return extracted ? QStringList{QStringLiteral("--appimage-extract-and-run")}
                         : QStringList();
    }
};

// The AppImage file this process is running out of, or an empty path when the
// process is not running from an AppImage — including the case where it does sit
// in a FUSE mount whose backing file the kernel does not name, because a caller
// that is about to elevate must have a definite answer or none at all.
// Always empty off Linux.
RunningAppImage runningAppImage();

// runningAppImage().path, for the callers that only need the file.
QString runningAppImagePath();

// The mount source (the "what is this mounted from" field) of the mount that
// contains `path`, or an empty string when no mount matches or the mount is not
// a FUSE mount. Split out from the /proc reading above so the parsing — nested
// mounts, longest-prefix matching, the optional-fields section, octal escapes —
// is unit-testable against synthetic mountinfo text on any platform.
// `mountinfo` is the verbatim content of a /proc/<pid>/mountinfo file.
QString fuseMountSourceForPath(const QString &mountinfo, const QString &path);

// `candidate`, canonicalised, when `exe` lies under a directory the runtime's
// extract-and-run names appimage_extracted_<MD5> and `candidate` is a file with
// that MD5; otherwise an empty string. The candidate is the kernel's name for the
// parent process's executable; split out so the matching is testable with
// ordinary files.
QString extractedAppImageSource(const QString &exe, const QString &candidate);

} // namespace freetunnel
