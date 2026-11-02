// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The [endpoint] tls_profile names TrustTunnel core 1.3 knows: the spellings of
// its TLS_PROFILE_MAP (trusttunnel/src/config.cpp), which scripts/
// verify_upstream_patch.sh compares this list with, so a bump that adds or drops
// one stops there until it is changed here. The core reads the key as an exact, case-sensitive name and refuses
// the whole config for one it has no name for, while an empty one means the
// default (chrome). One list for the wrapper that explains the refusal, the
// import that refuses the file before it is stored, and the mock core.
//
// Header only and free of Qt, so every target that builds the wrapper has it.

#include <array>
#include <string_view>

namespace freetunnel {

inline constexpr std::array<std::string_view, 7> kTrustTunnelTlsProfiles{
        "chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "default"};

// Whether the core takes this as a tls_profile. The empty name is not one: it is
// no profile, and the callers leave it alone before asking.
constexpr bool isKnownTlsProfile(std::string_view name)
{
    for (const std::string_view known : kTrustTunnelTlsProfiles) {
        if (name == known)
            return true;
    }
    return false;
}

} // namespace freetunnel
