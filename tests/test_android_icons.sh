#!/usr/bin/env bash
# The Android app draws the desktop's icons: res/drawable/ic_*.xml are made from
# assets/icons/*.svg and assets/logo.svg by scripts/android-icons-from-svg.py (and the
# arrows and ticks ✓ ↓ ↑ ← from the outlines of those glyphs in
# assets/fonts/inter/Inter-Regular.ttf, the font the desktop draws them in), and this
# fails when one differs from what the script makes of its source, when one is
# missing or has no source, and when the app's code uses a drawable that does not exist.
# It has fixtures for each, then checks the real tree.
#
#   bash tests/test_android_icons.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SCRIPT="$ROOT/scripts/android-icons-from-svg.py"

indent() { sed 's/^/    /'; }

fail=0
# check <name> <expect: pass|fail> <assets dir> <drawable dir>
check() {
  local name=$1 expect=$2 assets=$3 res=$4 got=pass out
  out="$(python3 "$SCRIPT" --check --assets "$assets" --res "$res" 2>&1)" || got=fail
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
  fi
}

# used_drawables <res dir> <source dir>: fails for each R.drawable.ic_* the code names
# that the drawable directory does not have.
used_drawables() {
  local res=$1 src=$2 missing=0 name
  while IFS= read -r name; do
    [ -f "$res/$name.xml" ] || { echo "$name is used and is not in $res"; missing=1; }
  # The app's own R, not android.R.drawable's.
  done < <(grep -rhoP '(?<![.\w])R\.drawable\.ic_[a-z_0-9]+' "$src" | sed 's/^R\.drawable\.//' | sort -u)
  return "$missing"
}
check_used() {
  local name=$1 expect=$2 res=$3 src=$4 got=pass out
  out="$(used_drawables "$res" "$src")" || got=fail
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
  fi
}

fresh() {
  rm -rf "$TMP/assets" "$TMP/res"
  mkdir -p "$TMP/assets" "$TMP/res"
  cp -R "$ROOT/assets/icons" "$TMP/assets/icons"
  cp "$ROOT/assets/logo.svg" "$TMP/assets/logo.svg"
  mkdir -p "$TMP/assets/fonts/inter"
  cp "$ROOT/assets/fonts/inter/Inter-Regular.ttf" "$TMP/assets/fonts/inter/Inter-Regular.ttf"
  python3 "$SCRIPT" --assets "$TMP/assets" --res "$TMP/res" > /dev/null
}

fresh
check "drawables made from the SVG files" pass "$TMP/assets" "$TMP/res"

fresh
sed -i 's/android:strokeWidth="2"/android:strokeWidth="1.8"/' "$TMP/res/ic_settings.xml"
check "a drawable edited by hand" fail "$TMP/assets" "$TMP/res"

fresh
sed -i 's/x1="18" y1="6"/x1="17" y1="6"/' "$TMP/assets/icons/close.svg"
check "an SVG changed since its drawable was made" fail "$TMP/assets" "$TMP/res"

fresh
sed -i 's/fill="#d0d0d0"/fill="#d1d1d1"/' "$TMP/assets/logo.svg"
check "the logo changed" fail "$TMP/assets" "$TMP/res"

fresh
rm "$TMP/res/ic_export.xml"
check "a drawable that is missing" fail "$TMP/assets" "$TMP/res"

fresh
cp "$TMP/res/ic_log.xml" "$TMP/res/ic_servers.xml"
check "a drawable with no SVG" fail "$TMP/assets" "$TMP/res"

fresh
cp "$TMP/assets/icons/log.svg" "$TMP/assets/icons/brand-new.svg"
check "an SVG the script does not know" fail "$TMP/assets" "$TMP/res"

# The arrows and ticks are Inter's own glyphs.
fresh
sed -i 's/android:width="14\.[0-9]*dp"/android:width="15dp"/' "$TMP/res/ic_back.xml"
check "an arrow whose width was edited by hand" fail "$TMP/assets" "$TMP/res"

