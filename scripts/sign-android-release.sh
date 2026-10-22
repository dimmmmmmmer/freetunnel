#!/usr/bin/env bash
# Sign an unsigned Android release APK, and check it the way Android and the
# in-app updater will, before anything publishes it. Run by the sign job of
# .github/workflows/android-release.yml with the release keystore, by
# android.yml with a throwaway keystore on every Android pull request, and by
# scripts/build-android-release.sh for a local build.
#
# Only Android SDK Build Tools run here: zipalign, apksigner and aapt2. Gradle
# builds the APK unsigned, in a job that has no secrets, so neither it nor the
# plugins and dependencies it loads ever see the keystore or its passwords.
#
# In this order, and the signed APK reaches <out.apk> only when all of it
# passed:
# - The pin's format (check-android-signer.sh), so nothing is signed for a
#   release that could not publish.
# - The unsigned APK is aligned: every uncompressed entry on 4 bytes, and the
#   uncompressed native libraries on 16 KiB. The TrustTunnel AAR brings .so
#   files, and with minSdk 26 AGP stores them uncompressed so that Android maps
#   them straight from the APK; on a device with 16 KiB pages that works only
#   at 16 KiB. AGP 8.5.1 and later aligns them so. zipalign must run before
#   apksigner, never after, since any change to a signed APK breaks its
#   signature; so this only checks.
# - Its package, versionName and versionCode are this release's (aapt2), so a
#   wrong build is never signed with the release key.
# - apksigner signs with v2 and v3 only, set explicitly rather than left to
#   apksigner's defaults. Android 7 and later read v2, so with minSdk 26 a v1
#   (JAR) signature adds nothing. v4 would be an .idsig file beside the APK,
#   used only for adb's incremental installs. A debuggable APK is refused.
# - apksigner verify --verbose: the APK verifies, with v2 and v3, and has one
#   signer. Then the alignment again, since apksigner rewrites the alignment of
#   uncompressed entries as it signs.
# - check-android-signer.sh: that signer is the pinned certificate.
# - aapt2 again on the signed APK.
#
# usage: sign-android-release.sh <unsigned.apk> <out.apk> <pin-file> <version>
#        <version> is X.Y.Z, the APK's versionName; versionCode must be
#        X*10000 + Y*100 + Z.
# env:   ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_PASSWORD;
#        ANDROID_KEY_ALIAS, by default freetunnel. apksigner reads the two
#        passwords from the environment itself, so they are never arguments.
#        BUILD_TOOLS, the directory to take zipalign, apksigner and aapt2 from;
#        by default Build Tools 35.0.0 under ANDROID_HOME or ANDROID_SDK_ROOT.
set -euo pipefail
export LC_ALL=C

SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
if [[ "$#" -ne 4 ]]; then
  echo "usage: sign-android-release.sh <unsigned.apk> <out.apk> <pin-file> <version>" >&2
  exit 2
fi
unsigned="$1"
out="$2"
pin_file="$3"
version="$4"
package=org.freetunnel.android

fail() {
  echo "::error title=$1::$2"
  exit 1
}

# No leading zeros, so a version has one spelling, and Y and Z at most 99, so
# versionCode grows with the version. X is short enough for bash arithmetic.
version_re='^(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]?)\.(0|[1-9][0-9]?)$'
if ! [[ "$version" =~ $version_re ]]; then
  fail "Bad version" "'$version' is not X.Y.Z with no leading zeros and Y and Z at most 99."
fi
version_code=$(( BASH_REMATCH[1] * 10000 + BASH_REMATCH[2] * 100 + BASH_REMATCH[3] ))

[[ -f "$unsigned" ]] || fail "No APK" "$unsigned does not exist"
[[ ! -d "$out" ]] || fail "Output is a directory" "$out is a directory; name the signed APK's file"
[[ ! "$out" -ef "$unsigned" ]] || fail "Output is the input" "$out is the unsigned APK; write the signed one elsewhere"
# Whatever is at <out.apk> after a failure is not left over from an earlier run.
rm -f "$out"

bash "$SCRIPTS/check-android-signer.sh" "$pin_file"

missing=""
for name in ANDROID_KEYSTORE_PATH ANDROID_KEYSTORE_PASSWORD ANDROID_KEY_PASSWORD; do
  if [[ -z "${!name:-}" ]]; then
    missing="$missing $name"
  fi
