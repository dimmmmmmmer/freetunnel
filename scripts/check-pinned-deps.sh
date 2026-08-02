#!/usr/bin/env bash
# Verify pinned third-party refs are present and documented.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail=0

check() {
  local desc="$1"
  local pattern="$2"
  local file="$3"
  if ! grep -qE "$pattern" "$file"; then
    echo "pinned-deps: missing $desc in $file" >&2
    fail=1
  fi
}

check "upstream ref" '^[0-9a-f]{40}$' scripts/upstream_ref.txt
check "upstream ref in build workflow" 'UPSTREAM_REF|upstream_ref' .github/workflows/build.yml
# A tag proves nothing — it can be repointed. Require the full commit SHA.
check "QHotkey commit pin" 'GIT_TAG [0-9a-f]{40}' CMakeLists.txt
check "QHotkey commit pin in tests" 'GIT_TAG [0-9a-f]{40}' tests/CMakeLists.txt
# The TLS stack's conan recipe: commit pin plus a digest over the exported tree.
check "boringssl NLC commit pin" '^NLC_COMMIT="[0-9a-f]{40}"$' scripts/export-patched-boringssl.sh
check "boringssl recipe digest" '^NLC_RECIPE_SHA256="[0-9a-f]{64}"$' scripts/export-patched-boringssl.sh
check "linuxdeploy checksum" 'sha256sum -c -' .github/workflows/build.yml

# Third-party GitHub Actions must be pinned to full commit SHAs. Validate every
# `uses:` reference in the workflows directly (a tag or branch is mutable —
# supply-chain risk). Checking the workflows themselves, instead of keeping a
# duplicate SHA list in a side file, keeps dependabot action bumps mergeable
# without a manual sync step.
while read -r use; do
  ref="${use##*@}"
  if ! [[ "$ref" =~ ^[0-9a-f]{40}$ ]]; then
    echo "pinned-deps: action not pinned to a full commit SHA: $use" >&2
    fail=1
  fi
done < <(grep -rhoE 'uses:[[:space:]]*[^[:space:]]+@[^[:space:]]+' .github/workflows \
         | sed -E 's/uses:[[:space:]]*//')

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "pinned-deps: OK"
