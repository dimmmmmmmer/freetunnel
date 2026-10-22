#!/usr/bin/env bash
# Tests for scripts/check-android-release-tag.sh, the Android release's check
# that a pushed tag is android-X.Y.Z and names appVersion. That workflow only
# runs for an android-* tag, so without these the script's first real run would
# be the release it is there to protect. The cases are mostly tags that must
# not publish anything.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/check-android-release-tag.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# gradle <appVersion line>...: a build.gradle with those lines around the real
# file's other assignments, written to $TMP/build.gradle.
gradle() {
  {
    echo "plugins { id 'com.android.application' }"
    printf '%s\n' "$@"
    echo "def appVersionCode = appVersion.tokenize('.').collect { it as int }"
    echo "android { defaultConfig { versionName appVersion } }"
  } > "$TMP/build.gradle"
}

out=""
rc=0
written=""
# check <tag> [<build.gradle>]
check() {
  rc=0
  : > "$TMP/output"
  out="$(GITHUB_OUTPUT="$TMP/output" bash "$CHECK" "$@" 2>&1)" || rc=$?
  written="$(cat "$TMP/output")"
}
fail=0
report() {
  echo "FAIL: $1: exit $rc, wrote '$written':"
  printf '    %s\n' "${out//$'\n'/$'\n'    }"
  fail=1
}
# passes <case> <version>
passes() {
  if [[ "$rc" -ne 0 || "$written" != "version=$2" ]]; then
    report "$1 (wanted exit 0 and 'version=$2')"
  else
    echo "ok: $1"
  fi
}
# refused <case> <ERE the output must match>
refused() {
  if [[ "$rc" -ne 1 || -n "$written" ]] || ! grep -qE -- "$2" <<<"$out"; then
    report "$1 (wanted exit 1, nothing written, output matching /$2/)"
  else
    echo "ok: $1"
  fi
}

for v in 1.0.0 1.2.3 12.34.56 10.0.99 209999.99.99; do
  gradle "def appVersion = '$v'"
  check "android-$v" "$TMP/build.gradle"
  passes "android-$v" "$v"
done

gradle "  def   appVersion='1.0.0'  "
check android-1.0.0 "$TMP/build.gradle"
passes "appVersion with other spacing" 1.0.0

# Without GITHUB_OUTPUT the script still answers, on stdout only.
gradle "def appVersion = '1.0.0'"
rc=0
out="$(env -u GITHUB_OUTPUT bash "$CHECK" android-1.0.0 "$TMP/build.gradle" 2>&1)" || rc=$?
if [[ "$rc" -ne 0 ]] || ! grep -q 'releases FreeTunnel for Android 1.0.0' <<<"$out"; then
  written=""; report "no GITHUB_OUTPUT"
else
  echo "ok: no GITHUB_OUTPUT"
fi

# Not android-X.Y.Z at all.
gradle "def appVersion = '1.0.0'"
for tag in v1.0.0 android-v1.0.0 Android-1.0.0 android-1.0 android-1 android-1.0.0.0 \
           android-1.0.0-rc1 android-1.0.0-preview android-1.0.0+1 "android-1.0.0 " \
           " android-1.0.0" "android- 1.0.0" android-1..0 android-.1.0 android-1.0. \
           "android-1.0.0"$'\n' android-1.0.x android-1.0.٠ android-1.0.0x "" android-; do
  check "$tag" "$TMP/build.gradle"
  refused "tag '$tag'" "::error title=Not an Android release tag::"
done

for tag in android-01.0.0 android-1.00.0 android-1.0.01 android-1.02.0 android-00.0.0; do
  gradle "def appVersion = '${tag#android-}'"
  check "$tag" "$TMP/build.gradle"
  refused "$tag, even when appVersion says the same" "::error title=Leading zero in the tag::"
done

for tag in android-0.9.0 android-0.0.1 android-0.99.99; do
  gradle "def appVersion = '${tag#android-}'"
  check "$tag" "$TMP/build.gradle"
  refused "$tag is the preview" "::error title=Preview version::"
done

for tag in android-1.100.0 android-1.0.100 android-1.999.0 android-1.0.99999999999999999999999; do
  gradle "def appVersion = '${tag#android-}'"
  check "$tag" "$TMP/build.gradle"
  refused "$tag overflows versionCode's Y*100 + Z" "::error title=Version part over 99::"
done

for tag in android-210000.0.0 android-1000000.0.0 android-99999999999999999999.0.0; do
  gradle "def appVersion = '${tag#android-}'"
  check "$tag" "$TMP/build.gradle"
  refused "$tag overflows versionCode" "::error title=Major version too large::"
done

gradle "def appVersion = '1.0.0'"
check android-1.0.1 "$TMP/build.gradle"
refused "a tag that is not appVersion" \
  "::error title=Tag does not match appVersion::the tag says 1.0.1 and appVersion in .* is 1.0.0"

gradle "def appVersion = '1.0.0' // the next release"
check android-1.0.0 "$TMP/build.gradle"
refused "appVersion with a comment after it" "::error title=Cannot read appVersion::.*in a form this check does not read"

gradle 'def appVersion = "1.0.0"'
check android-1.0.0 "$TMP/build.gradle"
refused "appVersion in double quotes" "in a form this check does not read"

gradle "def appVersion = '1.0.0'" "appVersion = '2.0.0'"
check android-1.0.0 "$TMP/build.gradle"
refused "appVersion assigned twice" "must assign appVersion exactly once, and does 2 times"

gradle "def version = '1.0.0'"
check android-1.0.0 "$TMP/build.gradle"
refused "no appVersion" "must assign appVersion exactly once, and does 0 times"

# A comparison is not an assignment, nor is another name that ends the same way.
gradle "def appVersion = '1.0.0'" "if (appVersion == '0.0.0') throw new GradleException('x')" \
       "def myappVersion = '9.9.9'"
check android-1.0.0 "$TMP/build.gradle"
passes "appVersion compared, and a longer name assigned" 1.0.0

check android-1.0.0 "$TMP/missing.gradle"
refused "no build.gradle" "::error title=No build.gradle::"

rc=0
out="$(bash "$CHECK" 2>&1)" || rc=$?
if [[ "$rc" -ne 2 ]]; then
  written=""; report "no arguments (wanted exit 2)"
else
  echo "ok: no arguments"
fi

# The real android/app/build.gradle: whatever its version, the script must be
# able to read it. A tag no release would use gets an answer naming it.
check android-209999.99.99
refused "the repository's build.gradle is readable" \
  "::error title=Tag does not match appVersion::the tag says 209999.99.99 and appVersion in .*android/app/build.gradle is [0-9]+\\.[0-9]+\\.[0-9]+\\."

exit "$fail"
