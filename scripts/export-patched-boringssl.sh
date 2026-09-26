#!/usr/bin/env bash
# Re-export the boringssl conan recipe from a newer NativeLibsCommon on top of
# the one the upstream bootstrap exported from the pinned NLC.
#
# Upstream (v1.1.7) pins native_libs_common/8.1.52 and with it
# openssl/boring-2026-05-08, and so does dns-libs 2.10.2. NLC's make_ssl.cpp
# calls SSL_set_server_padding_request, SSL_set_grease_sigalgs_enabled and
# SSL_set_extension_order, which only NLC's own boringssl patches declare.
# AdGuard's own CI resolves the recipe revision from their internal conan
# remote; building from plain git the bootstrap exports it from each NLC version
# the dependencies ask for, and when those differ an older NLC's recipe could be
# the one left in the cache. Exporting the pinned NLC's recipe last makes it the
# one conan picks: same package name/version, later export timestamp.
#
# This pin must therefore track upstream's, not lead it: an older re-export
# wins on timestamp and silently downgrades the recipe out from under the NLC
# upstream actually asked for, which fails to compile rather than fails safe.
#
# Supply chain: this recipe is Python that runs at export/build time and it
# picks the boringssl source that every shipped VPN binary links statically, so
# it is pinned twice — to the immutable commit behind the NLC tag (tags can
# be moved), and to a SHA-256 over the exported recipe tree (a rewritten commit
# or a tampered mirror then fails the build instead of quietly swapping our TLS
# stack). Bumping NLC means updating BOTH constants below; re-run with
# FT_PRINT_RECIPE_DIGEST=1 to print the new digest. scripts/check-pinned-deps.sh
# keeps them from degrading back into a movable ref.
set -euo pipefail

# NativeLibsCommon v8.1.52 — annotated tag v8.1.52 peeled to its commit
# (git ls-remote https://github.com/AdguardTeam/NativeLibsCommon 'v8.1.52^{}').
NLC_COMMIT="58cef252031e2cc1f540ecaec2952f5f32afa3a1"
# Digest of conan/recipes/boringssl at that commit (see recipe_digest below).
NLC_RECIPE_SHA256="985e54fe4cc407953fbd4840cc108ee84606bd7fe6fe0e88a1b0fc1b8d11bbe5"
NLC_URL="https://github.com/AdguardTeam/NativeLibsCommon.git"

# macOS runners have shasum, Linux/git-bash have sha256sum.
sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum
  else
    shasum -a 256
  fi
}

# Stable digest of a directory tree: one "<file sha256>  <relative path>" line
# per file, byte-sorted, hashed again. The checkout below forces LF so the
# digest also matches on the Windows runner.
recipe_digest() {
  # Without this, a moved/renamed recipe path makes `cd` fail, the subshell then
  # hashes the CALLER's working directory, and the mismatch reads as "someone
  # tampered with the recipe" instead of "the path is wrong".
  if [[ ! -d "$1" ]]; then
    echo "boringssl: recipe directory not found: $1" >&2
    exit 1
  fi
  (
    cd "$1"
    find . -type f | LC_ALL=C sort | while IFS= read -r f; do
      printf '%s  %s\n' "$(sha256 < "$f" | cut -d' ' -f1)" "$f"
    done
  ) | sha256 | cut -d' ' -f1
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Fetch the pinned commit itself — never a branch or tag, which can be repointed.
git init -q "$TMP/nlc"
git -C "$TMP/nlc" config core.autocrlf false
git -C "$TMP/nlc" config core.eol lf
git -C "$TMP/nlc" remote add origin "$NLC_URL"
git -C "$TMP/nlc" fetch -q --depth 1 origin "$NLC_COMMIT"
git -C "$TMP/nlc" -c advice.detachedHead=false checkout -q --detach FETCH_HEAD

head="$(git -C "$TMP/nlc" rev-parse HEAD)"
if [[ "$head" != "$NLC_COMMIT" ]]; then
  echo "boringssl: NLC checkout is $head, expected $NLC_COMMIT" >&2
  exit 1
fi

RECIPE="$TMP/nlc/conan/recipes/boringssl"
digest="$(recipe_digest "$RECIPE")"
if [[ "${FT_PRINT_RECIPE_DIGEST:-0}" == "1" ]]; then
  echo "boringssl recipe digest: $digest"
fi
if [[ "$digest" != "$NLC_RECIPE_SHA256" ]]; then
  echo "boringssl: recipe tree digest mismatch at ${NLC_COMMIT}" >&2
  echo "  expected $NLC_RECIPE_SHA256" >&2
  echo "  actual   $digest" >&2
  exit 1
fi

conan export "$RECIPE" --user adguard --channel oss
echo "boringssl recipe exported from NLC ${NLC_COMMIT} (recipe ${digest})"
