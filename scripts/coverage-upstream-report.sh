#!/usr/bin/env bash
# Full-stack coverage: fast unit tests + instrumented upstream FreeTunnel build.
# Linux CI only (needs conan, clang-18, Qt). Falls back to unit-test-only locally.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_TESTS="${1:-$ROOT/build-coverage}"
OUT="${2:-$BUILD_TESTS/coverage}"
UPSTREAM="${UPSTREAM_DIR:-$ROOT/upstream-tree}"
UPSTREAM_BUILD="${UPSTREAM_BUILD:-$UPSTREAM/build-coverage}"
UPSTREAM_CLIENT="$UPSTREAM/FreeTunnel"

mkdir -p "$OUT"

echo "==> Unit test coverage (tests/)"
bash "$ROOT/scripts/coverage-report.sh" "$BUILD_TESTS" "$OUT"

if [[ "${FT_SKIP_UPSTREAM_COVERAGE:-0}" == "1" ]]; then
  echo "FT_SKIP_UPSTREAM_COVERAGE=1 — skipping upstream merge"
  exit 0
fi

if ! command -v conan >/dev/null 2>&1; then
  echo "note: conan not found — uploading unit-test coverage only"
  exit 0
fi

echo "==> Preparing upstream tree"
bash "$ROOT/scripts/setup-upstream-tree.sh"

pip install -q -r "$UPSTREAM/requirements.txt"

echo "==> Bootstrapping conan deps"
(
  cd "$UPSTREAM"
  ./scripts/bootstrap_conan_deps.py
)
# The upstream bootstrap exports the exact pinned dns-libs version itself;
# the old re-export workaround for tag drift is gone.
conan profile detect --force >/dev/null 2>&1 || true

set +e
UPSTREAM_OK=0

echo "==> Configure upstream build with coverage"
cmake -S "$UPSTREAM" -B "$UPSTREAM_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="${FT_COVERAGE_CC:-clang-18}" \
  -DCMAKE_CXX_COMPILER="${FT_COVERAGE_CXX:-clang++-18}" \
  -DCMAKE_C_FLAGS="--coverage" \
  -DCMAKE_CXX_FLAGS="--coverage" \
  -DIPV6_UNAVAILABLE=ON -DDISABLE_HTTP3=OFF \
  -DBUILD_TRUSTTUNNEL_QT=ON \
  ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"} \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--copy-dt-needed-entries --coverage"

cmake --build "$UPSTREAM_BUILD" --target FreeTunnel -j"$(nproc 2>/dev/null || echo 4)"
UPSTREAM_OK=$?
set -e
if [[ "$UPSTREAM_OK" -ne 0 ]]; then
  echo "warning: upstream instrumented build failed — uploading unit-test coverage only" >&2
  exit 0
fi

APP_BIN="$UPSTREAM_BUILD/FreeTunnel/FreeTunnel"
if [[ -x "$APP_BIN" ]]; then
  echo "==> Smoke-run instrumented app (headless)"
  export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
  timeout 12 "$APP_BIN" freetunnel://focus >/dev/null 2>&1 &
  smoke_pid=$!
  sleep 6
  kill "$smoke_pid" 2>/dev/null || true
  wait "$smoke_pid" 2>/dev/null || true
fi

echo "==> Capture upstream object coverage"
lcov --quiet --capture --directory "$UPSTREAM_BUILD" --output-file "$OUT/upstream.info" || true
lcov --quiet --remove "$OUT/upstream.info" \
  '*/tests/*' '*/_deps/*' '*/Qt/*' '/usr/*' \
  --ignore-errors unused \
  --output-file "$OUT/upstream.filtered.info" 2>/dev/null || true

if [[ -f "$OUT/upstream.filtered.info" && -s "$OUT/upstream.filtered.info" ]]; then
  if grep -q "^SF:${UPSTREAM_CLIENT}/" "$OUT/upstream.filtered.info" 2>/dev/null; then
    sed -i.bak "s|^SF:${UPSTREAM_CLIENT}/|SF:|g" "$OUT/upstream.filtered.info"
    rm -f "$OUT/upstream.filtered.info.bak"
  fi
  lcov --quiet --add-tracefile "$OUT/coverage.filtered.info" \
    --add-tracefile "$OUT/upstream.filtered.info" \
    --output-file "$OUT/coverage.merged.info"
  mv "$OUT/coverage.merged.info" "$OUT/coverage.filtered.info"
fi

lcov --summary "$OUT/coverage.filtered.info"
if command -v genhtml >/dev/null 2>&1; then
  genhtml --quiet "$OUT/coverage.filtered.info" --output-directory "$OUT/html"
  echo "HTML report: $OUT/html/index.html"
fi
