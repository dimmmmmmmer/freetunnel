// cppcheck-suppress-file missingIncludeSystem
#include "core/AppImagePath.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

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

QString runningAppImagePath()
{
#ifdef Q_OS_LINUX
    // /proc/self/exe is the kernel's own answer to "which file is this process
    // executing", which is why it is used here instead of
    // QCoreApplication::applicationFilePath() — the latter can fall back to argv[0].
    const QString exe = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    if (exe.isEmpty())
        return QString();

    QFile mounts(QStringLiteral("/proc/self/mountinfo"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString source =
            fuseMountSourceForPath(QString::fromUtf8(mounts.readAll()), exe);
    if (source.isEmpty())
        return QString();

    // squashfuse passes the archive as its device name, so for an AppImage the
    // source is the .AppImage file itself. Other FUSE filesystems put a label
    // there ("squashfuse", "gvfsd-fuse"); those are not regular files, and a
    // caller about to elevate must not be handed a guess.
    const QFileInfo info(source);
    if (!info.isFile())
        return QString();
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QString() : canonical;
#else
    return QString();
#endif
}

} // namespace freetunnel
