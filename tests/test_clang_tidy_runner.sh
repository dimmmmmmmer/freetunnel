#!/usr/bin/env bash
# Run the production gate with fake build/analyser commands. A failing worker
# must fail the gate, while every other source still runs and reports findings.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/build with spaces" "$TMP/calls"
cat > "$TMP/bin/cmake" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
cat > "$TMP/bin/fake-tidy" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == --version ]]; then echo 'fake clang-tidy'; exit 0; fi
source_name="$(basename "$3")"
touch "$TIDY_TEST_CALLS/$source_name"
echo "diagnostic: $source_name" >&2
if [[ "$TIDY_TEST_FAIL" == yes && "$source_name" == Backend.cpp ]]; then exit 1; fi
EOF
chmod +x "$TMP/bin/cmake" "$TMP/bin/fake-tidy"
python3 - "$ROOT" "$TMP/build with spaces/compile_commands.json" <<'PY'
import json
import sys
from pathlib import Path
root, output = map(Path, sys.argv[1:])
files = ["src/main.cpp", "src/app/Backend.cpp", "src/vpn/qt_trusttunnel_switch.cpp",
         "src/main.cpp", "tests/test_qt_trusttunnel_client.cpp"]
output.write_text(json.dumps([{"file": str(root / name)} for name in files]))
PY
for failure in no yes; do
    rm -f "$TMP/calls/"*
    status=0
    PATH="$TMP/bin:$PATH" CLANG_TIDY="$TMP/bin/fake-tidy" \
        TIDY_TEST_CALLS="$TMP/calls" TIDY_TEST_FAIL="$failure" \
        bash "$ROOT/scripts/run-clang-tidy.sh" "$TMP/build with spaces" > "$TMP/output" 2>&1 || status=$?
    calls=("$TMP/calls/"*)
    [[ ${#calls[@]} == 3 ]] || { cat "$TMP/output"; echo 'Sources skipped or not deduplicated'; exit 1; }
    [[ $(grep -c '^diagnostic:' "$TMP/output") == 3 ]] || { cat "$TMP/output"; exit 1; }
    if [[ "$failure" == no ]]; then
        { [[ "$status" == 0 ]] && grep -q 'clang-tidy: OK' "$TMP/output"; } || { cat "$TMP/output"; exit 1; }
    else
        { [[ "$status" != 0 ]] && ! grep -q 'clang-tidy: OK' "$TMP/output"; } || { cat "$TMP/output"; exit 1; }
    fi
    echo "PASS: parallel clang-tidy gate (worker failure: $failure)"
done
