#!/usr/bin/env bash
# Fail CI when the Russian catalogue the app ships does not match its sources:
# QML/C++ strings changed but i18n/freetunnel_ru.ts was not refreshed, the .qm was
# not rebuilt from the .ts, or a string has no finished translation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TS="$ROOT/i18n/freetunnel_ru.ts"
QM="$ROOT/i18n/freetunnel_ru.qm"
TS_BAK="$(mktemp)"
QM_BAK="$(mktemp)"
UPDATE_LOG="$(mktemp)"
trap 'rm -f "$TS_BAK" "$QM_BAK" "$UPDATE_LOG"' EXIT

cp "$TS" "$TS_BAK"
cp "$QM" "$QM_BAK"
# Its output names the tools and their versions, which an error below repeats.
bash "$ROOT/scripts/i18n-update.sh" >"$UPDATE_LOG" 2>&1 || { cat "$UPDATE_LOG" >&2; exit 1; }
grep '^warning:' "$UPDATE_LOG" >&2 || true

# Both files are compared with what this run's tools write, and another Qt's
# tools can write other bytes: say which Qt CI uses, and which tools ran here.
say_which_qt() {
    local expected
    expected="$(sed -nE "s/^  QT_VER: '([^']+)'.*/\1/p" "$ROOT/.github/workflows/security.yml" | head -n1)"
    echo "       with QT_ROOT_DIR at Qt $expected (QT_VER in .github/workflows), as CI does. This check ran:" >&2
    grep -E '^(lupdate|lrelease):' "$UPDATE_LOG" | sed 's/^/         /' >&2 || true
}

if ! diff -q "$TS" "$TS_BAK" >/dev/null; then
    echo "error: i18n/freetunnel_ru.ts is out of date — run: bash scripts/i18n-update.sh" >&2
    say_which_qt
    diff -u "$TS_BAK" "$TS" | head -80 >&2 || true
    exit 1
fi

# The app embeds the committed .qm (i18n/i18n.qrc), never the .ts, so a .ts that
# was edited or merged without running the update shipped whatever .qm had been
# committed before it, and the check above passed. lrelease writes the same bytes
# for the same .ts given the same Qt, which is why QT_VER is an exact version and
# CI runs this with it (security.yml); a rebuild here must use that Qt too.
if ! cmp -s "$QM" "$QM_BAK"; then
    echo "error: i18n/freetunnel_ru.qm was not built from i18n/freetunnel_ru.ts — run: bash scripts/i18n-update.sh" >&2
    say_which_qt
    exit 1
fi

# lrelease ships an unfinished translation exactly as if it were finished, and an
# empty one as the English, so nothing downstream tells a reviewed string from a
# guess lupdate copied over from another context, or from one never translated.
unfinished="$(awk '
    /<name>/      { ctx = $0; gsub(/.*<name>|<\/name>.*/, "", ctx) }
    /<source>/    { src = $0; gsub(/.*<source>|<\/source>.*/, "", src) }
    /type="unfinished"/ { print "  " ctx ": " src }' "$TS")"
if [[ -n "$unfinished" ]]; then
    echo "error: i18n/freetunnel_ru.ts has strings without a finished translation —" \
         "translate them, drop type=\"unfinished\", and run: bash scripts/i18n-update.sh" >&2
    echo "$unfinished" >&2
    exit 1
fi

echo "i18n catalog is up to date"
