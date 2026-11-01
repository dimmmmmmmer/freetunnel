// cppcheck-suppress-file missingIncludeSystem
// Elevation for VpnHelperClient: starting the helper with administrator rights
// (pkexec, then sudo, on Linux; osascript on macOS; on Windows the installed
// helper service, or ShellExecuteEx "runas" where there is none), the command
// line each of them is handed, and noticing when the prompt was refused. Split out of vpn_helper_client.cpp, which had grown past
// the point where one file could be read end to end.
#include "vpn/vpn_helper_client.h"
#include "vpn/vpn_helper_launch.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>

#include "core/AppImagePath.h" // runningAppImagePath (Linux elevation target)
#include "core/AppUiUtils.h" // shellEscape / appleScriptEscape (macOS)

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <winsvc.h>

#include <string>
#include <vector>
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

QStringList helperServiceArguments(quint16 port, const QString &tokenPath)
{
    return {QStringLiteral("--port"), QString::number(port), QStringLiteral("--token-file"),
            tokenPath};
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

#if defined(Q_OS_WIN)
namespace {

// Closes a service-manager handle when it goes out of scope.
class ScHandle {
public:
    explicit ScHandle(SC_HANDLE h) : m_h(h) {}
    ~ScHandle()
    {
        if (m_h)
            CloseServiceHandle(m_h);
    }
    ScHandle(const ScHandle &) = delete;
    ScHandle &operator=(const ScHandle &) = delete;
    SC_HANDLE get() const { return m_h; }
    explicit operator bool() const { return m_h != nullptr; }

private:
    SC_HANDLE m_h;
};

// How long a connect waits for an earlier run of the service to finish taking its
// tunnel down: the app closed and opened again at once, say. The helper quits as
// soon as its GUI goes, so this is normally over before it starts. Past it the
// service is someone else's — another account's session on this machine — and
// the connect goes through UAC instead, as it did before there was a service.
constexpr int kServiceBusyWaitMs = 3000;

// *error is GetLastError() as OpenServiceW left it, before closing the manager's
// handle can change it.
ScHandle openHelperService(DWORD access, DWORD *error = nullptr)
{
    const ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm) {
        if (error)
            *error = GetLastError();
        return ScHandle(nullptr);
    }
    const std::wstring name = QString::fromLatin1(freetunnel::kHelperServiceName).toStdWString();
    SC_HANDLE svc = OpenServiceW(scm.get(), name.c_str(), access);
    if (!svc && error)
        *error = GetLastError();
    return ScHandle(svc);
}

// The program the service runs: the first word of its command line.
QString helperServiceProgram(SC_HANDLE svc)
{
    DWORD needed = 0;
    if (QueryServiceConfigW(svc, nullptr, 0, &needed) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return QString();
    std::vector<BYTE> buf(needed);
    auto *cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buf.data());
    if (!QueryServiceConfigW(svc, cfg, needed, &needed) || !cfg->lpBinaryPathName)
        return QString();
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(cfg->lpBinaryPathName, &argc);
    if (!argv)
        return QString();
    const QString program = argc > 0 ? QString::fromWCharArray(argv[0]) : QString();
    LocalFree(argv);
    return program;
}

bool isSameFile(const QString &a, const QString &b)
{
    const QString ca = QFileInfo(a).canonicalFilePath();
    return !ca.isEmpty() && ca.compare(QFileInfo(b).canonicalFilePath(), Qt::CaseInsensitive) == 0;
}

bool waitUntilServiceStopped(SC_HANDLE svc, int timeoutMs)
{
    const QDeadlineTimer deadline(timeoutMs);
    for (;;) {
        SERVICE_STATUS st{};
        if (!QueryServiceStatus(svc, &st))
            return false;
        if (st.dwCurrentState == SERVICE_STOPPED)
            return true;
        if (deadline.hasExpired())
            return false;
        Sleep(100);
    }
}

enum class ServiceStart { Started, NotInstalled, Unusable };

// Start the helper through the service the installer registered, which runs it
// as LocalSystem with no UAC prompt. It gets the same port and token file a
// prompted helper gets on its command line, and goes through the same handshake.
//
// Only the service of this very install: one registered by another copy of
// FreeTunnel (an older install elsewhere, while this one runs from a folder of
// its own) would start a helper this GUI may not speak to. *why says why a
// service that is there could not be used.
ServiceStart startHelperService(const QString &exe, quint16 port, const QString &tokenPath,
                                QString *why)
{
    DWORD e = ERROR_SUCCESS;
    const ScHandle svc =
            openHelperService(SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG, &e);
    if (!svc) {
        if (e == ERROR_SERVICE_DOES_NOT_EXIST)
            return ServiceStart::NotInstalled;
        *why = QStringLiteral("could not open it (error %1)").arg(e);
        return ServiceStart::Unusable;
    }
    if (!isSameFile(helperServiceProgram(svc.get()), exe)) {
        *why = QStringLiteral("it belongs to another copy of FreeTunnel");
        return ServiceStart::Unusable;
    }
    if (!waitUntilServiceStopped(svc.get(), kServiceBusyWaitMs)) {
        *why = QStringLiteral("it is already running");
        return ServiceStart::Unusable;
    }
    const QStringList args = freetunnel::helperServiceArguments(port, tokenPath);
    std::vector<std::wstring> wide;
    wide.reserve(args.size());
    for (const QString &a : args)
        wide.push_back(a.toStdWString());
    std::vector<LPCWSTR> argv;
    argv.reserve(wide.size());
    for (const std::wstring &w : wide)
        argv.push_back(w.c_str());
    if (!StartServiceW(svc.get(), static_cast<DWORD>(argv.size()), argv.data())) {
        *why = QStringLiteral("it did not start (error %1)").arg(GetLastError());
        return ServiceStart::Unusable;
    }
    return ServiceStart::Started;
}

} // namespace
#endif

