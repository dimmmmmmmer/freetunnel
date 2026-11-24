// cppcheck-suppress-file missingIncludeSystem
#include "vpn/installed_helper.h"
#include "vpn/vpn_helper_launch.h"
#include "core/AppImagePath.h"
#include "core/ReleaseVerify.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
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
#ifdef Q_OS_MACOS
    QString command = quote(QCoreApplication::applicationFilePath());
    for (const QString &arg : operation) command += QLatin1Char(' ') + quote(arg);
    command.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    command.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return execute(QStringLiteral("/usr/bin/osascript"),
                   {QStringLiteral("-e"), QStringLiteral("do shell script \"%1\" with administrator privileges").arg(command)});
#else
    QStringList command = linuxHelperCommand(QCoreApplication::applicationFilePath(), runningAppImagePath(), 1, {});
    command.erase(command.end() - 5, command.end());
    command += operation;
    return execute(QStringLiteral("/usr/bin/pkexec"), command);
#endif
}

bool download(QNetworkAccessManager &manager, const QUrl &url, const QString &path, qint64 limit)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = manager.get(request);
    reply->setReadBufferSize(65536);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { delete reply; return false; }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    qint64 total = 0;
    bool failed = false;
    const auto drain = [&] {
        const QByteArray chunk = reply->readAll();
        if ((total += chunk.size()) > limit || file.write(chunk) != chunk.size()) {
            failed = true;
            reply->abort();
        }
    };
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, drain);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(60000);
    loop.exec();
    drain();
    const bool ok = !failed && reply->error() == QNetworkReply::NoError
            && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && file.flush();
    delete reply;
    return ok;
}

QByteArray readManifestFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(65537) : QByteArray();
}

enum class Preparation { Ready, Unavailable, Refused };

Preparation prepareRuntime(const QTemporaryDir &cache, const QString &ownHash)
{
    const QString base = QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/download/helper-%1/")
            .arg(QStringLiteral(FREETUNNEL_VERSION));
    QNetworkAccessManager manager;
    const QString sums = cache.filePath(QStringLiteral("SHA256SUMS.txt"));
    const QString sig = cache.filePath(QStringLiteral("SHA256SUMS.txt.sig"));
    const QString archive = cache.filePath(assetName());
    if (!download(manager, QUrl(base + QStringLiteral("SHA256SUMS.txt")), sums, 65536)
        || !download(manager, QUrl(base + QStringLiteral("SHA256SUMS.txt.sig")), sig, 64)
        || !download(manager, QUrl(base + assetName()), archive, 768LL * 1024 * 1024))
        return Preparation::Unavailable;
    QString error;
    if (!verifyArchive(archive, readManifestFile(sums), readManifestFile(sig),
                       QStringLiteral(FREETUNNEL_VERSION), true, &error))
        return Preparation::Refused;
    // A locally built executable may share the published version number. Check
    // compatibility as the user, before presenting an administrator prompt.
    // Root independently repeats all verification on its own private copy.
    const QString unpacked = cache.filePath(QStringLiteral("unpacked"));
    if (!QDir().mkdir(unpacked)
        || execute(QStringLiteral("/usr/bin/tar"), {QStringLiteral("-xzf"), archive,
                   QStringLiteral("-C"), unpacked}, 120000) != 0) return Preparation::Refused;
    return sha256HexOfFile(unpacked + QStringLiteral("/runtime/") + executableRelativePath()) == ownHash
            ? Preparation::Ready : Preparation::Unavailable;
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
    const QString ownHash = sha256HexOfFile(app.applicationFilePath());
    if (ownHash.isEmpty()) return 1;
    const QString uid = QString::number(::getuid());
    if (sha256HexOfFile(executablePath()) == ownHash) return authorizeAndStart(args, uid);
    // Only release builds can install a permanent runtime. Development builds
    // keep the existing prompted helper; they must never silently replace the
    // administrator's installed copy with unsigned code.
    progress("preparing");
    QTemporaryDir cache;
    if (!cache.isValid()) return 1;
    const Preparation prepared = prepareRuntime(cache, ownHash);
    if (prepared == Preparation::Unavailable) return transient(args);
    if (prepared == Preparation::Refused) return 1;
    progress("installing");
    const QStringList installation{cache.filePath(assetName()), cache.filePath(QStringLiteral("SHA256SUMS.txt")),
                                   cache.filePath(QStringLiteral("SHA256SUMS.txt.sig")), ownHash, uid};
    if (QFileInfo::exists(executablePath()) && sudoHelper({QStringLiteral("check")}, 5000) == 0) {
        if (sudoHelper(QStringList{QStringLiteral("update")} + installation, 120000) != 0)
            return transient(args);
    } else if (elevated(QStringList{QStringLiteral("--install-helper")} + installation) != 0) return 1;
    if (sha256HexOfFile(executablePath()) != ownHash) return 1;
    progress("starting");
    return sudoHelper(QStringList{QStringLiteral("start")} + args);
}
} // namespace freetunnel::installed_helper
