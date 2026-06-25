#!/usr/bin/env bash
# Inner helper for run-ctest.sh — gnome-keyring in a dbus-run-session.
set -euo pipefail
BUILD_DIR="${1:?build dir required}"
shift
eval "$(echo "" | gnome-keyring-daemon --unlock --components=secrets 2>/dev/null || true)"
eval "$(gnome-keyring-daemon --start --components=secrets 2>/dev/null || true)"
exec ctest --test-dir "$BUILD_DIR" -j1 --output-on-failure "$@"
