#!/usr/bin/env bash
# Build a signed release APK on your own machine, the way the release workflow
# does: Gradle runs the release tests and lint and builds the APK unsigned,
# with none of the signing variables in its environment, and
# scripts/sign-android-release.sh signs it with Build Tools alone and checks
# it. The certificate it must carry is the keystore's own, so a test keystore
# works as well as the release one.
#
# Signing credentials come from the caller's environment, never from the repo:
# ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_PASSWORD and ANDROID_KEY_PASSWORD;
# ANDROID_KEY_ALIAS, by default freetunnel. Android SDK 35 and Build Tools
# 35.0.0 under ANDROID_HOME or ANDROID_SDK_ROOT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
for name in ANDROID_KEYSTORE_PATH ANDROID_KEYSTORE_PASSWORD ANDROID_KEY_PASSWORD; do
  if [[ -z "${!name:-}" ]]; then
    echo "Missing required signing variable: $name" >&2
    exit 1
  fi
done
[[ -f "$ANDROID_KEYSTORE_PATH" ]] || { echo "Signing keystore does not exist" >&2; exit 1; }
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[[ -n "$SDK" ]] || { echo "Set ANDROID_HOME to Android SDK 35" >&2; exit 1; }
VERSION="$(sed -nE "s/^[[:space:]]*def[[:space:]]+appVersion[[:space:]]*=[[:space:]]*'([^']*)'[[:space:]]*\$/\1/p" \
  "$ROOT/android/app/build.gradle")"
[[ -n "$VERSION" ]] || { echo "Cannot read appVersion from android/app/build.gradle" >&2; exit 1; }
# Nothing but the signing below sees the signing variables: not the download
# of the TrustTunnel runtime, not Gradle.
NO_SIGNING=(env -u ANDROID_KEYSTORE_PATH -u ANDROID_KEYSTORE_PASSWORD -u ANDROID_KEY_PASSWORD -u ANDROID_KEY_ALIAS)
"${NO_SIGNING[@]}" "$ROOT/scripts/prepare-trusttunnel-android.sh"
"${NO_SIGNING[@]}" \
  "$ROOT/android/gradlew" -p "$ROOT/android" --no-daemon --max-workers=1 \
  -Dorg.gradle.jvmargs='-Xmx1024m -Dfile.encoding=UTF-8' \
  -Pkotlin.compiler.execution.strategy=in-process \
  :app:testReleaseUnitTest :app:lintRelease :app:assembleRelease
UNSIGNED="$ROOT/android/app/build/outputs/apk/release/app-release-unsigned.apk"
APK="$ROOT/android/app/build/outputs/apk/release/app-release.apk"
# The keystore's certificate is the pin, so the APK must be signed with that
# key and no other. keytool reads the password from the environment, never
# from the command line.
KEYTOOL="${JAVA_HOME:+$JAVA_HOME/bin/}keytool"
CERT="$(mktemp)"
PIN="$(mktemp)"
trap 'rm -f "$CERT" "$PIN"' EXIT
"$KEYTOOL" -exportcert -keystore "$ANDROID_KEYSTORE_PATH" -alias "${ANDROID_KEY_ALIAS:-freetunnel}" \
  -storepass:env ANDROID_KEYSTORE_PASSWORD -file "$CERT"
shasum -a 256 "$CERT" | awk '{print $1}' > "$PIN"
BUILD_TOOLS="$SDK/build-tools/35.0.0" bash "$ROOT/scripts/sign-android-release.sh" "$UNSIGNED" "$APK" "$PIN" "$VERSION"
echo "Signed release APK: $APK"
