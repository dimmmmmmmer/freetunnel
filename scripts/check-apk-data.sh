#!/usr/bin/env bash
# Fail unless an APK carries the data the desktop and the app share, as this commit has it:
# data/split/recommended-russia.txt and data/split/default-excluded-routes.txt, each at
# assets/split/<name> with the same bytes. android/app/build.gradle copies them in
# (copySharedData), and RussiaList and ExcludedRoutes read them: without the first a new
# installation's Default would be empty and Recommended for Russia would do nothing, without
# the second the VPN would exclude no private range and Restore defaults would empty the
# list, and no test of the app would say so but a device test.
#
# And the Inter font files, the two in assets/fonts/inter, as font resources (android/app/build.gradle
# copies them to res/font/inter_regular.ttf and inter_medium.ttf, Fonts reads them). They are found
# by their bytes, not by their paths, which aapt2 may shorten: for each file SHA256SUMS lists
# there has to be a .ttf under res/ in the APK with that SHA-256. Without them the app runs in the
# system's font, and nothing but a look at the screen would say so.
#
# usage: check-apk-data.sh <apk>
set -euo pipefail

apk="${1:?usage: check-apk-data.sh <apk>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$apk" ] || { echo "no APK at $apk" >&2; exit 2; }

listing="$(unzip -Z1 "$apk")"
fail=0
checked=0
# check <path in the repository below data/>: the APK has it at assets/<path>, unchanged.
check() {
  local entry="assets/$1"
  checked=$((checked + 1))
  if ! grep -qxF "$entry" <<< "$listing"; then
    echo "::error::$apk has no $entry"
    fail=1
  elif ! unzip -p "$apk" "$entry" | cmp -s - "$ROOT/data/$1"; then
    echo "::error::$entry in $apk differs from data/$1"
    fail=1
  fi
}

check split/recommended-russia.txt
check split/default-excluded-routes.txt

# Every font assets/fonts/inter/SHA256SUMS pins, by its bytes among the APK's res/**/*.ttf.
fonts_in_apk=""
while IFS= read -r entry; do
  [ -n "$entry" ] || continue
  fonts_in_apk+="$(unzip -p "$apk" "$entry" | sha256sum | cut -d' ' -f1)"$'\n'
done < <(grep -E '^res/.*\.ttf$' <<< "$listing" || true)
while read -r want name; do
  checked=$((checked + 1))
  if ! grep -qxF "$want" <<< "$fonts_in_apk"; then
    echo "::error::$apk has no font resource (res/**/*.ttf) with the bytes of assets/fonts/inter/$name"
    fail=1
  fi
done < "$ROOT/assets/fonts/inter/SHA256SUMS"

[ "$fail" -eq 0 ] || exit 1
echo "$apk carries the shared data and fonts ($checked files)"
