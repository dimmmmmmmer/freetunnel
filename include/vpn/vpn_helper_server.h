#pragma once

// Entry point for the privileged helper mode (FreeTunnel --helper --port P
// --token T). Runs a headless QCoreApplication that drives the real VPN core
// and exposes it to the user-level GUI over a localhost socket.
int runVpnHelper(int argc, char **argv);
