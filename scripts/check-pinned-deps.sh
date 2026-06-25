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

check "upstream ref" 'fa033c08|UPSTREAM_REF|upstream_ref' scripts/upstream_ref.txt
check "upstream ref in build workflow" 'UPSTREAM_REF|upstream_ref' .github/workflows/build.yml
check "QHotkey tag" 'GIT_TAG 1\.5\.0' CMakeLists.txt
check "QHotkey tag in tests" 'GIT_TAG 1\.5\.0' tests/CMakeLists.txt

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "pinned-deps: OK"
