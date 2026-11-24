// cppcheck-suppress-file missingIncludeSystem
#include "core/AppImagePath.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#ifdef Q_OS_LINUX
#include <QSet>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#endif

namespace freetunnel {

namespace {

// mountinfo escapes space, tab, newline and backslash as three-digit octal so a
// mount point containing them still occupies exactly one field.
QString unescapeMountinfoField(const QString &field)
{
    QString out;
    out.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            bool ok = false;
            const int code = QStringView(field).mid(i + 1, 3).toInt(&ok, 8);
            if (ok && code > 0 && code < 256) {
                out.append(QChar(code));
                i += 3;
                continue;
            }
        }
        out.append(field.at(i));
    }
    return out;
}

// True when `dir` is `path` itself or one of its ancestor directories. Compared
// per path component: "/usr" must not count as containing "/usrlocal/bin/x".
bool mountPointContains(const QString &dir, const QString &path)
{
    if (dir == QLatin1String("/"))
        return path.startsWith(QLatin1Char('/'));
    if (path == dir)
        return true;
    return path.startsWith(dir) && path.size() > dir.size()
            && path.at(dir.size()) == QLatin1Char('/');
}

// The MD5 in the name of the extract-and-run directory `path` lies under, or an
// empty string. The runtime unpacks to $TMPDIR/appimage_extracted_<MD5 of the
// AppImage, lowercase hex>; the nearest such ancestor is the one this came from.
QString extractionDigest(const QString &path)
{
    static const QRegularExpression dirName(
            QStringLiteral("^appimage_extracted_([0-9a-f]{32})$"));
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (auto it = parts.crbegin(); it != parts.crend(); ++it) {
        const QRegularExpressionMatch match = dirName.match(*it);
        if (match.hasMatch())
            return match.captured(1);
    }
    return QString();
}

#ifdef Q_OS_LINUX
// Type 2's FUSE daemon runs the AppImage itself. It holds /dev/fuse and
// writes to a keepalive pipe inherited by AppRun. Neither a filename in the
// working directory nor an environment variable identifies that daemon.
QSet<quint64> runtimePipes()
{
    QSet<quint64> pipes;
    for (const QString &name : QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System)) {
        bool ok = false;
        const int fd = name.toInt(&ok);
        struct stat st{};
        if (ok && ::fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode)
            && (::fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY)
            pipes.insert(st.st_ino);
    }
    return pipes;
}

bool matchesFuseConnection(const QString &infoPath, unsigned int device)
{
    QFile info(infoPath);
    if (!info.open(QIODevice::ReadOnly)) return false;
    for (const QByteArray &line : info.readAll().split('\n')) {
        if (line.startsWith("fuse_connection:"))
            return line.mid(sizeof("fuse_connection:") - 1).trimmed() == QByteArray::number(device);
    }
    // Older kernels only expose the keepalive pipe shared with AppRun.
    return true;
}

bool servesRuntime(const QString &process, const QSet<quint64> &pipes, unsigned int device)
{
    const QDir directory(process + QStringLiteral("/fd"));
    bool sharedPipe = false;
    bool fuse = false;
    for (const QString &name : directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System)) {
        const QString path = directory.filePath(name);
        struct stat st{};
        if (::stat(QFile::encodeName(path).constData(), &st) != 0) continue;
        if (S_ISFIFO(st.st_mode) && pipes.contains(st.st_ino)) sharedPipe = true;
        if (S_ISCHR(st.st_mode) && QFile::symLinkTarget(path) == QLatin1String("/dev/fuse")
            && matchesFuseConnection(process + QStringLiteral("/fdinfo/") + name, device)) fuse = true;
    }
    return fuse && sharedPipe;
}