bool VpnHelperClient::spawnElevatedHelper(quint16 port, const QString &tokenPath, QString *err) {
    const QString exe = QCoreApplication::applicationFilePath();

#if defined(Q_OS_MACOS)
    return launchMacElevatedHelper(&m_proc, this, exe, port, tokenPath, err);
#elif defined(Q_OS_WIN)
    QString why;
    switch (startHelperService(exe, port, tokenPath, &why)) {
    case ServiceStart::Started:
        m_viaService = true;
        emit connectionInfo(QStringLiteral("Starting the VPN helper service"));
        return true;
    case ServiceStart::Unusable:
        emit connectionInfo(
                QStringLiteral("VPN helper service not used (%1); asking for administrator rights")
                        .arg(why));
        break;
    case ServiceStart::NotInstalled:
        break; // a portable or development build: UAC, as it always was
    }
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
        return; // Windows: the service is watched by helperServiceStoppedEarly, and
                // ShellExecuteEx already says no
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

// Whether the helper service this attempt started has stopped again before the
// GUI reached it. Polled with the connection attempts: the helper runs hidden as
// LocalSystem, so a helper that gave up at once (a token file it would not read,
// say) left nothing for the GUI to notice but a port nobody ever opened, and the
// window sat on "Connecting…" for the full minute.
bool VpnHelperClient::helperServiceStoppedEarly(QString *why) const
{
#if defined(Q_OS_WIN)
    if (!m_viaService)
        return false;
    const ScHandle svc = openHelperService(SERVICE_QUERY_STATUS);
    SERVICE_STATUS st{};
    if (!svc || !QueryServiceStatus(svc.get(), &st) || st.dwCurrentState != SERVICE_STOPPED)
        return false;
    if (why) {
        const DWORD code = st.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR
                ? st.dwServiceSpecificExitCode
                : st.dwWin32ExitCode;
        *why = QStringLiteral(" (VPN helper service exit code %1)").arg(code);
    }
    return true;
#else
    Q_UNUSED(why)
    return false;
#endif
}
