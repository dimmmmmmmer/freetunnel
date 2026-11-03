#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
# Use the same installed runtime for both devices. SE exercises the narrowest
# supported iPhone layout; iPad checks tablet popovers and landscape forms.
runtime="$(xcrun simctl list runtimes -j | python3 -c 'import json,sys; print(next(r["identifier"] for r in json.load(sys.stdin)["runtimes"] if r["isAvailable"] and "iOS" in r["name"]))')"
mkdir -p "$root/ios/ui-results"
result=0
for family in small-phone ipad; do
  kind="$(xcrun simctl list devicetypes -j | python3 -c 'import json,sys; rows=json.load(sys.stdin)["devicetypes"]; family=sys.argv[1]; candidates=[r for r in rows if ("iPhone SE (3rd generation)" in r["name"] if family=="small-phone" else "iPad (A16)" in r["name"])]; assert candidates, "Required device type unavailable"; print(candidates[0]["identifier"])' "$family")"
  device="$(xcrun simctl create "FreeTunnel-$family" "$kind" "$runtime")"
  xcrun simctl boot "$device"
  xcrun simctl bootstatus "$device" -b
  xcrun simctl ui "$device" appearance light
  xcrun simctl status_bar "$device" override --time '9:41' --dataNetwork wifi --wifiMode active --wifiBars 3 --batteryState charged --batteryLevel 100
  if ! xcodebuild -project "$root/ios/FreeTunnel.xcodeproj" -scheme FreeTunnel \
      -destination "platform=iOS Simulator,id=$device" -configuration Debug \
      -derivedDataPath "$root/ios/DerivedData" -resultBundlePath "$root/ios/ui-results/$family.xcresult" \
      CODE_SIGNING_ALLOWED=NO ARCHS=arm64 -parallel-testing-enabled NO \
      -test-timeouts-enabled YES -default-test-execution-time-allowance 180 \
      -maximum-test-execution-time-allowance 240 test; then
    result=1
  fi
  if test -d "$root/ios/ui-results/$family.xcresult"; then
    xcrun xcresulttool export attachments --path "$root/ios/ui-results/$family.xcresult" \
      --output-path "$root/ios/ui-results/$family-screenshots" || result=1
  fi
  xcrun simctl shutdown "$device"
  xcrun simctl delete "$device"
done
exit "$result"
