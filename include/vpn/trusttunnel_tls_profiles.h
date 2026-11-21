// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The [endpoint] tls_profile names TrustTunnel core 1.3.0 accepts. The spellings
// are its TLS_PROFILE_MAP (trusttunnel/src/config.cpp). scripts/verify_upstream_patch.sh
// compares kTrustTunnelTlsProfiles with that map, so a bump that adds or drops a
// name stops there until this list changes. The core reads the key as an exact,
// case-sensitive name and refuses the whole config for one it has no name for.
// An absent or empty value means chrome, the core's default. "default" is a
// deprecated alias of boringssl that the core still accepts; it is not a separate
// choice. Each connection stores its own value. This header is the one list for
// the desktop editor, the import that refuses a file, the deep-link tag and the
// mock core. It is header-only and free of Qt.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace freetunnel {

inline constexpr std::string_view kTrustTunnelDefaultTlsProfile = "chrome";

// Names the core's map contains, including the deprecated alias.
inline constexpr std::array<std::string_view, 8> kTrustTunnelTlsProfiles{
        "chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "boringssl", "default"};

// Choices a connection can be set to. Order matches the core's map, without the alias.
inline constexpr std::array<std::string_view, 7> kTrustTunnelTlsProfileChoices{
        "chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "boringssl"};

// Deep-link tag 0x10, one byte. Same order as VpnTlsProfile in TrustTunnel 1.3.0.
inline constexpr std::array<std::string_view, 7> kTrustTunnelTlsProfileBytes{
        "chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "boringssl"};

constexpr bool isKnownTlsProfile(std::string_view name)
{
    for (const std::string_view known : kTrustTunnelTlsProfiles) {
        if (name == known)
            return true;
    }
    return false;
}

// "default" is boringssl. Empty stays empty, which callers treat as chrome.
constexpr std::string_view canonicalTlsProfile(std::string_view name)
{
    return name == "default" ? std::string_view("boringssl") : name;
}

// A value worth writing. Chrome, and a missing value, are the core's default.
constexpr bool tlsProfileIsExplicit(std::string_view name)
{
    const std::string_view canonical = canonicalTlsProfile(name);
    return !canonical.empty() && canonical != kTrustTunnelDefaultTlsProfile;
}

constexpr std::optional<std::string_view> tlsProfileFromByte(unsigned char value)
{
    if (value >= kTrustTunnelTlsProfileBytes.size())
        return std::nullopt;
    return kTrustTunnelTlsProfileBytes[value];
}

constexpr std::optional<unsigned char> tlsProfileByte(std::string_view name)
{
    const std::string_view canonical = canonicalTlsProfile(name);
    for (std::size_t i = 0; i < kTrustTunnelTlsProfileBytes.size(); ++i) {
        if (kTrustTunnelTlsProfileBytes[i] == canonical)
            return static_cast<unsigned char>(i);
    }
    return std::nullopt;
}

} // namespace freetunnel
