#!/usr/bin/env bash
# Re-export the boringssl conan recipe from a newer NativeLibsCommon on top of
# the one the upstream bootstrap exported from the pinned NLC.
#
# Upstream (v1.1.5-rc.1) calls SSL_set_raw_verify_algorithm_prefs, which only
# exists with NLC's 18_openssl_imitation.patch — first shipped in NLC v8.1.45,
# while upstream still pins native_libs_common/8.1.42. AdGuard's own CI
# resolves a fresher recipe REVISION of openssl/boring-2024-09-13 from their
# internal conan remote; building from plain git we must re-export that recipe
# ourselves. Same package name/version, later export timestamp — conan picks
# this revision over the bootstrap's one.
set -euo pipefail

NLC_REF="${NLC_BORINGSSL_REF:-v8.1.45}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git clone --depth 1 --branch "$NLC_REF" \
  https://github.com/AdguardTeam/NativeLibsCommon.git "$TMP/nlc"
conan export "$TMP/nlc/conan/recipes/boringssl" --user adguard --channel oss
echo "boringssl recipe exported from NLC ${NLC_REF}"
