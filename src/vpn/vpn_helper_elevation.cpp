// cppcheck-suppress-file missingIncludeSystem
// Elevation for VpnHelperClient: starting the helper with administrator rights
// (pkexec, then sudo, on Linux; osascript on macOS; ShellExecuteEx "runas" on
// Windows), the command line each of them is handed, and noticing when the
// prompt was refused. Split out of vpn_helper_client.cpp, which had grown past
// the point where one file could be read end to end.
#include "vpn/vpn_helper_client.h"
#include "vpn/vpn_helper_launch.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>

#include "core/AppImagePath.h" // runningAppImagePath (Linux elevation target)
#include "core/AppUiUtils.h" // shellEscape / appleScriptEscape (macOS)

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
namespace {

// What root runs an AppImage through, with the AppImage and the helper's
// arguments as "$@" — never part of this text, so no path can change what it says.
//
// Root runs the AppImage with extract-and-run, which also works where there is no
// FUSE — where this copy may itself have been started that way. But the runtime
// unpacks under $TMPDIR, and pkexec does not pass TMPDIR on, so root unpacked
// under a fixed name in the shared /tmp, which another user of the machine could
// prepare in advance. So the runtime gets a TMPDIR that root has just created for
// this run alone (mktemp -d: a new name, mode 0700), removed once the helper
// exits. Under /tmp because the runtime execs what it unpacks, and /run is
// mounted noexec on Debian and Ubuntu. Not a fixed directory of our own: a helper
// still running at shutdown is killed before the runtime cleans up, and /tmp is
// emptied at boot.
//
// The PATH comes first. pkexec sets a fixed one of its own, but sudo passes the
// user's on where secure_path is unset (as on Arch), and mktemp and rm run as
// root — as does the helper, whose core runs ip and resolvectl by name.
const char kUnpackWhereOnlyRootCanWrite[] =
        "PATH=/usr/sbin:/usr/bin:/sbin:/bin; export PATH\n"
        "d=$(mktemp -d /tmp/freetunnel-helper.XXXXXXXXXX) || exit 1\n"
        "TMPDIR=\"$d\" APPIMAGE_EXTRACT_AND_RUN=1 \"$@\"\n"
        "s=$?\n"
        "rm -rf -- \"$d\"\n"
        "exit $s\n";

} // namespace

// Declared in vpn_helper_launch.h and deliberately NOT in an anonymous namespace:
// these build and start what pkexec runs as root, and while they were file-local
// no test could reach them. External linkage here rather than a move to
// vpn_helper_launch.cpp, which would mean adding that source to nine test targets
// that already compile this one.
namespace freetunnel {

QStringList linuxHelperCommand(const QString &exe, const QString &appImage, quint16 port,
                               const QString &tokenPath)
{
    QStringList cmd;
    // `appImage` names the binary the user is about to authorize as root, so it
    // must come from the kernel (freetunnel::runningAppImagePath) and never from
    // $APPIMAGE/$APPDIR. Re-exec is needed at all because the running executable
    // lives inside a user-private FUSE mount that root cannot read, or, unpacked
    // without FUSE, in a directory its user owns; the AppImage file behind it is
    // a normal file that root can read and unpack for itself.
    if (!appImage.isEmpty()) {
        // Through a shell that gives the runtime a directory of root's own to
        // unpack the AppImage into: see kUnpackWhereOnlyRootCanWrite.
        cmd << QStringLiteral("/bin/sh") << QStringLiteral("-c")
            << QString::fromLatin1(kUnpackWhereOnlyRootCanWrite)
            << QStringLiteral("freetunnel-helper") << appImage;
    } else {
        cmd << exe;
    }
    cmd << QStringLiteral("--helper") << QStringLiteral("--port") << QString::number(port)
        << QStringLiteral("--token-file") << tokenPath;
    return cmd;
}

bool startLinuxElevation(QProcess *proc, const QString &elevator, const QStringList &helperCmd)
{
    QStringList args;
    if (elevator == QLatin1String("sudo"))
        args << QStringLiteral("--");
    args += helperCmd;
    // pkexec and sudo become the helper, so whatever is attached here is the
    // helper's stdout and stderr for as long as it runs — and with session
    // logging off, the core writes its log to stderr. Left as pipes, QProcess
    // read all of it into this process's memory, where nothing ever looked at it.
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());
    proc->start(elevator, args);
    if (!proc->waitForStarted(5000))
        return false;
    if (elevator == QLatin1String("pkexec") && proc->waitForFinished(1000))
        return false;
    return true;
}

} // namespace freetunnel
#endif

