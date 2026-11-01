#!/usr/bin/env bash
# Capture output in a regular file: /dev/stderr may not be reopenable in CI.
set -euo pipefail
output="$(mktemp)"
trap 'rm -f "$output"' EXIT
status=0
adb shell am instrument -w -e withoutInstallPermission true \
  -e class org.freetunnel.android.TvDeviceTest#installWithoutThePermissionKeepsTheUpdateReady \
  org.freetunnel.android.test/androidx.test.runner.AndroidJUnitRunner > "$output" 2>&1 || status=$?
cat "$output"
[ "$status" -eq 0 ] || exit "$status"
grep -Eq '^OK \(1 test\)[[:space:]]*$' "$output"
