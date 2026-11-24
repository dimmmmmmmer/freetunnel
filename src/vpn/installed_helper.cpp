// cppcheck-suppress-file missingIncludeSystem
#include "vpn/installed_helper.h"
#include "core/ReleaseVerify.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <fcntl.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace freetunnel::installed_helper {
int launch(int argc, char **argv);

namespace {
constexpr qint64 kArchiveLimit = 768LL * 1024 * 1024;

QString version() { return QStringLiteral(FREETUNNEL_VERSION); }

bool protectedDirectory(const QString &path, bool create)
{
    if (path == QLatin1String("/")) return true;
    if (!protectedDirectory(QFileInfo(path).absolutePath(), create)) return false;
    const QByteArray name = QFile::encodeName(path);
    struct stat st{};
    if (::lstat(name.constData(), &st) != 0) {
        if (!create || ::mkdir(name.constData(), 0755) != 0) return false;
        if (::lstat(name.constData(), &st) != 0) return false;
    }
    return S_ISDIR(st.st_mode) && st.st_uid == 0 && (st.st_mode & 0022) == 0;
}

// Copy from a descriptor once, then verify/extract only the private snapshot.
// An unprivileged caller may replace or modify its file throughout this call;
// it cannot change the bytes after the signature/hash checks below.
bool snapshot(const QString &source, const QString &target, qint64 limit)
{
    const int fd = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    QFile input;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1
        || st.st_size < 0 || st.st_size > limit || !input.open(fd, QIODevice::ReadOnly, QFile::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    QFile output(target);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    qint64 total = 0;
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(65536);
        if (chunk.isEmpty() || (total += chunk.size()) > limit || output.write(chunk) != chunk.size()) return false;
    }
    return output.flush();
}

QByteArray readSmall(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(65537) : QByteArray();
}

bool run(const QString &program, const QStringList &args)
{
    QProcess process;
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(program, args);
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool checkTree(const QString &root)
{
    const QString prefix = root + QLatin1Char('/');
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        struct stat st{};
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0 || st.st_uid != 0) return false;
        if (S_ISLNK(st.st_mode)) {
            if (!QFileInfo(path).canonicalFilePath().startsWith(prefix)) return false;
        } else if (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)) {
            // No setuid/setgid bits, world/group writes, sockets or devices in
            // the installed runtime. Preserve executable bits of libraries/tools.
            if (::chmod(QFile::encodeName(path).constData(), st.st_mode & 0755) != 0) return false;
        } else return false;
    }
    return true;
}

QString rulePath(quint32 uid)
{
#ifdef Q_OS_MACOS
    return QStringLiteral("/private/etc/sudoers.d/freetunnel-helper-%1").arg(uid);
#else
    return QStringLiteral("/etc/sudoers.d/freetunnel-helper-%1").arg(uid);
#endif
}

bool grant(quint32 uid)
{
    if (!::getpwuid(uid) || !protectedDirectory(QFileInfo(rulePath(uid)).absolutePath(), true)) return false;
    // Validate a private temporary file before making it a sudoers include.
    QTemporaryDir staging(rootDirectory() + QStringLiteral("/grant-XXXXXX"));
    if (!staging.isValid()) return false;
    const QByteArray contents = sudoRule(uid).toUtf8();
    QFile draft(staging.filePath(QStringLiteral("rule")));
    if (contents.isEmpty() || !draft.open(QIODevice::WriteOnly) || draft.write(contents) != contents.size()) return false;
    draft.close();
    if (!run(QStringLiteral("/usr/sbin/visudo"), {QStringLiteral("-cf"), draft.fileName()})) return false;
    QSaveFile rule(rulePath(uid));
    if (!rule.open(QIODevice::WriteOnly)) return false;
    rule.setPermissions(QFile::ReadOwner | QFile::ReadGroup);
    if (contents.isEmpty() || rule.write(contents) != contents.size() || !rule.commit()) return false;
    if (!run(QStringLiteral("/usr/sbin/visudo"), {QStringLiteral("-cf"), rulePath(uid)})) {
        QFile::remove(rulePath(uid));
        return false;
    }
    return true;
}