# Another weight of the font has other outlines: the drawables are of the one the desktop draws.
fresh
cp "$ROOT/assets/fonts/inter/Inter-Medium.ttf" "$TMP/assets/fonts/inter/Inter-Regular.ttf"
check "the font changed since the arrows were made" fail "$TMP/assets" "$TMP/res"

fresh
rm "$TMP/assets/fonts/inter/Inter-Regular.ttf"
check "no font to draw the arrows from" fail "$TMP/assets" "$TMP/res"

# Not a font at all: a script that reads the wrong thing must say so, not draw nothing.
fresh
head -c 4096 /dev/urandom > "$TMP/assets/fonts/inter/Inter-Regular.ttf"
check "a file that is no font" fail "$TMP/assets" "$TMP/res"

# What the tint leaves alone: the logo keeps its greys, the icons are drawn in white.
fresh
if grep -q 'android:fillColor="#d0d0d0"' "$TMP/res/ic_logo.xml" && ! grep -q 'tint' "$TMP/res/ic_logo.xml"; then
  echo "ok - the logo keeps its own greys"
else
  echo "not ok - the logo keeps its own greys"; fail=1
fi
# The speedometer's hub is filled, and the settings gear is Feather's, not a polygon.
if grep -q 'android:pathData="M12,10.6 A1.4,1.4' "$TMP/res/ic_ping.xml" && grep -q 'a1.65 1.65 0 0 0' "$TMP/res/ic_settings.xml" \
    && grep -q 'M8 18.93 A8 8 0 1 1 16 18.93' "$TMP/res/ic_ping.xml"; then
  echo "ok - the speedometer arc ends at 18.93, its hub is filled, the gear is Feather's"
else
  echo "not ok - the speedometer arc ends at 18.93, its hub is filled, the gear is Feather's"; fail=1
fi

# The size of each one is the ink of the character in Inter at the pixel size the desktop's QML sets it in, from the font's own
# numbers (units per em 2048: ✓ 1409 by 1028 units, ↓ and ↑ 1304 by 1514, ← 1496 by 1304), measured separately by Qt's QRawFont:
# ✓ 14 px, ↓ and ↑ 14 px, ← 20 px (SettingsPage.qml, HomePage.qml, AppPickerOverlay.qml, CreateConfigOverlay.qml).
fresh
if python3 - "$TMP/res" <<'PY'
import re, sys
from pathlib import Path
bad = 0
for name, width, height in (("ic_check", 9.63, 7.03), ("ic_arrow_down", 8.91, 10.35), ("ic_arrow_up", 8.91, 10.35), ("ic_back", 14.61, 12.73)):
    xml = (Path(sys.argv[1]) / (name + ".xml")).read_text(encoding="utf-8")
    got = re.search(r'android:width="([0-9.]+)dp" android:height="([0-9.]+)dp"', xml)
    if not got or abs(float(got.group(1)) - width) > 0.006 or abs(float(got.group(2)) - height) > 0.006:
        print("%s is %s, not %s by %s dp" % (name, got.groups() if got else None, width, height))
        bad = 1
sys.exit(bad)
PY
then
  echo "ok - ✓ ↓ ↑ ← are the size Inter draws them at the desktop's pixel sizes"
else
  echo "not ok - ✓ ↓ ↑ ← are the size Inter draws them at the desktop's pixel sizes"; fail=1
fi

mkdir -p "$TMP/src"
printf '%s\n' 'tabIcons = {R.drawable.ic_logo, R.drawable.ic_connection};' > "$TMP/src/Good.java"
check_used "drawables the code uses exist" pass "$TMP/res" "$TMP/src"
printf '%s\n' 'icon.setImageResource(R.drawable.ic_servers);' > "$TMP/src/Bad.java"
rm -f "$TMP/res/ic_servers.xml"
check_used "a drawable the code uses and the app lacks" fail "$TMP/res" "$TMP/src"

check "the repository's drawables" pass "$ROOT/assets" "$ROOT/android/app/src/main/res/drawable"
check_used "the repository's code uses drawables that exist" pass "$ROOT/android/app/src/main/res/drawable" "$ROOT/android/app/src/main/java"
exit "$fail"
