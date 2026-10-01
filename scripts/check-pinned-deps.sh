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
check "QWindowKit commit pin" 'GIT_TAG [0-9a-f]{40}' cmake/QWindowKit.cmake
# The app and test_windows_chrome both include cmake/QWindowKit.cmake. A second
# declaration could be bumped on its own, and the test would then check a chrome
# the app no longer ships.
while read -r line; do
  echo "pinned-deps: QWindowKit declared outside cmake/QWindowKit.cmake, include that file instead: $line" >&2
  fail=1
done < <(grep -n 'qwindowkit\.git' CMakeLists.txt tests/CMakeLists.txt cmake/*.cmake \
         | grep -v '^cmake/QWindowKit\.cmake:')
# The TLS stack's conan recipe: commit pin plus a digest over the exported tree.
check "boringssl NLC commit pin" '^NLC_COMMIT="[0-9a-f]{40}"$' scripts/export-patched-boringssl.sh
check "boringssl recipe digest" '^NLC_RECIPE_SHA256="[0-9a-f]{64}"$' scripts/export-patched-boringssl.sh
check "linuxdeploy checksum" 'sha256sum -c -' .github/workflows/build.yml
# The Linux release is compiled inside this image; a tag can be repointed.
check "Linux build container digest pin" 'container: ubuntu:[0-9.]+@sha256:[0-9a-f]{64}$' .github/workflows/build.yml

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

# One toolchain version per repository, not per workflow. Qt and conan were each
# written out in several workflows; nothing stopped a bump landing in one and not
# the others, and the failure mode is quiet — a release built against a Qt the
# tests never ran on. Each workflow now declares it once in `env:`; here we check
# the declarations agree.
check_one_value() {
  local name="$1"
  local values
  values="$(grep -rhoE "^  ${name}: '[^']+'" .github/workflows | sort -u)"
  if [[ -z "$values" ]]; then
    echo "pinned-deps: no workflow declares ${name}" >&2
    fail=1
  elif [[ "$(printf '%s\n' "$values" | wc -l)" -ne 1 ]]; then
    echo "pinned-deps: workflows disagree on ${name}:" >&2
    printf '%s\n' "$values" >&2
    fail=1
  fi
}

check_one_value QT_VER
check_one_value CONAN_VER

# ...and that nothing reintroduces a literal alongside the declaration.
while read -r line; do
  echo "pinned-deps: hardcoded Qt version, use \${{ env.QT_VER }}: $line" >&2
  fail=1
done < <(grep -rnE "^ +version: '[0-9]" .github/workflows)

# ...nor a Qt from a package manager, which no declaration reaches: the macOS
# unit tests ran on `brew install qt`, Homebrew's Qt of the week, while the
# macOS release was built against QT_VER.
while read -r line; do
  echo "pinned-deps: Qt from Homebrew, use install-qt-action with \${{ env.QT_VER }}: $line" >&2
  fail=1
done < <(grep -rnE 'brew (install|reinstall|upgrade)( [^#]*)? qt(@[0-9]+)?([[:space:];|&]|$)' .github/workflows)

while read -r line; do
  echo "pinned-deps: hardcoded conan version, use CONAN_VER: $line" >&2
  fail=1
done < <(grep -rnE 'conan==[0-9]' .github/workflows)

# The vendored patches are applied in four places (this repo's two scripts, the
# build workflow, and CONTRIBUTING). Naming one patch file in any of them is how
# adding a second patch, or renaming the first, silently stops applying one —
# which is exactly what happened. Everything must glob the directory.
while read -r line; do
  echo "pinned-deps: hardcoded patch filename, glob vendor/trusttunnel/*.patch instead: $line" >&2
  fail=1
done < <(grep -rn 'vendor/trusttunnel/[A-Za-z0-9_.-]*\.patch' \
           .github/workflows scripts CONTRIBUTING.md 2>/dev/null)

# ...and apply them with --fuzz=0. GNU patch's default fuzz lets a hunk land
# with context lines that no longer match, which after an upstream bump can be
# the wrong place; each of those places has to refuse it.
while read -r line; do
  echo "pinned-deps: vendored patch applied without --fuzz=0: $line" >&2
  fail=1
done < <(grep -rnE --exclude=check-pinned-deps.sh '(^|[^[:alnum:]_-])patch -p1' \
           .github/workflows scripts CONTRIBUTING.md 2>/dev/null | grep -v -- '--fuzz=0')

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "pinned-deps: OK"
