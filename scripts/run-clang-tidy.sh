#!/usr/bin/env bash
# Run clang-tidy on production sources using the test project's compile_commands.json.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-tidy}"
# CI names the versioned binary (clang-tidy-18), so a newer clang-tidy in the
# runner image cannot change what this gate reports.
TIDY="${CLANG_TIDY:-clang-tidy}"

if ! command -v "$TIDY" >/dev/null 2>&1; then
  echo "error: $TIDY not found" >&2
  exit 127
fi
"$TIDY" --version

# HeaderFilterRegex decides which headers findings are reported for, and one
# that matches nothing fails quietly: every finding in a header is dropped and
# the run still ends in OK. That is what happened when the filter became
# '(^|/)(include|src)/[^/]*\.h$' -- all of our headers sit one directory further
# down. So hold the filter to the tree before trusting it: every header under
# include/ and src/ must match it, and system, Qt and fetched headers must not.
ROOT="$ROOT" python3 - <<'PY'
import os
import re
import sys
from pathlib import Path

root = Path(os.environ["ROOT"]).resolve()
m = re.search(r"^HeaderFilterRegex:\s*'((?:[^']|'')*)'\s*$",
              (root / ".clang-tidy").read_text(), re.M)
if not m:
    sys.exit("error: .clang-tidy has no single-quoted HeaderFilterRegex")
pattern = m.group(1).replace("''", "'")
rx = re.compile(pattern)
ours = sorted(p for d in ("include", "src") for p in (root / d).rglob("*.h"))
if not ours:
    sys.exit("error: no headers found under include/ or src/")
theirs = [
    "/usr/include/glib-2.0/glib/gtypes.h",
    "/usr/include/openssl/evp.h",
    "/usr/include/xcb/xcb.h",
    "/opt/Qt/6.8.3/gcc_64/include/QtCore/qobject.h",
    str(root / "build-tidy/_deps/qhotkey-src/QHotkey/qhotkey.h"),
]
missed = [str(p) for p in ours if not rx.search(str(p))]
leaked = [p for p in theirs if rx.search(p)]
for p in missed:
    print(f"error: HeaderFilterRegex '{pattern}' does not match our header {p}", file=sys.stderr)
for p in leaked:
    print(f"error: HeaderFilterRegex '{pattern}' matches {p}, which is not ours", file=sys.stderr)
if missed or leaked:
    sys.exit(1)
print(f"clang-tidy: HeaderFilterRegex covers all {len(ours)} headers")
PY

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
  if ! "$TIDY" -p "$BUILD" "$src" --quiet; then
    fail=1
  fi
done

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "clang-tidy: OK"
