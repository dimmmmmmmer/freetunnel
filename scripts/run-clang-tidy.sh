#!/usr/bin/env bash
# Run clang-tidy on production sources using the test project's compile_commands.json.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-tidy}"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "error: clang-tidy not found" >&2
  exit 127
fi

cmake -S "$ROOT/tests" -B "$BUILD" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mapfile -t SOURCES < <(
  BUILD="$BUILD" ROOT="$ROOT" python3 - <<'PY'
import json
import os
from pathlib import Path

build = Path(os.environ["BUILD"])
cmds = json.loads((build / "compile_commands.json").read_text())
root = Path(os.environ["ROOT"]).resolve()
seen = set()
for entry in cmds:
    src = Path(entry["file"]).resolve()
    if not src.is_relative_to(root / "src"):
        continue
    if src.suffix != ".cpp":
        continue
    if src not in seen:
        seen.add(src)
        print(src)
PY
)

if [[ ${#SOURCES[@]} -eq 0 ]]; then
  echo "error: no src/*.cpp entries in compile_commands.json" >&2
  exit 1
fi

echo "clang-tidy: ${#SOURCES[@]} translation units"
fail=0
for src in "${SOURCES[@]}"; do
  echo "  $src"
  if ! clang-tidy -p "$BUILD" "$src" --quiet; then
    fail=1
  fi
done

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "clang-tidy: OK"