QString imageForMountLabel(const QString &label, const QString &exe)
{
    // The runtime retains its mount root at fd 1023 when it execs AppRun.
    struct stat root{};
    struct stat binary{};
    if (::fstat(1023, &root) != 0 || !S_ISDIR(root.st_mode)
        || ::stat(QFile::encodeName(exe).constData(), &binary) != 0 || binary.st_dev != root.st_dev)
        return {};
    const auto pipes = runtimePipes();
    if (pipes.isEmpty() || label.contains(QLatin1Char('/'))) return {};
    QString image;
    const QDir proc(QStringLiteral("/proc"));
    for (const QString &pid : proc.entryList({QStringLiteral("[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString process = proc.filePath(pid);
        const QFileInfo candidate(QFile::symLinkTarget(process + QStringLiteral("/exe")));
        if (candidate.fileName() != label || !candidate.isFile() || !servesRuntime(process, pipes, minor(root.st_dev)))
            continue;
        const QString path = candidate.canonicalFilePath();
        if (!image.isEmpty() && image != path) return {};
        image = path;
    }
    return image;
}

// The AppImage behind a FUSE mount containing `exe`, as the kernel names it.
QString mountedAppImagePath(const QString &exe)
{
    QFile mounts(QStringLiteral("/proc/self/mountinfo"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString source =
            fuseMountSourceForPath(QString::fromUtf8(mounts.readAll()), exe);
    if (source.isEmpty())
        return QString();

    // Some FUSE runtimes record the full path. Our type 2 runtime records
    // only the basename, even when the AppImage was launched by absolute path.
    const QFileInfo info(source);
    if (!info.isAbsolute()) return imageForMountLabel(source, exe);
    if (!info.isFile())
        return QString();
    return info.canonicalFilePath();
}

// The AppImage the runtime unpacked `exe` from. extract-and-run forks, execs the
// payload's AppRun in the child and waits, so the runtime — the AppImage file
// itself, to the kernel — is this process's parent.
QString extractedAppImagePath(const QString &exe)
{
    // Hashing the whole AppImage is the slow part, and neither where this
    // executable is nor which process started it changes while it runs. It
    // happens once, on whichever thread asks first, which is the GUI's. An empty
    // answer is kept too: an AppImage replaced or removed before then is not
    // looked for again, and this copy is taken for an ordinary install, as it
    // was before this existed.
    static const QString path = extractedAppImageSource(
            exe, QFileInfo(QStringLiteral("/proc/%1/exe").arg(::getppid())).canonicalFilePath());
    return path;
}
#endif

} // namespace

QString fuseMountSourceForPath(const QString &mountinfo, const QString &path)
{
    QString bestSource;
    int bestMountPointLength = -1;

    const QStringList lines = mountinfo.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // Layout: id parent major:minor root mountPoint options [optional...] - fstype source superOptions
        // The optional-fields section is variable length and terminated by a lone
        // "-", so the tail can only be located relative to that separator.
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const int sep = fields.indexOf(QStringLiteral("-"));
        if (sep < 6 || fields.size() < sep + 3)
            continue;

        const QString mountPoint = unescapeMountinfoField(fields.at(4));
        const QString fsType = fields.at(sep + 1);
        // The AppImage runtime mounts its payload over FUSE (squashfuse, or dwarfs
        // on some builds). Anything else is a normal filesystem and its "source" is
        // a block device, which is not what we are looking for.
        if (!fsType.startsWith(QLatin1String("fuse")))
            continue;
        if (!mountPointContains(mountPoint, path))
            continue;
        // Mounts nest and mountinfo lists them in mount order, so the deepest
        // matching mount point is the one the file actually lives in.
        if (mountPoint.size() <= bestMountPointLength)
            continue;
        bestMountPointLength = mountPoint.size();
        bestSource = unescapeMountinfoField(fields.at(sep + 2));
    }
    return bestSource;
}

QString extractedAppImageSource(const QString &exe, const QString &candidate)
{
    const QString digest = extractionDigest(exe);
    if (digest.isEmpty() || !QFileInfo(candidate).isFile())
        return QString();
    // The directory's name only says which file the payload came from; the
    // file's content is what proves the candidate is that file.
    QFile file(candidate);
    QCryptographicHash md5(QCryptographicHash::Md5);
    if (!file.open(QIODevice::ReadOnly) || !md5.addData(&file))
        return QString();
    if (QString::fromLatin1(md5.result().toHex()) != digest)
        return QString();
    return QFileInfo(candidate).canonicalFilePath();
}

RunningAppImage runningAppImage()
{
#ifdef Q_OS_LINUX
    // /proc/self/exe is the kernel's own answer to "which file is this process
    // executing", which is why it is used here instead of
    // QCoreApplication::applicationFilePath() — the latter can fall back to argv[0].
    const QString exe = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    if (exe.isEmpty())
        return {};
    static const QString mounted = mountedAppImagePath(exe);
    if (!mounted.isEmpty())
        return {mounted, false};
    const QString extracted = extractedAppImagePath(exe);
    return {extracted, !extracted.isEmpty()};
#else
    return {};
#endif
}

QString runningAppImagePath()
{
    return runningAppImage().path;
}

RunningAppImage updatableAppImage()
{
#ifdef FT_ENABLE_TEST_HOOKS
    const QString override = qEnvironmentVariable("FT_TEST_UPDATER_APPIMAGE");
    if (!override.isEmpty()) {
        const bool extracted =
                qEnvironmentVariable("FT_TEST_UPDATER_APPIMAGE_EXTRACTED") == QLatin1String("1");
        return {override, extracted};
    }
#endif
    return runningAppImage();
}

} // namespace freetunnel
