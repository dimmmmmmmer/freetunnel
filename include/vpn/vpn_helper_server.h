// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QStringList>

#include <functional>

class QCoreApplication;

// Entry point for the privileged helper mode (FreeTunnel --helper --port P
// --token-file F). Reads the one-time auth token from the 0600 file F (kept
// off argv so it isn't exposed via /proc/<pid>/cmdline), then runs a headless
// QCoreApplication that drives the real VPN core and exposes it to the
// user-level GUI over loopback TCP (127.0.0.1:P). The port is random and the
// token is single-use; together they gate access to the elevated helper.
int runVpnHelper(int argc, char **argv);

// How the Windows helper service runs the same helper (vpn_helper_service_win.cpp).
struct VpnHelperRunOptions {
    // The launch arguments, read the way parseHelperLaunchArgs reads a command
    // line (the first entry is skipped). Empty: the process's own command line.
    // The service gets them from ServiceMain, not from its command line.
    QStringList launchArgs;
    // Called with the application object once it exists, and with nullptr before
    // it is destroyed, so that a stop request from another thread can end it.
    std::function<void(QCoreApplication *)> appLifetime;
};
int runVpnHelper(int argc, char **argv, const VpnHelperRunOptions &options);

#if defined(_WIN32)
// Entry point for FreeTunnel --service: the command line the installer registers
// for the helper service. Hands the process to the service control manager,
// whose ServiceMain runs the helper with the arguments StartServiceW was given.
// Returns non-zero at once when the process was not started as a service.
int runVpnHelperService(int argc, char **argv);
#endif