class Lock {
public:
    Lock() {
        if (!protectedDirectory(QFileInfo(rootDirectory()).absolutePath(), true)) return;
        // The lock lives beside the runtime and survives removal. Unlinking a
        // held lock would let a concurrent installer lock a different inode.
        fd = ::open(QFile::encodeName(rootDirectory() + QStringLiteral(".lock")).constData(),
                    O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        struct stat st{};
        if (fd < 0 || ::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != 0
            || st.st_nlink != 1 || ::flock(fd, LOCK_EX | LOCK_NB) != 0
            || !protectedDirectory(rootDirectory(), true)) {
            if (fd >= 0) ::close(fd);
            fd = -1;
        }
    }
    ~Lock() { if (fd >= 0) ::close(fd); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    bool ok() const { return fd >= 0; }
private:
    int fd = -1;
};

bool activate(const QString &unpacked, const QString &hash, quint32 uid)
{
    // The GUI must be able to hash the installed executable, including a
    // runtime copied from the package into a private QTemporaryDir.
    if (::chmod(QFile::encodeName(unpacked).constData(), 0755) != 0) return false;
    const QString next = rootDirectory() + QStringLiteral("/release-") + hash;
    if (QFileInfo::exists(next)) {
        if (!checkTree(next) || sha256HexOfFile(next + QStringLiteral("/runtime/") + executableRelativePath()) != hash)
            return false;
    } else if (!QDir().rename(unpacked, next)) return false;
    // Authorization must succeed before changing the runtime used by existing
    // users. A failed installation remains recoverable without losing `current`.
    if (uid != 0 && !grant(uid)) return false;
    const QString link = rootDirectory() + QStringLiteral("/next");
    ::unlink(QFile::encodeName(link).constData());
    if (::symlink(QFile::encodeName(QFileInfo(next).fileName()).constData(), QFile::encodeName(link).constData()) != 0
        || ::rename(QFile::encodeName(link).constData(), QFile::encodeName(rootDirectory() + QStringLiteral("/current")).constData()) != 0)
        return false;
    const QDir root(rootDirectory());
    for (const QString &old : root.entryList({QStringLiteral("release-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (old != QFileInfo(next).fileName()) QDir(root.filePath(old)).removeRecursively();
    }
    return true;
}

#ifdef Q_OS_LINUX
bool installPackaged(quint32 uid)
{
    // Only an administrator or the package manager can activate the Debian payload.
    // The .deb has already installed this complete runtime under /opt. Copy it
    // while retaining the same activation/locking rules as signed updates.
    const QString source = QStringLiteral("/opt/freetunnel");
    const QString exe = QString::fromLatin1(kDebianExecutable);
    if (QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath() != exe
        || !protectedDirectory(source, false)) return false;
    const Lock lock;
    if (!lock.ok()) return false;
    QTemporaryDir stage(rootDirectory() + QStringLiteral("/package-XXXXXX"));
    if (!stage.isValid()) return false;
    if (!run(QStringLiteral("/bin/cp"), {QStringLiteral("-a"), QStringLiteral("--"), source,
                                        stage.path() + QStringLiteral("/runtime")})) return false;
    if (!checkTree(stage.path())) return false;
    const QString hash = sha256HexOfFile(exe);
    if (hash.isEmpty()) return false;
    return activate(stage.path(), hash, uid);
}
#endif

bool installSnapshot(const QStringList &args, bool update, quint32 uid, const QTemporaryDir &stage)
{
    const QString archive = stage.path() + QStringLiteral("/archive.tar.gz");
    const QString sumsPath = stage.path() + QStringLiteral("/sums");
    const QString sigPath = stage.path() + QStringLiteral("/signature");
    if (!snapshot(args[0], archive, kArchiveLimit) || !snapshot(args[1], sumsPath, 65536)
        || !snapshot(args[2], sigPath, 64)) return false;
    const QByteArray sums = readSmall(sumsPath);
    QString error;
    if (!verifyArchive(archive, sums, readSmall(sigPath), version(), !update, &error)) {
        QTextStream(stderr) << error << '\n';
        return false;
    }
    const QString unpacked = stage.path() + QStringLiteral("/unpacked");
    if (!QDir().mkdir(unpacked)) return false;
    // These are now bytes authenticated by our release key, never the caller's
    // mutable archive. CI packages a single `runtime/` directory.
    if (!run(QStringLiteral("/usr/bin/tar"), {QStringLiteral("-xzf"), archive,
             QStringLiteral("-C"), unpacked})) return false;
    if (!checkTree(unpacked)) return false;
    const QString exe = unpacked + QStringLiteral("/runtime/") + executableRelativePath();
    if (!QFileInfo(exe).isExecutable() || sha256HexOfFile(exe) != args[3]) return false;
    return activate(unpacked, args[3], uid);
}

bool install(const QStringList &args, bool update)
{
    // archive, signed manifest, signature, expected executable SHA-256, uid
    if (args.size() != 5) return false;
    quint32 uid = 0;
    if (!validUserId(args[4], &uid)) return false;
    if (update && qEnvironmentVariable("SUDO_UID") != args[4]) return false;
    if (args[3].size() != 64 || QByteArray::fromHex(args[3].toLatin1()).size() != 32) return false;
    const Lock lock;
    if (!lock.ok()) return false;
    QTemporaryDir stage(rootDirectory() + QStringLiteral("/stage-XXXXXX"));
    return stage.isValid() && installSnapshot(args, update, uid, stage);
}

bool ownToken(const QString &path, quint32 uid)
{
    struct stat st{};
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISREG(st.st_mode)
            && st.st_uid == uid && st.st_nlink == 1 && (st.st_mode & 0077) == 0;
}
void resetRootEnvironment()
{
    // sudo normally resets the environment; do not depend on a machine's
    // env_keep policy for Qt plugins, OpenSSL configuration or tar options.
    const QByteArray caller = qgetenv("SUDO_UID");
    for (const QString &key : QProcessEnvironment::systemEnvironment().keys())
        qunsetenv(key.toLocal8Bit().constData());
    if (!caller.isEmpty()) qputenv("SUDO_UID", caller);
    qputenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin");
#ifdef Q_OS_MACOS
    qputenv("HOME", "/var/root");
    qputenv("LANG", "en_US.UTF-8");
#else
    qputenv("HOME", "/root");
    qputenv("LANG", "C.UTF-8");
#endif
}

int startInstalled(const QStringList &args, const std::function<int()> &runSession)
{
    quint32 uid = 0;
    if (!validUserId(qEnvironmentVariable("SUDO_UID"), &uid)
        || !validStartArguments(args) || !ownToken(args[3], uid)) return 1;
    const Lock lock;
    return lock.ok() ? runSession() : 1;
}

bool authorize(const QStringList &args)
{
    quint32 uid = 0;
    const Lock lock;
    return lock.ok() && args.size() == 1 && validUserId(args[0], &uid)
            && QFileInfo(executablePath()).isExecutable() && grant(uid);
}

bool removeInstalled()
{
    const Lock lock;
    if (!lock.ok()) return false;
    const QDir rules(QFileInfo(rulePath(1)).absolutePath());
    for (const QString &name : rules.entryList({QStringLiteral("freetunnel-helper-*")}, QDir::Files)) {
        quint32 uid = 0;
        if (validUserId(name.mid(QStringLiteral("freetunnel-helper-").size()), &uid)
            && !QFile::remove(rules.filePath(name))) return false;
    }
    return QDir(rootDirectory()).removeRecursively();
}

int adminCommand(const QString &mode, const QStringList &args)
{
    if (mode == QLatin1String("--install-helper")) return install(args, false) ? 0 : 1;
#ifdef Q_OS_LINUX
    if (mode == QLatin1String("--install-packaged-helper")) {
        quint32 uid = 0;
        if (args.isEmpty()) validUserId(qEnvironmentVariable("SUDO_UID"), &uid);
        else if (args.size() != 1 || !validUserId(args.first(), &uid)) return 1;
        return installPackaged(uid) ? 0 : 1;
    }
#endif
    if (mode == QLatin1String("--authorize-helper")) return authorize(args) ? 0 : 1;
    if (mode == QLatin1String("--remove-helper")) return args.isEmpty() && removeInstalled() ? 0 : 1;
    return 1;
}
} // namespace

bool isCommand(const QString &arg)
{
    // sudoers matches a joined argument string, not individual argv elements.
    // A first argument such as "--installed-helper start" must also enter this
    // restricted dispatcher and be rejected, never fall through to GUI/--helper.
    return arg == QLatin1String("--launch-installed-helper") || arg.startsWith(QLatin1String("--installed-helper"))
            || arg == QLatin1String("--install-helper") || arg == QLatin1String("--remove-helper")
            || arg == QLatin1String("--authorize-helper") || arg == QLatin1String("--install-packaged-helper");
}

int runCommand(int argc, char **argv, const std::function<int()> &runSession)
{
    const QString mode = QString::fromLocal8Bit(argv[1]);
    if (mode == QLatin1String("--launch-installed-helper")) return launch(argc, argv);
    if (::geteuid() != 0) return 1;
    resetRootEnvironment();
    QStringList args;
    for (int i = 2; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    // Restrict the one passwordless entry point before any normal CLI dispatch.
    if (mode == QLatin1String("--installed-helper") && args.value(0) == QLatin1String("start"))
        return startInstalled(args.mid(1), runSession);
    // Maintenance arguments are data for our parser, never Qt command-line
    // options. Session starts above already validated every argument.
    int coreArgc = 1;
    char *coreArgv[] = {argv[0], nullptr};
    QCoreApplication app(coreArgc, coreArgv);
    if (mode == QLatin1String("--installed-helper")) {
        if (args == QStringList{QStringLiteral("check")}) return 0;
        if (args.value(0) != QLatin1String("update")) return 1;
        return install(args.mid(1), true) ? 0 : 1;
    }
    return adminCommand(mode, args);
}

} // namespace freetunnel::installed_helper
