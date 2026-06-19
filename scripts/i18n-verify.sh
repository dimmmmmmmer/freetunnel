#!/usr/bin/env bash
# Fail CI when QML/C++ strings changed but i18n/freetunnel_ru.ts was not refreshed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TS="$ROOT/i18n/freetunnel_ru.ts"
BAK="$(mktemp)"

cp "$TS" "$BAK"
bash "$ROOT/scripts/i18n-update.sh" >/dev/null

if ! diff -q "$TS" "$BAK" >/dev/null; then
    echo "error: i18n/freetunnel_ru.ts is out of date — run: bash scripts/i18n-update.sh" >&2
    diff -u "$BAK" "$TS" | head -80 >&2 || true
    rm -f "$BAK"
    exit 1
fi

rm -f "$BAK"
echo "i18n catalog is up to date"
