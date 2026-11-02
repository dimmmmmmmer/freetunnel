#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
device="$(xcrun simctl list devices available -j | python3 -c 'import json,sys; devices=json.load(sys.stdin)["devices"]; print(next(d["udid"] for runtime, rows in devices.items() if "iOS" in runtime for d in rows if d["isAvailable"] and d["name"].startswith("iPhone")))')"
xcrun simctl boot "$device" || true
xcrun simctl bootstatus "$device" -b
xcrun simctl install "$device" "$root/ios/DerivedData/Build/Products/Debug-iphonesimulator/FreeTunnel.app"
mkdir -p "$root/ios/previews"
for screen in connection servers routing settings logs; do
  xcrun simctl terminate "$device" org.freetunnel.ios || true
  SIMCTL_CHILD_FT_PREVIEW_TAB="$screen" xcrun simctl launch "$device" org.freetunnel.ios --ui-preview -AppleLanguages '(ru)'
  sleep 3
  xcrun simctl io "$device" screenshot "$root/ios/previews/$screen.png"
done
