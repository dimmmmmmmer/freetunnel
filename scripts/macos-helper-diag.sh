#!/usr/bin/env bash
# TEMPORARY: the real helper's suites on macOS, quickly, without the whole Tests run.
set -uo pipefail
B="${1:?build dir}"
rc=0
for t in helper_server_fuzz helper_server_lifecycle helper_server_settings helper_server; do
  echo "== $t"
  (cd "$B" && QT_QPA_PLATFORM=offscreen ./test_$t 2>&1 | grep -v waitForDisconnected | grep -E "^(PASS|FAIL|Totals|   Loc|QWARN|QINFO)" | head -80) || true
  (cd "$B" && QT_QPA_PLATFORM=offscreen ./test_$t >/dev/null 2>&1) || rc=1
done
exit $rc
