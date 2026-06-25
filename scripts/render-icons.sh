#!/usr/bin/env bash
# Rasterize assets/logo.svg into bundle/installer icons.
# macOS .icns is finalized with iconutil on the macOS CI runner (see build.yml).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
svg=$root/assets/logo.svg
out=${1:-$root/assets}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Leave ~12% transparent margin so macOS Finder/Dock don't clip the mark in a
# hard square (Apple icons are masked, but edge-to-edge art reads as a box).
pad_png() {
  local size=$1 dest=$2
  local inner=$((size * 82 / 100))
  rsvg-convert -w "$inner" -h "$inner" "$svg" -o "$tmp/mark.png"
  # Force RGBA (TrueColorAlpha): the mark is pure grey, so without this
  # ImageMagick stores grayscale PNGs and drops the alpha channel on the .ico's
  # 256px layer — which then renders with a white background at large sizes.
  convert -size "${size}x${size}" xc:none "$tmp/mark.png" -gravity center -composite \
    -type TrueColorAlpha "$dest"
}

for s in 16 32 48 64 128 256 512 1024; do
  pad_png "$s" "$tmp/logo-$s.png"
done

mkdir -p "$out"
cp "$tmp/logo-256.png" "$out/logo-256.png"
cp "$tmp/logo-512.png" "$out/logo-512.png"
cp "$tmp/logo-128.png" "$out/logo-128.png"
cp "$tmp/logo-48.png"  "$out/logo-48.png"
cp "$tmp/logo-256.png" "$out/logo.png"

# Fallback .icns for local builds; CI macOS job rebuilds via iconutil.
png2icns "$out/logo.icns" \
  "$tmp/logo-16.png" "$tmp/logo-32.png" "$tmp/logo-48.png" \
  "$tmp/logo-128.png" "$tmp/logo-256.png" "$tmp/logo-512.png" "$tmp/logo-1024.png"

convert "$tmp/logo-16.png" "$tmp/logo-32.png" "$tmp/logo-48.png" \
  "$tmp/logo-64.png" "$tmp/logo-128.png" "$tmp/logo-256.png" \
  -type TrueColorAlpha "$out/logo.ico"

# Apple iconset for iconutil (proper Finder/Dock .icns on macOS).
ic=$out/icon.iconset
rm -rf "$ic"
mkdir "$ic"
cp "$tmp/logo-16.png"  "$ic/icon_16x16.png"
cp "$tmp/logo-32.png"  "$ic/icon_16x16@2x.png"
cp "$tmp/logo-32.png"  "$ic/icon_32x32.png"
cp "$tmp/logo-64.png"  "$ic/icon_32x32@2x.png"
cp "$tmp/logo-128.png" "$ic/icon_128x128.png"
cp "$tmp/logo-256.png" "$ic/icon_128x128@2x.png"
cp "$tmp/logo-256.png" "$ic/icon_256x256.png"
cp "$tmp/logo-512.png" "$ic/icon_256x256@2x.png"
cp "$tmp/logo-512.png" "$ic/icon_512x512.png"
cp "$tmp/logo-1024.png" "$ic/icon_512x512@2x.png"

# NSIS Modern UI bitmaps (installer wizard branding).
convert -size 164x314 xc:'#ececec' \
  \( "$tmp/logo-128.png" \) -gravity center -composite \
  BMP3:"$out/installer-welcome.bmp"
convert -size 150x57 xc:'#ececec' \
  \( "$tmp/logo-48.png" \) -gravity center -composite \
  BMP3:"$out/installer-header.bmp"

ls -la "$out"