done
[[ -z "$missing" ]] || fail "Signing variables missing" "Not set:$missing"
[[ -f "$ANDROID_KEYSTORE_PATH" ]] || fail "No keystore" "ANDROID_KEYSTORE_PATH is $ANDROID_KEYSTORE_PATH, which does not exist"
alias="${ANDROID_KEY_ALIAS:-freetunnel}"

tools="${BUILD_TOOLS:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}/build-tools/35.0.0}"
for tool in zipalign apksigner aapt2; do
  [[ -x "$tools/$tool" ]] || fail "No Build Tools" "$tools/$tool is not an executable. Install Build Tools 35.0.0: sdkmanager \"build-tools;35.0.0\""
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# aligned <apk> <what>
aligned() {
  if ! "$tools/zipalign" -c -P 16 -v 4 "$1" > "$tmp/zipalign" 2>&1; then
    grep -E 'BAD|Verif' "$tmp/zipalign" >&2 || true
    fail "APK not aligned" "$2 is not aligned for Android: zipalign -c -P 16 -v 4 refuses it. Uncompressed native libraries need 16 KiB, every other uncompressed entry 4 bytes."
  fi
  echo "$2: $(tail -n 1 "$tmp/zipalign")"
}

# field <name> <aapt2's package line>: the value of name='...' in it.
field() {
  local re="[[:space:]]$1='([^']*)'"
  if [[ "$2" =~ $re ]]; then
    printf '%s' "${BASH_REMATCH[1]}"
  fi
}

# badging <apk> <what>: the package, versionName and versionCode aapt2 reads
# from the manifest. The updater installs only org.freetunnel.android, with
# the release's versionName and a versionCode above the installed one.
badging() {
  if ! "$tools/aapt2" dump badging "$1" > "$tmp/badging" 2> "$tmp/badging.err"; then
    head -n 5 "$tmp/badging.err" >&2
    fail "Cannot read the APK" "aapt2 dump badging cannot read $2"
  fi
  local line name code version_name
  line="$(grep -m 1 '^package: ' "$tmp/badging" || true)"
  name="$(field name "$line")"
  code="$(field versionCode "$line")"
  version_name="$(field versionName "$line")"
  if [[ "$name" != "$package" || "$version_name" != "$version" || "$code" != "$version_code" ]]; then
    fail "Wrong APK" "$2 is package '$name', versionName '$version_name', versionCode '$code'; this release is $package, $version, $version_code. Installed apps would refuse it."
  fi
  echo "$2: $package $version, versionCode $version_code"
}

aligned "$unsigned" "the unsigned APK"
badging "$unsigned" "the unsigned APK"

if ! "$tools/apksigner" sign \
       --ks "$ANDROID_KEYSTORE_PATH" --ks-key-alias "$alias" \
       --ks-pass env:ANDROID_KEYSTORE_PASSWORD --key-pass env:ANDROID_KEY_PASSWORD \
       --v1-signing-enabled false --v2-signing-enabled true \
       --v3-signing-enabled true --v4-signing-enabled false \
       --debuggable-apk-permitted false \
       --out "$tmp/signed.apk" "$unsigned"; then
  fail "Signing failed" "apksigner could not sign $unsigned with the key $alias from the keystore"
fi

if ! "$tools/apksigner" verify --verbose "$tmp/signed.apk" > "$tmp/verify" 2>&1; then
  cat "$tmp/verify" >&2
  fail "Signed APK does not verify" "apksigner verify refuses the APK it has just signed"
fi
cat "$tmp/verify"
for want in "Verifies" \
            "Verified using v2 scheme (APK Signature Scheme v2): true" \
            "Verified using v3 scheme (APK Signature Scheme v3): true" \
            "Number of signers: 1"; do
  grep -qxF -- "$want" "$tmp/verify" \
    || fail "Unexpected signature" "apksigner verify --verbose does not say '$want' for the signed APK"
done
aligned "$tmp/signed.apk" "the signed APK"
APKSIGNER="$tools/apksigner" bash "$SCRIPTS/check-android-signer.sh" "$pin_file" "$tmp/signed.apk"
badging "$tmp/signed.apk" "the signed APK"

mkdir -p "$(dirname "$out")"
mv -f "$tmp/signed.apk" "$out"
echo "Signed $out: $package $version ($version_code), certificate $(cat "$pin_file")"
