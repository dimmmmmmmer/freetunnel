#!/usr/bin/env bash
# Render home-page preview PNGs. Run from repo root after building build-preview/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREVIEW="${PREVIEW:-$ROOT/build-preview/preview}"
OUT="${OUT:-$ROOT/build-preview}"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_XHR_ALLOW_FILE_READ=1

mkdir -p "$OUT"
for s in empty config connecting disconnecting connected; do
  QML_PREVIEW_SCENARIO="$s" "$PREVIEW" "$OUT/freetunnel-home-$s.png" 0
done

echo "Wrote $OUT/freetunnel-home-{empty,config,connecting,disconnecting,connected}.png"
