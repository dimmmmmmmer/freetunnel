#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
device="$(xcrun simctl list devices available -j | python3 -c 'import json,sys; devices=json.load(sys.stdin)["devices"]; print(next(d["udid"] for runtime, rows in devices.items() if "iOS" in runtime for d in rows if d["isAvailable"] and d["name"].startswith("iPhone")))')"
xcrun simctl boot "$device" || true
xcrun simctl bootstatus "$device" -b
xcrun simctl install "$device" "$root/ios/DerivedData/Build/Products/Debug-iphonesimulator/FreeTunnel.app"
mkdir -p "$root/ios/previews"
for appearance in light dark; do
 xcrun simctl ui "$device" appearance "$appearance"
 for screen in connection servers routing routing-geo settings logs import new-server edit-server add-list licenses; do
  xcrun simctl terminate "$device" org.freetunnel.ios || true
  tab="$screen"
  geo=0
  form=""
  case "$screen" in
    import|new-server|edit-server|add-list|licenses) form="$screen"; tab=servers ;;
  esac
  if [[ "$screen" == routing-geo ]]; then tab=routing; geo=1; fi
  SIMCTL_CHILD_FT_PREVIEW_FORM="$form" SIMCTL_CHILD_FT_PREVIEW_GEO="$geo" SIMCTL_CHILD_FT_PREVIEW_TAB="$tab" xcrun simctl launch "$device" org.freetunnel.ios --ui-preview -AppleLanguages '(ru)'
  sleep 3
  xcrun simctl io "$device" screenshot "$root/ios/previews/$screen-$appearance.png"
done
done
