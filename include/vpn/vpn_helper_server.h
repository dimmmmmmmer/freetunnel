#pragma once

// Entry point for the privileged helper mode (FreeTunnel --helper --socket S
// --token-file F [--peer-uid U]). Reads the one-time auth token from the 0600
// file F (kept off argv so it isn't exposed via /proc/<pid>/cmdline), then runs
// a headless QCoreApplication that drives the real VPN core and exposes it to
// the user-level GUI over a local socket (Unix-domain socket / Windows named
// pipe). On Unix the socket is chowned to U (the launching user) and locked to
// 0600, and each connection's peer credentials are checked against U, so only
// that user can drive the elevated helper.
int runVpnHelper(int argc, char **argv);
