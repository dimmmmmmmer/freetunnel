// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include "core/ReleaseSigning.h"

namespace freetunnel {
// Only untagged CI builds substitute a disposable, build-specific key here.
// Passwordless upgrades always require the release key in ReleaseSigning.h.
inline constexpr auto kBundledHelperSigningPublicKeyPem = kReleaseSigningPublicKeyPem;
} // namespace freetunnel
