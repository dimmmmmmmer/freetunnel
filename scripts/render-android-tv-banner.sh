#!/usr/bin/env bash
# Render android/art/tv-banner.svg into the Android TV launcher banner,
# android/app/src/main/res/drawable-xhdpi/banner.png: 320x180, opaque RGB.
# The wordmark's font comes from fontconfig, which differs between machines, so
# the PNG is committed rather than built. After changing the SVG, rerun this and
# commit both. Needs DejaVu Sans Bold, and librsvg: rsvg-convert with ImageMagick,
# as scripts/render-icons.sh uses, or else Python's GObject bindings to it.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
svg=$root/android/art/tv-banner.svg
out=${1:-$root/android/app/src/main/res/drawable-xhdpi/banner.png}

# Without the font, librsvg falls back to another one without a word.
if ! fc-list ':family=DejaVu Sans:style=Bold' | grep -q .; then
  echo "DejaVu Sans Bold is not installed" >&2
  exit 1
fi
if command -v rsvg-convert >/dev/null && command -v convert >/dev/null; then
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  rsvg-convert -w 320 -h 180 "$svg" -o "$tmp/banner.png"
  # A banner has no transparency, so the PNG gets no alpha channel.
  convert "$tmp/banner.png" -background '#181818' -alpha remove -alpha off \
    -type TrueColor -define png:color-type=2 "$out"
else
  python3 - "$svg" "$out" <<'EOF'
import sys
import gi
gi.require_version('Rsvg', '2.0')
from gi.repository import Rsvg
import cairo
surface = cairo.ImageSurface(cairo.FORMAT_RGB24, 320, 180)
viewport = Rsvg.Rectangle()
viewport.x, viewport.y, viewport.width, viewport.height = 0, 0, 320, 180
Rsvg.Handle.new_from_file(sys.argv[1]).render_document(cairo.Context(surface), viewport)
surface.write_to_png(sys.argv[2])
EOF
fi
echo "Wrote $out"
