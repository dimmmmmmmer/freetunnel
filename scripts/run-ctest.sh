#!/usr/bin/env bash
# Run ctest for the test suite. On Linux CI, unlock gnome-keyring so credential
# tests work without FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK.
set -euo pipefail

BUILD_DIR="${1:?usage: run-ctest.sh <build-dir> [ctest args...]}"
shift

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

if [[ "$(uname -s)" == "Linux" ]] && [[ -n "${CI:-}" ]]; then
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
  dbus-run-session -- bash "$ROOT/scripts/run-ctest-keyring.sh" "$BUILD_DIR" "$@"
else
  # Dump the per-test log too: on some runners ctest's --output-on-failure text
  # does not survive into the job log, and a bare "***Failed" is not something
  # anyone can act on.
  if ! ctest --test-dir "$BUILD_DIR" -j1 --output-on-failure "$@"; then
    echo "===== LastTest.log ====="
    cat "$BUILD_DIR/Testing/Temporary/LastTest.log" 2>/dev/null || true
    exit 1
  fi
fi
