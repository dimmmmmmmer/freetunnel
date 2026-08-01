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
#
# Supply chain: this recipe is Python that runs at export/build time and it
# picks the boringssl source that every shipped VPN binary links statically, so
# it is pinned twice — to the immutable commit behind the v8.1.45 tag (tags can
# be moved), and to a SHA-256 over the exported recipe tree (a rewritten commit
# or a tampered mirror then fails the build instead of quietly swapping our TLS
# stack). Bumping NLC means updating BOTH constants below; re-run with
# FT_PRINT_RECIPE_DIGEST=1 to print the new digest. scripts/check-pinned-deps.sh
# keeps them from degrading back into a movable ref.
set -euo pipefail

# NativeLibsCommon v8.1.45 — annotated tag v8.1.45 peeled to its commit
# (git ls-remote https://github.com/AdguardTeam/NativeLibsCommon 'v8.1.45^{}').
NLC_COMMIT="aa2ea9200fc8334fe3d5d3a096495ede975b8396"
# Digest of conan/recipes/boringssl at that commit (see recipe_digest below).
NLC_RECIPE_SHA256="40d7735282ff83ad8c2a9beb8c9f376a43327dc5310eabf569d8e8fd9d738938"
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
