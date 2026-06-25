#!/usr/bin/env bash
# Generate an lcov HTML/text summary from a coverage-instrumented test build.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-coverage}"
OUT="${2:-$BUILD/coverage}"

if ! command -v lcov >/dev/null 2>&1; then
  echo "error: lcov not found" >&2
  exit 127
fi

cmake -S "$ROOT/tests" -B "$BUILD" -G Ninja -DFT_ENABLE_COVERAGE=ON
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
ctest --test-dir "$BUILD" -j1 --output-on-failure

mkdir -p "$OUT"
lcov --quiet --capture --directory "$BUILD" --output-file "$OUT/coverage.info"
lcov --quiet --remove "$OUT/coverage.info" \
  '*/tests/*' '*/_deps/*' '*/Qt/*' '/usr/*' '*/build-coverage/*' \
  --ignore-errors unused \
  --output-file "$OUT/coverage.filtered.info"

# Codacy matches lcov SF: entries to repository paths — use paths relative to repo root.
if grep -q "^SF:${ROOT}/" "$OUT/coverage.filtered.info" 2>/dev/null; then
  sed -i.bak "s|^SF:${ROOT}/|SF:|g" "$OUT/coverage.filtered.info"
  rm -f "$OUT/coverage.filtered.info.bak"
fi

lcov --summary "$OUT/coverage.filtered.info"
if command -v genhtml >/dev/null 2>&1; then
  genhtml --quiet "$OUT/coverage.filtered.info" --output-directory "$OUT/html"
  echo "HTML report: $OUT/html/index.html"
fi
