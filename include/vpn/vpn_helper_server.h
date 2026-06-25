// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Entry point for the privileged helper mode (FreeTunnel --helper --port P
// --token-file F). Reads the one-time auth token from the 0600 file F (kept
// off argv so it isn't exposed via /proc/<pid>/cmdline), then runs a headless
// QCoreApplication that drives the real VPN core and exposes it to the
// user-level GUI over loopback TCP (127.0.0.1:P). The port is random and the
// token is single-use; together they gate access to the elevated helper.
int runVpnHelper(int argc, char **argv);
