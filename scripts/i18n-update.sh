#!/usr/bin/env bash
# Extract translatable strings and compile the Russian translation catalog.
# Requires Qt linguist tools (lupdate, lrelease) — typically in $QT_ROOT_DIR/bin.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TS="$ROOT/i18n/freetunnel_ru.ts"
QM="$ROOT/i18n/freetunnel_ru.qm"

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
    echo "error: $default not found (install qt6-l10n-tools or set QT_ROOT_DIR)" >&2
    exit 127
}

resolve_linguist_tool LUPDATE lupdate
resolve_linguist_tool LRELEASE lrelease

echo "lupdate: $LUPDATE"
echo "lrelease: $LRELEASE"

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
