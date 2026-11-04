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
 for screen in connection connection-active servers servers-active routing routing-geo settings settings-bottom logs logs-filled import new-server new-server-bottom edit-server edit-server-bottom add-list add-list-menu routing-mode geo-direction geo-interval settings-language settings-theme server-export; do
  xcrun simctl terminate "$device" org.freetunnel.ios || true
  tab="$screen"
  geo=0
  bottom=0
  form=""
  scenario=""
  menu=""
  case "$screen" in
    add-list-menu) tab=routing; geo=1; menu=add-list ;;
    routing-mode) tab=routing; menu=mode ;;
    geo-direction) tab=routing; geo=1; menu=direction ;;
    geo-interval) tab=routing; geo=1; menu=interval ;;
    settings-language) tab=settings; menu=language ;;
    settings-theme) tab=settings; menu=theme ;;
    server-export) tab=servers; menu=export ;;
    connection-active) tab=connection; scenario=connected ;;
    servers-active) tab=servers; scenario=connected ;;
    logs-filled) tab=logs; scenario=logs-filled ;;
    settings-bottom) tab=settings; bottom=1 ;;
    new-server-bottom|edit-server-bottom) form="${screen%-bottom}"; tab=servers; bottom=1 ;;
    add-list) form=add-list; tab=routing; geo=1 ;;
    import|new-server|edit-server) form="$screen"; tab=servers ;;
  esac
  if [[ "$screen" == routing-geo ]]; then tab=routing; geo=1; fi
  SIMCTL_CHILD_FT_PREVIEW_MENU="$menu" SIMCTL_CHILD_FT_PREVIEW_SCENARIO="$scenario" SIMCTL_CHILD_FT_PREVIEW_BOTTOM="$bottom" SIMCTL_CHILD_FT_PREVIEW_FORM="$form" SIMCTL_CHILD_FT_PREVIEW_GEO="$geo" SIMCTL_CHILD_FT_PREVIEW_TAB="$tab" xcrun simctl launch "$device" org.freetunnel.ios --ui-preview -AppleLanguages '(ru)'
  sleep 3
  xcrun simctl io "$device" screenshot "$root/ios/previews/$screen-$appearance.png"
done
done
