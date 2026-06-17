#pragma once

// Entry point for the privileged helper mode (FreeTunnel --helper --port P
// --token-file F). Reads the one-time auth token from the 0600 file F (kept off
// argv so it isn't exposed via /proc/<pid>/cmdline), then runs a headless
// QCoreApplication that drives the real VPN core and exposes it to the
// user-level GUI over a localhost socket.
int runVpnHelper(int argc, char **argv);
