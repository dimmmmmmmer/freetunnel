#!/usr/bin/env bash
# Checks that the Android APK installs and launches on Android TV, as TV
# launchers and app stores read it: aapt2 dump badging of the built APK, after
# the TrustTunnel AAR's manifest is merged in. Lint checks the app's own
# manifest; only this sees what a library adds, such as a permission that
# implies hardware a TV doesn't have.
#
#   bash tests/test_android_tv_manifest.sh            tests the checks on fixtures
#   bash tests/test_android_tv_manifest.sh app.apk    then checks that APK, with
#                                                     $AAPT2 or Build Tools 35.0.0's
set -euo pipefail

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# tv_ready <badging>: prints what keeps the APK off TVs, and fails if anything does.
tv_ready() {
  local badging=$1 found=0 feature abi
  if ! grep -qE "^leanback-launchable-activity: name='org\.freetunnel\.android\.MainActivity'" "$badging"; then
    echo "MainActivity has no TV launcher entry (LEANBACK_LAUNCHER)"; found=1
  fi
  if ! grep -qE "^application: .* banner='[^']+'" "$badging"; then
    echo "the application has no banner for the TV launcher"; found=1
  fi
  for feature in android.software.leanback android.hardware.touchscreen; do
    if ! grep -qxF "uses-feature-not-required: name='$feature'" "$badging"; then
      echo "$feature is not declared as not required"; found=1
    fi
  done
  # A required feature, declared or implied by a permission, hides the app from
  # every TV without it. Touchscreen not being declared implies faketouch.
  if grep -E "^uses-(implied-)?feature: " "$badging"; then
    echo "the lines above require features that TVs may not have"; found=1
  fi
  # Most TVs run 32-bit ARM, and the TV emulator is 32-bit x86.
  for abi in armeabi-v7a x86; do
    if ! grep -qE "^native-code: (.* )?'$abi'( |$)" "$badging"; then
      echo "no native code for $abi"; found=1
    fi
  done
  return "$found"
}

# What aapt2 prints for the app as it should be, trimmed to the lines checked
# and their neighbours.
cat > "$TMP/good" <<'EOF'
package: name='org.freetunnel.android' versionCode='900' versionName='0.9.0' platformBuildVersionName='15' platformBuildVersionCode='35' compileSdkVersion='35' compileSdkVersionCodename='15'
sdkVersion:'26'
targetSdkVersion:'35'
uses-permission: name='android.permission.INTERNET'
application-label:'FreeTunnel'
application: label='FreeTunnel' icon='res/drawable-nodpi-v4/logo.png' banner='res/drawable-xhdpi-v4/banner.png'
launchable-activity: name='org.freetunnel.android.MainActivity'  label='FreeTunnel' icon=''
leanback-launchable-activity: name='org.freetunnel.android.MainActivity'  label='FreeTunnel' icon='' banner='res/drawable-xhdpi-v4/banner.png'
uses-feature-not-required: name='android.software.leanback'
uses-feature-not-required: name='android.hardware.touchscreen'
main
supports-screens: 'small' 'normal' 'large' 'xlarge'
native-code: 'arm64-v8a' 'armeabi-v7a' 'x86' 'x86_64'
EOF

indent() { sed 's/^/    /'; }

fail=0
n=0
# case_ <name> <expect: pass|fail> <sed script applied to the good badging> [output must contain]
case_() {
  local name=$1 expect=$2 edit=$3 want=${4:-} got=pass out
  n=$((n + 1))
  sed -e "$edit" "$TMP/good" > "$TMP/case$n"
  out="$(tv_ready "$TMP/case$n")" || got=fail
  if [ "$got" != "$expect" ] || { [ -n "$want" ] && ! grep -qF -- "$want" <<<"$out"; }; then
    echo "not ok $n - $name: expected $expect${want:+ with output saying: $want}, got $got:"
    indent <<<"$out"
    fail=1
  else
    echo "ok $n - $name"
  fi
}

case_ "ready for TVs" pass ''
case_ "no TV launcher entry" fail '/^leanback-launchable-activity:/d' "no TV launcher entry"
case_ "TV launcher entry for another activity" fail \
  "s/^leanback-launchable-activity: name='[^']*'/leanback-launchable-activity: name='org.freetunnel.android.InstallResultActivity'/" \
  "no TV launcher entry"
case_ "no banner" fail "s/ banner='[^']*'//g" "no banner"
case_ "leanback required" fail \
  "s/^uses-feature-not-required: name='android.software.leanback'/uses-feature: name='android.software.leanback'/" \
  "android.software.leanback is not declared as not required"
case_ "touchscreen not declared" fail "/name='android.hardware.touchscreen'/d" \
  "android.hardware.touchscreen is not declared as not required"
case_ "touchscreen not declared, so faketouch is implied" fail \
  "/name='android.hardware.touchscreen'/c\\
uses-feature: name='android.hardware.faketouch'\\
uses-implied-feature: name='android.hardware.faketouch' reason='default feature for all apps'" \
  "uses-implied-feature: name='android.hardware.faketouch'"
case_ "a library's permission implies Wi-Fi" fail \
  "/^main\$/i\\
uses-feature: name='android.hardware.wifi'\\
uses-implied-feature: name='android.hardware.wifi' reason='requested android.permission.ACCESS_WIFI_STATE permission'" \
  "require features that TVs may not have"
case_ "a library requires a camera" fail "/^main\$/i\\
uses-feature: name='android.hardware.camera'" "uses-feature: name='android.hardware.camera'"
case_ "a feature not required is fine" pass "/^main\$/i\\
uses-feature-not-required: name='android.hardware.camera'"
case_ "no 32-bit ARM code" fail "s/ 'armeabi-v7a'//" "no native code for armeabi-v7a"
case_ "x86_64 but no x86" fail "s/ 'x86' / /" "no native code for x86"
case_ "x86 last" pass "s/ 'x86' 'x86_64'/ 'x86_64' 'x86'/"

if [ "$#" -gt 0 ]; then
  aapt2=${AAPT2:-${ANDROID_HOME:-}/build-tools/35.0.0/aapt2}
  "$aapt2" dump badging "$1" > "$TMP/apk"
  if tv_ready "$TMP/apk"; then
    echo "ok - $1 is ready for Android TV"
  else
    echo "not ok - $1 is not ready for Android TV. aapt2 dump badging printed:"
    indent < "$TMP/apk"
    fail=1
  fi
fi

exit "$fail"
