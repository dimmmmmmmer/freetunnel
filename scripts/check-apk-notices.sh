#!/usr/bin/env bash
# Fail unless an APK carries the third-party notices as this commit has them:
# THIRD_PARTY_NOTICES.md, LICENSE and every license text under licenses/common
# and licenses/android, as assets with the same paths and the same bytes, and
# none of licenses/desktop. android/app/build.gradle copies them in, and
# Settings, About, Open-source licenses shows them.
#
# usage: check-apk-notices.sh <apk>
set -euo pipefail

apk="${1:?usage: check-apk-notices.sh <apk>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$apk" ] || { echo "no APK at $apk" >&2; exit 2; }

listing="$(unzip -Z1 "$apk")"
fail=0
checked=0
# check <path in the repository>: the APK has it at assets/<path>, unchanged.
check() {
  local entry="assets/$1"
  checked=$((checked + 1))
  if ! grep -qxF "$entry" <<< "$listing"; then
    echo "::error::$apk has no $entry"
    fail=1
  elif ! unzip -p "$apk" "$entry" | cmp -s - "$ROOT/$1"; then
    echo "::error::$entry in $apk differs from $1"
    fail=1
  fi
}

check THIRD_PARTY_NOTICES.md
check LICENSE
while IFS= read -r file; do
  check "${file#"$ROOT"/}"
done < <(find "$ROOT/licenses/common" "$ROOT/licenses/android" -type f | sort)

if grep -q '^assets/licenses/desktop/' <<< "$listing"; then
  echo "::error::$apk carries the desktop's license texts (assets/licenses/desktop/)"
  fail=1
fi
[ "$fail" -eq 0 ] || exit 1
echo "$apk carries the third-party notices ($checked files)"
