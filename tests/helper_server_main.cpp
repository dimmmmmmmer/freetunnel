// cppcheck-suppress-file missingIncludeSystem
// Test-only entry point for the REAL privileged helper server.
//
// vpn_helper_server.cpp is the code that runs as root and parses everything the
// GUI sends it, and it was in no test target at all: the helper-IPC tests drove
// a hand-written double whose pre-auth policy differed from production, so a
// parsing or authentication regression in the real server was invisible.
//
// Built against tests/mock_core, this produces a helper process that speaks the
// real protocol without needing the actual VPN core (or root). test_helper_server
// spawns it and drives it with the real VpnHelperClient.
#include "vpn/vpn_helper_server.h"

int main(int argc, char **argv)
{
    return runVpnHelper(argc, argv);
}
