#!/usr/bin/env bash
# Run ctest for the test suite. On Linux CI, unlock gnome-keyring so credential
# tests work without FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK.
set -euo pipefail

BUILD_DIR="${1:?usage: run-ctest.sh <build-dir> [ctest args...]}"
shift

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

if [[ "$(uname -s)" == "Linux" ]] && [[ -n "${CI:-}" ]]; then
  dbus-run-session -- bash -c '
    eval "$(echo "" | gnome-keyring-daemon --unlock --components=secrets 2>/dev/null || true)"
    eval "$(gnome-keyring-daemon --start --components=secrets 2>/dev/null || true)"
    exec ctest --test-dir "$1" -j1 --output-on-failure "${@:2}"
  ' _ "$BUILD_DIR" "$@"
else
  ctest --test-dir "$BUILD_DIR" -j1 --output-on-failure "$@"
fi
