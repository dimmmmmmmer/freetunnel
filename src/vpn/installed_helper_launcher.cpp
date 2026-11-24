// cppcheck-suppress-file missingIncludeSystem
#include "vpn/installed_helper.h"
#include "vpn/vpn_helper_launch.h"
#include "core/AppImagePath.h"
#include "core/ReleaseVerify.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <unistd.h>

namespace freetunnel::installed_helper {
namespace {
void progress(const char *state) { QTextStream(stdout) << state << Qt::endl; }

#ifdef Q_OS_MACOS
QString quote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
#endif

int execute(const QString &program, const QStringList &args, int timeout = -1)
{
    QProcess process;
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(program, args);
    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished();
        return 1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}

int sudoHelper(const QStringList &args, int timeout = -1)
{
    return execute(QStringLiteral("/usr/bin/sudo"),
                   QStringList{QStringLiteral("-n"), QStringLiteral("--"), executablePath(),
                               QStringLiteral("--installed-helper")} + args, timeout);
}

int elevated(const QStringList &operation)
{
    const QString exe = QCoreApplication::applicationFilePath();
#ifdef Q_OS_MACOS
    const QStringList command = QStringList{exe} + operation;
#else
    QStringList command = linuxHelperCommand(exe, runningAppImagePath(), 1, {});
    command.erase(command.end() - 5, command.end());
    command += operation;
#endif
    // Respect existing administrator authorization (including managed hosts).
    // sudo checks the exact command. An ordinary first run uses the OS dialog.
    if (execute(QStringLiteral("/usr/bin/sudo"), {QStringLiteral("-n"), QStringLiteral("-v")}, 5000) == 0
        && execute(QStringLiteral("/usr/bin/sudo"), QStringList{QStringLiteral("-n"), QStringLiteral("--")} + command, 120000) == 0)
        return 0;
#ifdef Q_OS_MACOS
    QString shellCommand;
    for (const QString &part : command) shellCommand += QLatin1Char(' ') + quote(part);
    shellCommand.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    shellCommand.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return execute(QStringLiteral("/usr/bin/osascript"),
                   {QStringLiteral("-e"), QStringLiteral("do shell script \"%1\" with administrator privileges").arg(shellCommand)}, 180000);
#else
    return execute(QStringLiteral("/usr/bin/pkexec"), command, 180000);
#endif
}

QDir bundledHelper()
{
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/vpn-helper"));
#else
    return QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../share/freetunnel/vpn-helper"));
#endif
}

QByteArray readSmall(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(65537) : QByteArray();
}

int authorizeAndStart(const QStringList &args, const QString &uid)
{
    if (sudoHelper({QStringLiteral("check")}, 5000) != 0
        && elevated({QStringLiteral("--authorize-helper"), uid}) != 0) return 1;
    progress("starting");
    return sudoHelper(QStringList{QStringLiteral("start")} + args);
}

int transient(const QStringList &args)
{
    progress("starting");
#ifdef Q_OS_MACOS
    return execute(QStringLiteral("/usr/bin/osascript"),
                   {QStringLiteral("-e"), macHelperElevationScript(QCoreApplication::applicationFilePath(),
                                                                  args[1].toUShort(), args[3])});
#else
    QProcess process;
    const QStringList command = linuxHelperCommand(QCoreApplication::applicationFilePath(),
                                                   runningAppImagePath(), args[1].toUShort(), args[3]);
    if (!startLinuxElevation(&process, QStringLiteral("pkexec"), command)
        && !startLinuxElevation(&process, QStringLiteral("sudo"), command)) return 1;
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
#endif
}

int launchWithoutBundle(const QStringList &args, const QString &uid, const QString &installedHash)
{
    const QFileInfo self(QCoreApplication::applicationFilePath());
    if (!installedHash.isEmpty() && installedHash == sha256HexOfFile(self.absoluteFilePath()))
        return authorizeAndStart(args, uid);
#ifdef Q_OS_LINUX
    // dpkg can replace the GUI while a session holds the installed helper
    // lock. Retry its deferred postinst using the root-owned package locally.
    if (self.canonicalFilePath() == QLatin1String(kDebianExecutable)) {
        progress("installing");
        if (elevated({QStringLiteral("--install-packaged-helper"), uid}) != 0) return 1;
        return authorizeAndStart(args, uid);
    }
#endif
    return transient(args);
}
} // namespace

int launch(int argc, char **argv)
{
    if (::geteuid() == 0) return 1;
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(2);
    if (!validStartArguments(args)) return 1;
    if (!QFileInfo(QStringLiteral("/usr/bin/sudo")).isExecutable()
        || !QFileInfo(QStringLiteral("/usr/sbin/visudo")).isExecutable())
        return transient(args);
    const QString uid = QString::number(::getuid());
    const QString installedHash = sha256HexOfFile(executablePath());
    const QDir bundle = bundledHelper();
    if (!bundle.exists()) return launchWithoutBundle(args, uid, installedHash);
    const QString sumsName = QStringLiteral("SHA256SUMS.txt");
    const QString sigName = QStringLiteral("SHA256SUMS.txt.sig");
    const QByteArray sums = readSmall(bundle.filePath(sumsName));
    const QByteArray sig = readSmall(bundle.filePath(sigName));
    // Signing the outer .app seals its new resources and changes its Mach-O
    // hash. The manifest identifies the executable inside the helper archive.
    const QString expected = expectedSha256FromSums(sums, QStringLiteral("helper-executable"));
    if (versionFromSums(sums) != QStringLiteral(FREETUNNEL_VERSION) || expected.size() != 64
        || !verifyHelperManifest(sums, sig, true))
        return 1;
    if (installedHash == expected) return authorizeAndStart(args, uid);

    progress("preparing");
    // A root process cannot read a user-private AppImage FUSE mount. Stage
    // ordinary files before elevation. Root verifies its own private snapshot.
    QTemporaryDir cache;
    if (!cache.isValid()) return 1;
    for (const QString &name : {assetName(), sumsName, sigName}) {
        if (!QFile::copy(bundle.filePath(name), cache.filePath(name))) return 1;
    }
    if (!verifyFileAgainstSums(cache.filePath(assetName()), sums, assetName())) return 1;
    progress("installing");
    const QStringList installation{cache.filePath(assetName()), cache.filePath(sumsName),
                                   cache.filePath(sigName), expected, uid};
    if (QFileInfo::exists(executablePath()) && sudoHelper({QStringLiteral("check")}, 5000) == 0) {
        if (sudoHelper(QStringList{QStringLiteral("update")} + installation, 120000) != 0)
            return transient(args);
    } else if (elevated(QStringList{QStringLiteral("--install-helper")} + installation) != 0) return 1;
    if (sha256HexOfFile(executablePath()) != expected) return 1;
    progress("starting");
    return sudoHelper(QStringList{QStringLiteral("start")} + args);
}
} // namespace freetunnel::installed_helper
