#!/usr/bin/env bash
# Extract translatable strings and compile the Russian translation catalog.
# Requires Qt linguist tools (lupdate, lrelease) — typically in $QT_ROOT_DIR/bin.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TS="$ROOT/i18n/freetunnel_ru.ts"
QM="$ROOT/i18n/freetunnel_ru.qm"

if [[ -n "${QT_ROOT_DIR:-}" ]]; then
  LUPDATE="${LUPDATE:-$QT_ROOT_DIR/bin/lupdate}"
  LRELEASE="${LRELEASE:-$QT_ROOT_DIR/bin/lrelease}"
else
  LUPDATE="${LUPDATE:-lupdate}"
  LRELEASE="${LRELEASE:-lrelease}"
fi

echo "lupdate: $LUPDATE"
echo "lrelease: $LRELEASE"

"$LUPDATE" "$ROOT" \
  -I "$ROOT/include" \
  -ts "$TS" \
  -locations none \
  -no-obsolete

"$LRELEASE" "$TS" -qm "$QM"

echo "Updated $TS and $QM"
