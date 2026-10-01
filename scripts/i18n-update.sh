#!/usr/bin/env bash
# Extract translatable strings and compile the Russian translation catalog.
# Requires Qt linguist tools (lupdate, lrelease) — typically in $QT_ROOT_DIR/bin.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TS="$ROOT/i18n/freetunnel_ru.ts"
QM="$ROOT/i18n/freetunnel_ru.qm"

# The Qt CI checks the catalogue with: QT_VER in the workflows, an exact version.
expected_qt_version() {
    sed -nE "s/^  QT_VER: '([^']+)'.*/\1/p" "$ROOT/.github/workflows/security.yml" | head -n1
}

# "lupdate version 6.8.3" and the like, or a note that the tool would not say.
tool_version() {
    "$1" -version 2>/dev/null | head -n1 || true
}

resolve_linguist_tool() {
    local var_name="$1"
    local default="$2"
    if [[ -n "${!var_name:-}" ]]; then
        return
    fi
    if [[ -n "${QT_ROOT_DIR:-}" && -x "$QT_ROOT_DIR/bin/$default" ]]; then
        printf -v "$var_name" '%s' "$QT_ROOT_DIR/bin/$default"
        return
    fi
    if command -v "$default" >/dev/null 2>&1; then
        printf -v "$var_name" '%s' "$default"
        return
    fi
    local alt="${default}-qt6"
    if command -v "$alt" >/dev/null 2>&1; then
        printf -v "$var_name" '%s' "$alt"
        return
    fi
    for dir in /usr/lib/qt6/bin /usr/lib/x86_64-linux-gnu/qt6/bin; do
        if [[ -x "$dir/$default" ]]; then
            printf -v "$var_name" '%s' "$dir/$default"
            return
        fi
    done
    # The distro's tools are found above as a last resort, but CI checks with Qt's
    # own (QT_VER) and compares bytes, so point at that Qt rather than install them.
    echo "error: $default not found (set QT_ROOT_DIR to Qt $(expected_qt_version), QT_VER in .github/workflows)" >&2
    exit 127
}

resolve_linguist_tool LUPDATE lupdate
resolve_linguist_tool LRELEASE lrelease

# With their versions: CI compares what they write byte for byte, so a mismatch
# with another Qt's tools should say which Qt wrote it.
echo "lupdate: $LUPDATE ($(tool_version "$LUPDATE"))"
echo "lrelease: $LRELEASE ($(tool_version "$LRELEASE"))"
expected="$(expected_qt_version)"
if [[ -n "$expected" ]] && ! tool_version "$LRELEASE" | grep -qE "(^| )${expected//./\.}\$"; then
    echo "warning: CI checks the catalogue with Qt $expected (QT_VER in .github/workflows);" \
         "these tools may write a .ts or .qm that differs from what CI writes" >&2
fi

# Scan only app sources — not tests/ or FetchContent deps (QHotkey HotTestWidget, …).
"$LUPDATE" \
  "$ROOT/qml" \
  "$ROOT/src" \
  "$ROOT/include" \
  "$ROOT/main.cpp" \
  -I "$ROOT/include" \
  -ts "$TS" \
  -locations none \
  -no-obsolete

"$LRELEASE" "$TS" -qm "$QM"

echo "Updated $TS and $QM"
