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
    # On the Windows runner ctest captures nothing at all from the Qt Test
    # binaries — every entry in the log above, passing ones included, carries an
    # empty Output block — so a failure there arrives as "Test Failed" and not one
    # word about which assertion broke. Ask the binaries themselves instead:
    # Qt Test's -o writes its report to a destination it opens, which does not
    # depend on whatever ctest does with the child's stdout.
    # Each test writes its own Qt Test report (see the -o in tests/CMakeLists.txt),
    # which is the report from the run that actually failed — unlike a re-run,
    # which may well pass.
    echo "===== reports from the failed run ====="
    BUILD_ABS0="$(cd "$BUILD_DIR" && pwd)"
    ctest --test-dir "$BUILD_DIR" --rerun-failed -N 2>/dev/null \
        | sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9]*:[[:space:]]*//p' \
        | while read -r name; do
      [ -n "$name" ] || continue
      echo "--- $name ---"
      cat "$BUILD_ABS0/report-$name.txt" 2>/dev/null || echo "(no report written)"
    done

    echo "===== re-running failed tests directly ====="
    # Absolute, because the re-run below runs from inside the build directory to
    # match ctest, and BUILD_DIR is usually relative to where this script started.
    BUILD_ABS="$(cd "$BUILD_DIR" && pwd)"
    ctest --test-dir "$BUILD_DIR" --rerun-failed -N 2>/dev/null \
        | sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9]*:[[:space:]]*//p' \
        | while read -r name; do
      [ -n "$name" ] || continue
      for bin in "$BUILD_ABS/test_$name" "$BUILD_ABS/test_$name.exe"; do
        [ -x "$bin" ] || continue
        echo "--- $name ---"
        # To a FILE, not to stdout. When stdout is a pipe it is block-buffered, so
        # a test that dies mid-run — which is what an empty log with a long elapsed
        # time looks like — takes its buffer with it. Qt Test flushes its file
        # writer as it goes, so the report up to the crash survives.
        #
        # Up to three attempts, stopping at the first failure: a test that ctest
        # just failed and that then passes here is flaky, and one clean re-run
        # tells you nothing about why. The report kept is the failing one.
        for attempt in 1 2 3; do
          echo "(attempt $attempt)"
          if ! (cd "$BUILD_ABS" && "$bin" -o "$BUILD_ABS/failed-$name.txt,txt"); then
            break
          fi
        done
        cat "$BUILD_ABS/failed-$name.txt" 2>/dev/null || echo "(no report written)"
        break
      done
    done
    exit 1
  fi
fi
