// cppcheck-suppress-file missingIncludeSystem
#include "UpdateStaging.h"
#include "UpdateChecker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace freetunnel::update_staging {
// Where the verified installer is staged before it is executed. Not
// TempLocation: on Linux that is the shared /tmp, where another local user can
// pre-create (and therefore own) a fixed subdirectory — our chmod then silently
// fails, and they can plant a symlink under the asset name or overwrite the
// .AppImage/.deb after we verify it and before it runs. The per-user cache dir
// lives inside the user's own home on every platform and cannot be squatted.
QString updateStagingDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/updates");
}

#if defined(Q_OS_UNIX)
// The staging dir is one we made: a real directory, not a symlink pointed
// elsewhere, owned by us. Asked before staging a download and before clearing
// downloads out. CacheLocation follows XDG_CACHE_HOME, which can point somewhere
// shared, where another user could put a symlink in its place: staging would
// then write where they chose, and clearing out would delete every file of ours
// in the directory it points to. *st is filled in for the caller's own checks.
static bool stagingDirIsOurs(const QString &dir, struct stat *st)
{
    return ::lstat(QFile::encodeName(dir).constData(), st) == 0 && S_ISDIR(st->st_mode)
           && st->st_uid == ::geteuid();
}
#endif

// Create the staging dir and prove we got what we asked for: a real directory
// (not a symlink pointed elsewhere), owned by us, with nothing for group/other.
// Refuse to download at all rather than stage an executable somewhere another
// local user can reach.
bool prepareStagingDir(const QString &dir, QString *error)
{
    if (!QDir().mkpath(dir)) {
        *error = QCoreApplication::translate("UpdateChecker", "Could not create the update download directory");
        return false;
    }
    if (!QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                            | QFileDevice::ExeOwner)) {
        *error = QCoreApplication::translate("UpdateChecker", "Could not make the update download directory owner-only");
        return false;
    }
#if defined(Q_OS_UNIX)
    struct stat st = {};
    if (!stagingDirIsOurs(dir, &st) || (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        *error = QCoreApplication::translate("UpdateChecker", "The update download directory is not private to this user");
        return false;
    }
#endif
    return true;
}

// Create the installer file itself: owner-only from the moment it exists, and
// never through a pre-planted symlink (O_NOFOLLOW) or an existing file
// (O_EXCL) — the caller removes any leftover from a previous download first.
bool createInstallerFile(QFile &f, const QString &path)
{
#if defined(Q_OS_UNIX)
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    if (!f.open(fd, QIODevice::WriteOnly, QFile::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    return true;
#else
    // Windows: the file inherits the (owner-only) directory ACL; NewOnly gives
    // the same "must not exist already" guarantee as O_EXCL.
    return f.open(QIODevice::WriteOnly | QIODevice::NewOnly);
#endif
}

}

using namespace freetunnel::update_staging;

// Called once a run, before any download: whatever is staged by then is from an
// earlier run, and has been installed or never will be. An AppImage was copied
// out, an installer or disk image has run, and a .deb left waiting is offered
// again by the next check. Nothing else ever removed them, so each update left
// 100 MB or more behind for good — and a download kept under an asset name a
// later release no longer uses (the AppImage's, renamed) was not even replaced.
// The directory is ours alone (see prepareStagingDir), so all of it can go; a
// file still in use, as an installer still running on Windows, stays until the
// next start. One that is not ours is left alone, as a download would refuse it.
void UpdateChecker::discardStagedDownloads()
{
    const QString path = updateStagingDir();
#if defined(Q_OS_UNIX)
    struct stat st = {};
    if (!stagingDirIsOurs(path, &st))
        return;
#endif
    const QDir dir(path);
    for (const QString &name : dir.entryList(QDir::Files | QDir::Hidden | QDir::System))
        QFile::remove(dir.filePath(name));
}

