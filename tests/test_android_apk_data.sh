#!/usr/bin/env bash
# scripts/check-apk-data.sh fails for an APK that lacks the shared data or the Inter fonts, or has any
# of it changed, and passes one that has it as the repository does. Fixtures: an APK is a zip.
#
#   bash tests/test_android_apk_data.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/scripts/check-apk-data.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

command -v unzip > /dev/null || { echo "unzip is needed" >&2; exit 2; }
command -v python3 > /dev/null || { echo "python3 is needed" >&2; exit 2; }

fail=0
# apk <path> [<name in the zip> <file to put there>]...: a zip with those entries, or none.
apk() {
  python3 - "$@" <<'PY'
import sys, zipfile
path, rest = sys.argv[1], sys.argv[2:]
with zipfile.ZipFile(path, "w") as z:
    z.writestr("AndroidManifest.xml", "x")
    for name, source in zip(rest[0::2], rest[1::2]):
        z.write(source, name)
PY
}
check() {
  local name=$1 expect=$2 file=$3 got=pass out
  out="$(bash "$SCRIPT" "$file" 2>&1)" || got=fail
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    sed 's/^/    /' <<<"$out"
    fail=1
  fi
}

RUSSIA="$ROOT/data/split/recommended-russia.txt"
ROUTES="$ROOT/data/split/default-excluded-routes.txt"
RUSSIA_IN_APK=assets/split/recommended-russia.txt
ROUTES_IN_APK=assets/split/default-excluded-routes.txt
# The fonts, where the build puts them (res/font/inter_regular.ttf and inter_medium.ttf).
REGULAR="$ROOT/assets/fonts/inter/Inter-Regular.ttf"
MEDIUM="$ROOT/assets/fonts/inter/Inter-Medium.ttf"
FONTS=(res/font/inter_regular.ttf "$REGULAR" res/font/inter_medium.ttf "$MEDIUM")

apk "$TMP/good.apk" "$RUSSIA_IN_APK" "$RUSSIA" "$ROUTES_IN_APK" "$ROUTES" "${FONTS[@]}"
check "an APK with both lists and both fonts as the repository has them" pass "$TMP/good.apk"

printf 'yandex.ru\n' > "$TMP/short.txt"
apk "$TMP/short.apk" "$RUSSIA_IN_APK" "$TMP/short.txt" "$ROUTES_IN_APK" "$ROUTES" "${FONTS[@]}"
check "an APK with another Russia list" fail "$TMP/short.apk"

{ cat "$RUSSIA"; echo "extra.example"; } > "$TMP/longer.txt"
apk "$TMP/longer.apk" "$RUSSIA_IN_APK" "$TMP/longer.txt" "$ROUTES_IN_APK" "$ROUTES" "${FONTS[@]}"
check "an APK with a rule more" fail "$TMP/longer.apk"

apk "$TMP/elsewhere.apk" assets/recommended-russia.txt "$RUSSIA" "$ROUTES_IN_APK" "$ROUTES" "${FONTS[@]}"
check "an APK with the Russia list in the wrong place" fail "$TMP/elsewhere.apk"

apk "$TMP/only-russia.apk" "$RUSSIA_IN_APK" "$RUSSIA" "${FONTS[@]}"
check "an APK without the routes" fail "$TMP/only-russia.apk"

apk "$TMP/only-routes.apk" "$ROUTES_IN_APK" "$ROUTES" "${FONTS[@]}"
check "an APK without the Russia list" fail "$TMP/only-routes.apk"

{ cat "$ROUTES"; echo "8.8.8.8/32"; } > "$TMP/more-routes.txt"
apk "$TMP/more-routes.apk" "$RUSSIA_IN_APK" "$RUSSIA" "$ROUTES_IN_APK" "$TMP/more-routes.txt" "${FONTS[@]}"
check "an APK with a route more" fail "$TMP/more-routes.apk"

printf '10.0.0.0/8\n' > "$TMP/few-routes.txt"
apk "$TMP/few-routes.apk" "$RUSSIA_IN_APK" "$RUSSIA" "$ROUTES_IN_APK" "$TMP/few-routes.txt" "${FONTS[@]}"
check "an APK with other routes" fail "$TMP/few-routes.apk"

apk "$TMP/routes-elsewhere.apk" "$RUSSIA_IN_APK" "$RUSSIA" assets/default-excluded-routes.txt "$ROUTES" "${FONTS[@]}"
check "an APK with the routes in the wrong place" fail "$TMP/routes-elsewhere.apk"

# The fonts are looked for by their bytes among the APK's res/**/*.ttf, whatever aapt2 names the entries.
LISTS=("$RUSSIA_IN_APK" "$RUSSIA" "$ROUTES_IN_APK" "$ROUTES")
apk "$TMP/short-paths.apk" "${LISTS[@]}" res/Ab.ttf "$REGULAR" res/zQ.ttf "$MEDIUM"
check "an APK with the fonts under shortened paths" pass "$TMP/short-paths.apk"

apk "$TMP/no-fonts.apk" "${LISTS[@]}"
check "an APK without the fonts" fail "$TMP/no-fonts.apk"

apk "$TMP/no-medium.apk" "${LISTS[@]}" res/font/inter_regular.ttf "$REGULAR"
check "an APK without Medium" fail "$TMP/no-medium.apk"

apk "$TMP/no-regular.apk" "${LISTS[@]}" res/font/inter_medium.ttf "$MEDIUM"
check "an APK without Regular" fail "$TMP/no-regular.apk"

apk "$TMP/regular-for-medium.apk" "${LISTS[@]}" res/font/inter_regular.ttf "$REGULAR" res/font/inter_medium.ttf "$REGULAR"
check "an APK with Regular where Medium belongs" fail "$TMP/regular-for-medium.apk"

{ cat "$MEDIUM"; printf 'x'; } > "$TMP/medium-changed.ttf"
apk "$TMP/medium-changed.apk" "${LISTS[@]}" res/font/inter_regular.ttf "$REGULAR" res/font/inter_medium.ttf "$TMP/medium-changed.ttf"
check "an APK with a Medium that has a byte more" fail "$TMP/medium-changed.apk"

apk "$TMP/fonts-as-assets.apk" "${LISTS[@]}" assets/inter_regular.ttf "$REGULAR" assets/inter_medium.ttf "$MEDIUM"
check "an APK with the fonts as assets, which Resources cannot read" fail "$TMP/fonts-as-assets.apk"

apk "$TMP/none.apk"
check "an APK with none" fail "$TMP/none.apk"

if bash "$SCRIPT" > /dev/null 2>&1; then
  echo "not ok - no APK named: expected fail, got pass"
  fail=1
else
  echo "ok - no APK named"
fi
exit "$fail"
