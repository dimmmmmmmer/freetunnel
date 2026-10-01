// cppcheck-suppress-file missingIncludeSystem
#include "vpn/vpn_helper_launch.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace freetunnel {

namespace {

// What the GUI calls its token file: QTemporaryFile's ".fthelper-XXXXXX", in its
// own config directory (VpnHelperClient::configureProductionHelper).
const QLatin1String kTokenFilePrefix(".fthelper-");

// The token is 32 hex digits. A file holding more than this is not one the GUI
// wrote, and the bound is what stops a file that never ends from being read
// for ever.
constexpr qint64 kMaxTokenFileBytes = 128;

// Open @p path only if it is a file in its own right: not a symlink to another,
// not a pipe or a device, and — on POSIX — not a second name for a file someone
// else owns (a hard link). There the answer comes from the descriptor that is
// then read, so a name swapped after the check is not followed; O_NONBLOCK keeps
// a pipe from holding the open itself until it is turned away.
bool openOwnFile(QFile &f, const QString &path)
{
#if defined(Q_OS_UNIX)
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st = {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1
        || !f.open(fd, QIODevice::ReadOnly, QFile::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    return true;
#else
    const QFileInfo fi(path);
    if (fi.isSymLink() || !fi.isFile())
        return false;
    f.setFileName(path);
    return f.open(QIODevice::ReadOnly);
#endif
}

} // namespace

HelperLaunchConfig parseHelperLaunchArgs(const QStringList &args)
{
    HelperLaunchConfig cfg;
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args[i] == QLatin1String("--port"))
            cfg.port = args[i + 1].toUShort();
        else if (args[i] == QLatin1String("--token-file"))
            cfg.token = readHelperTokenFile(args[i + 1]);
    }
    return cfg;
}

QString readHelperTokenFile(const QString &path)
{
    // This runs elevated — as root, or as Administrator — on whatever follows
    // --token-file, and our GUI is not the only thing that can put it there:
    // anything running as the user can start this same binary with arguments of
    // its own, and the prompt the user then answers is the genuine one. It used
    // to read whatever it was named, to the end, and then delete it. So: only
    // what could be one of the GUI's token files, only as far as a token goes,
    // and nothing is deleted at all — the GUI removes its own file, as the user,
    // once the helper has answered or the attempt is given up. A check on the
    // name would have made the delete safe on Linux and macOS, but not on
    // Windows, where a folder in the path can be made to lead to any other file.
    QFile f;
    if (!QFileInfo(path).fileName().startsWith(kTokenFilePrefix) || !openOwnFile(f, path))
        return QString();
    const QByteArray body = f.read(kMaxTokenFileBytes + 1);
    if (body.size() > kMaxTokenFileBytes)
        return QString();
    return QString::fromUtf8(body).trimmed();
}

} // namespace freetunnel
