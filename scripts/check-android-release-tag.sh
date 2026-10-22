#!/usr/bin/env bash
# Check that a pushed tag may publish an Android release, and give its version.
# Run by the first job of .github/workflows/android-release.yml, before the CI
# and before anything is signed.
#
# The tag must be exactly android-X.Y.Z, and X.Y.Z must equal appVersion in
# android/app/build.gradle. The in-app updater binds the tag, the APK's file
# name, the manifest's #version= line and the APK's versionName together, and
# refuses an update where any of them says another version.
#
# The parts are decimal without leading zeros, so a version has one spelling:
# 1.02.0 would be a second name for 1.2.0. Y and Z are at most 99, because
# versionCode is X*10000 + Y*100 + Z, and 1.100.0 would have the code of 2.0.0.
# X is at least 1: 0.x is the preview, which is not released from here. And X
# is at most 209999, which keeps versionCode within 2100000000, the largest
# value Google Play takes and well inside the int Gradle computes it in.
#
# usage: check-android-release-tag.sh <tag> [<build.gradle>]
# env:   GITHUB_OUTPUT, if set, gets version=X.Y.Z once the tag passes.
set -euo pipefail
# [0-9] and the rest mean ASCII here, whatever the runner's locale.
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
  echo "usage: check-android-release-tag.sh <tag> [<build.gradle>]" >&2
  exit 2
fi
tag="$1"
gradle="${2:-$ROOT/android/app/build.gradle}"

fail() {
  echo "::error title=$1::$2"
  exit 1
}

re='^android-([0-9]+)\.([0-9]+)\.([0-9]+)$'
if ! [[ "$tag" =~ $re ]]; then
  fail "Not an Android release tag" "'$tag' is not android-X.Y.Z. Android releases are tagged android-X.Y.Z, for example android-1.0.0, and nothing else publishes one."
fi
x="${BASH_REMATCH[1]}"
y="${BASH_REMATCH[2]}"
z="${BASH_REMATCH[3]}"
version="$x.$y.$z"
for part in "$x" "$y" "$z"; do
  if [[ "$part" == 0?* ]]; then
    fail "Leading zero in the tag" "'$tag' has a leading zero. Write each part without one, as in android-1.2.0."
  fi
done
if [[ "$x" == 0 ]]; then
  fail "Preview version" "'$tag' is a 0.x version, which is the preview. Official Android releases start at android-1.0.0."
fi
# Lengths first: bash arithmetic would wrap a long enough number.
if [[ "${#y}" -gt 2 || "${#z}" -gt 2 ]]; then
  fail "Version part over 99" "'$tag' has a second or third part over 99. versionCode is X*10000 + Y*100 + Z, so those parts must stay at most 99 for it to keep growing."
fi
if [[ "${#x}" -gt 6 ]] || (( x > 209999 )); then
  fail "Major version too large" "'$tag' gives a versionCode over 2100000000. The first part must be at most 209999."
fi

if [[ ! -f "$gradle" ]]; then
  fail "No build.gradle" "$gradle does not exist, so there is no appVersion to check the tag against."
fi
# Every assignment of appVersion, so that a second one further down cannot
# quietly change the version this check read.
assignments="$(grep -nE '(^|[^[:alnum:]_])appVersion[[:space:]]*=([^=]|$)' "$gradle" || true)"
count=0
if [[ -n "$assignments" ]]; then
  count="$(wc -l <<<"$assignments")"
fi
if [[ "$count" -ne 1 ]]; then
  fail "Cannot read appVersion" "$gradle must assign appVersion exactly once, and does $count times."
fi
assign_re="^[0-9]+:[[:space:]]*def[[:space:]]+appVersion[[:space:]]*=[[:space:]]*'([^']*)'[[:space:]]*$"
if ! [[ "$assignments" =~ $assign_re ]]; then
  fail "Cannot read appVersion" "$gradle sets appVersion in a form this check does not read; it expects def appVersion = 'X.Y.Z' on a line of its own: $assignments"
fi
app_version="${BASH_REMATCH[1]}"
if [[ "$app_version" != "$version" ]]; then
  fail "Tag does not match appVersion" "the tag says $version and appVersion in $gradle is $app_version. Bump appVersion in the release pull request, or tag the commit that has it."
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  echo "version=$version" >> "$GITHUB_OUTPUT"
fi
echo "$tag releases FreeTunnel for Android $version, which is appVersion in $gradle."