// Declared in vpn_helper_launch.h, for the same reason as linuxHelperCommand: what
// these build is run with administrator rights on the platforms most users are
// on, and while it was built inside the functions below no test could see it.
// Compiled everywhere, not only where it is used, because it is plain string
// work: this way it is checked wherever the tests run, Linux included.
namespace freetunnel {

QString macHelperElevationScript(const QString &exe, quint16 port, const QString &tokenPath)
{
    const QString inner =
            QStringLiteral("logf=$(mktemp \"${TMPDIR:-/tmp}/freetunnel-helper.XXXXXX\") || logf=/dev/null; "
                           "exec %1 --helper --port %2 --token-file %3 "
                           ">\"$logf\" 2>&1 &")
                    .arg(shellEscape(exe), QString::number(port), shellEscape(tokenPath));
    return QStringLiteral("do shell script \"%1\" with administrator privileges")
            .arg(appleScriptEscape(inner));
}

QString windowsHelperParameters(quint16 port, const QString &tokenPath)
{
    return QStringLiteral("--helper --port %1 --token-file \"%2\"").arg(QString::number(port), tokenPath);
}

} // namespace freetunnel

#if defined(Q_OS_MACOS)
static bool launchMacElevatedHelper(QProcess **procOut, QObject *parent, const QString &exe,
                                    quint16 port, const QString &tokenPath, QString *err)
{
    const QString script = freetunnel::macHelperElevationScript(exe, port, tokenPath);
    auto *proc = new QProcess(parent);
    proc->start(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
    if (!proc->waitForStarted(5000)) {
        if (err)
            *err = QObject::tr("Could not launch osascript");
        proc->deleteLater();
        return false;
    }
    *procOut = proc;
    return true;
}
#endif

#if defined(Q_OS_WIN)
static bool launchWinElevatedHelper(const QString &exe, quint16 port, const QString &tokenPath,
                                    QString *err)
{
    const QString exeDir = QFileInfo(exe).absolutePath();
    const QString args = freetunnel::windowsHelperParameters(port, tokenPath);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    // No SEE_MASK_NOCLOSEPROCESS. It makes ShellExecuteExW hand back a process
    // handle the caller then owns, and nothing here ever read it or closed it —
    // so every elevation attempt leaked one and pinned the exited helper's kernel
    // object for the life of this process. Asking for what is not used is the
    // whole of the cost; the value-initialised mask asks for nothing.
    sei.lpVerb = L"runas";
    const std::wstring wexe = exe.toStdWString();
    const std::wstring wargs = args.toStdWString();
    const std::wstring wdir = exeDir.toStdWString();
    sei.lpFile = wexe.c_str();
    sei.lpParameters = wargs.c_str();
    sei.lpDirectory = wdir.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        if (err)
            *err = QObject::tr("Elevation was cancelled or failed");
        return false;
    }
    return true;
}
#endif

bool VpnHelperClient::spawnElevatedHelper(quint16 port, const QString &tokenPath, QString *err) {
    const QString exe = QCoreApplication::applicationFilePath();

#if defined(Q_OS_MACOS)
    return launchMacElevatedHelper(&m_proc, this, exe, port, tokenPath, err);
#elif defined(Q_OS_WIN)
    return launchWinElevatedHelper(exe, port, tokenPath, err);
#else
    m_proc = new QProcess(this);
    const QStringList helperCmd =
            freetunnel::linuxHelperCommand(exe, freetunnel::runningAppImagePath(), port,
                                           tokenPath);

    if (freetunnel::startLinuxElevation(m_proc, QStringLiteral("pkexec"), helperCmd))
        return true;

    m_proc->deleteLater();
    m_proc = new QProcess(this);

    if (freetunnel::startLinuxElevation(m_proc, QStringLiteral("sudo"), helperCmd))
        return true;

    if (err) {
        *err = tr("Could not start the VPN helper — authorization may have been "
                  "declined, or pkexec/sudo elevation failed.");
    }
    return false;
#endif
}

// Notice when the elevation prompt was answered with "no".
//
// Reaching the helper is a poll — a quarter-second apart, for sixty seconds,
// because the prompt blocks the helper from starting until someone has finished
// typing a password. Nothing watched the launcher itself, so a cancelled prompt
// looked exactly like a slow one: the launcher exited at once and the interface
// sat on "Connecting…" for the full minute before saying anything.
void VpnHelperClient::watchElevationOutcome()
{
    if (!m_proc)
        return; // Windows elevates through ShellExecuteEx, which already says no
    connect(m_proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
                if (!m_starting)
                    return; // already connected, or torn down
                if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState)
                    return;
#if defined(Q_OS_MACOS)
                // osascript's job is to put the helper in the background and
                // leave; finishing cleanly says nothing about whether the helper
                // came up, and the poll is still the thing that decides. A
                // refusal is what it reports as an error — "User canceled" is
                // AppleScript error -128.
                if (status == QProcess::NormalExit && code == 0)
                    return;
#else
                // pkexec and sudo exec INTO the helper (for an AppImage, into a
                // shell that waits for it), so they last exactly as long as it
                // does. Either of them exiting before the connection is made
                // means there is nothing left to connect to, whatever the code.
                Q_UNUSED(code)
                Q_UNUSED(status)
#endif
                if (m_attempt) {
                    m_attempt->stop();
                    m_attempt->deleteLater();
                    m_attempt = nullptr;
                }
                fail(tr("The VPN helper didn't start — authorization was declined, "
                        "or the elevation failed."));
            });
}
