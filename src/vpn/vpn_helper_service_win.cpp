// cppcheck-suppress-file missingIncludeSystem
// The helper as a Windows service: FreeTunnel --service, which the installer
// registers as FreeTunnelHelper (win/installer.nsi) to run as LocalSystem.
//
// Its point is the prompt it does not show. The helper used to be started with
// ShellExecuteEx "runas", which put a UAC prompt in front of every session; the
// service is started on demand by the GUI instead (StartServiceW), with the same
// port and token file the prompted helper got on its command line, and from
// there on it is the same helper running the same handshake. The installer
// grants interactive users the right to start the service, and no other right
// over it. Why that is safe enough, and what it gives up, is in
// docs/security-threats.md.
//
// Windows only, and compiled into the app alone: nothing else needs a service
// entry point, and the tests start the helper as a plain process.
#include "vpn/vpn_helper_launch.h"
#include "vpn/vpn_helper_server.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QString>
#include <QStringList>

#include <windows.h>
#include <winsvc.h>

#include <mutex>
#include <string>

namespace {

// main()'s arguments, for the QCoreApplication that ServiceMain creates. The
// helper's own arguments come from ServiceMain, not from these.
int g_argc = 0;
char **g_argv = nullptr;

SERVICE_STATUS_HANDLE g_status = nullptr;
int g_exitCode = 0;

// The control handler runs on the dispatcher's thread, the helper on
// ServiceMain's; this is what the one tells the other.
std::mutex g_appMutex;
QCoreApplication *g_app = nullptr;
bool g_stopRequested = false;

std::wstring serviceName()
{
    return QString::fromLatin1(freetunnel::kHelperServiceName).toStdWString();
}

void reportStatus(DWORD state, int exitCode = 0)
{
    SERVICE_STATUS st{};
    st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    st.dwCurrentState = state;
    st.dwControlsAccepted =
            state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    if (exitCode != 0) {
        // What the GUI reads back, to say why a helper it started is gone
        // (VpnHelperClient::helperServiceStoppedEarly): runVpnHelper's own codes.
        st.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        st.dwServiceSpecificExitCode = static_cast<DWORD>(exitCode);
    }
    // Taking the tunnel down can take a few seconds; past the hint the service
    // control manager would count the service as hung.
    st.dwWaitHint = state == SERVICE_STOP_PENDING ? 30000 : 0;
    SetServiceStatus(g_status, &st);
}

void quitHelper(QCoreApplication *app)
{
    QMetaObject::invokeMethod(app, []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
}

// Stop (an administrator, or the uninstaller) and shutdown end the helper the way
// its GUI going away does: the event loop quits, and the helper takes its tunnel
// down on the way out.
DWORD WINAPI onServiceControl(DWORD control, DWORD /*eventType*/, LPVOID /*eventData*/,
                              LPVOID /*context*/)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN: {
        reportStatus(SERVICE_STOP_PENDING);
        const std::lock_guard<std::mutex> lock(g_appMutex);
        g_stopRequested = true;
        if (g_app)
            quitHelper(g_app);
        return NO_ERROR;
    }
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI serviceMain(DWORD argc, LPWSTR *argv)
{
    const std::wstring name = serviceName();
    g_status = RegisterServiceCtrlHandlerExW(name.c_str(), onServiceControl, nullptr);
    if (!g_status)
        return;
    reportStatus(SERVICE_RUNNING);

    // The service name, then what StartServiceW was given
    // (freetunnel::helperServiceArguments). parseHelperLaunchArgs skips the name
    // as it skips the program on a command line, and it is the same parser, with
    // the same checks on the token file, that a prompted helper goes through.
    VpnHelperRunOptions options;
    for (DWORD i = 0; i < argc; ++i)
        options.launchArgs << QString::fromWCharArray(argv[i]);
    // Never empty, so the helper never falls back to the process's command line.
    if (options.launchArgs.isEmpty())
        options.launchArgs << QString::fromLatin1(freetunnel::kHelperServiceName);
    options.appLifetime = [](QCoreApplication *app) {
        const std::lock_guard<std::mutex> lock(g_appMutex);
        g_app = app;
        // A stop that came before there was anything to quit.
        if (app && g_stopRequested)
            quitHelper(app);
    };

    g_exitCode = runVpnHelper(g_argc, g_argv, options);
    reportStatus(SERVICE_STOPPED, g_exitCode);
}

} // namespace

int runVpnHelperService(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;
    std::wstring name = serviceName();
    SERVICE_TABLE_ENTRYW table[] = {{name.data(), serviceMain}, {nullptr, nullptr}};
    // Returns once the service has stopped. Fails at once when this process was
    // not started by the service control manager — FreeTunnel --service typed at
    // a prompt — and there is then nothing to do.
    if (!StartServiceCtrlDispatcherW(table))
        return 1;
    return g_exitCode;
}
