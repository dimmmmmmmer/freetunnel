#!/usr/bin/env bash
# The Android app has the desktop's colours: Theme.java holds every token of
# qml/Main.qml's `theme` object, in both themes, under the same name, and the values
# the desktop works out from them (Qt.darker, Qt.lighter, Qt.rgba). This fails when a
# value differs, when the QML has a token Theme.java lacks, and when Theme.java has
# a colour that is neither a token nor a derived value this script knows. So a
# change to the desktop's palette fails here, instead of leaving the two apps apart.
#
#   bash tests/test_android_theme_matches_desktop.sh    the checks on fixtures, then the real files
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/compare.py" <<'PY'
import colorsys
import re
import sys

qml_path, java_path = sys.argv[1], sys.argv[2]
qml = open(qml_path, encoding="utf-8").read()
java = open(java_path, encoding="utf-8").read()
problems = []

def argb(hex6):
    return 0xFF000000 | int(hex6, 16)

# The tokens: readonly property color X: dark ? "#aaaaaa" : "#bbbbbb"
tokens = {}
for name, d, l in re.findall(r'readonly property color (\w+):\s*dark\s*\?\s*"#([0-9a-fA-F]{6})"\s*:\s*"#([0-9a-fA-F]{6})"', qml):
    tokens[name] = (argb(d), argb(l))

# infoBg is Qt.rgba(r, g, b, a) in both themes, one expression over two lines.
def rgba(r, g, b, a):
    q = lambda v: int(float(v) * 255 + 0.5)
    return q(a) << 24 | q(r) << 16 | q(g) << 8 | q(b)

m = re.search(r'readonly property color (\w+):\s*dark\s*\?\s*Qt\.rgba\(([^)]*)\)\s*:\s*Qt\.rgba\(([^)]*)\)', qml)
if m:
    tokens[m.group(1)] = tuple(rgba(*[p.strip() for p in part.split(",")]) for part in (m.group(2), m.group(3)))
if len(tokens) < 17:
    problems.append("found only %d tokens in %s; has the theme's layout changed?" % (len(tokens), qml_path))

# Theme.java: name = pick(dark, 0xAARRGGBB, 0xAARRGGBB);
mine = {}
for name, d, l in re.findall(r'\b(\w+)\s*=\s*pick\(\s*dark\s*,\s*0x([0-9A-Fa-f]{8})\s*,\s*0x([0-9A-Fa-f]{8})\s*\)\s*;', java):
    mine[name] = (int(d, 16), int(l, 16))

def hexa(v):
    return "#%08x" % v

for name, (d, l) in sorted(tokens.items()):
    if name not in mine:
        problems.append("Theme.java has no %s (QML: %s / %s)" % (name, hexa(d), hexa(l)))
    elif mine[name] != (d, l):
        problems.append("%s: Theme.java has %s / %s, the QML %s / %s" % (name, hexa(mine[name][0]), hexa(mine[name][1]), hexa(d), hexa(l)))

# What the desktop works out from them. Qt computes in 16 bits; within one level of
# 8 is what a float computation can promise, and the values in Theme.java are Qt's.
def rgb(c):
    return [(c >> 16) & 255, (c >> 8) & 255, c & 255]

def darker(c, percent):
    h, s, v = colorsys.rgb_to_hsv(*[x / 255 for x in rgb(c)])
    return 0xFF000000 | int(sum(int(x * 255 + 0.5) << sh for x, sh in zip(colorsys.hsv_to_rgb(h, s, v * 100 / percent), (16, 8, 0))))

def lighter(c, percent):
    h, s, v = colorsys.rgb_to_hsv(*[x / 255 for x in rgb(c)])
    v = v * percent / 100
    if v > 1:
        s = max(0, s - (v - 1))
        v = 1
    return 0xFF000000 | int(sum(int(x * 255 + 0.5) << sh for x, sh in zip(colorsys.hsv_to_rgb(h, s, v), (16, 8, 0))))

derived = {
    "primaryHover": ("accent", lambda c: darker(c, 112), "Qt.darker(accent, 1.12)"),
    "destructiveHover": ("danger", lambda c: darker(c, 115), "Qt.darker(danger, 1.15)"),
    "dangerLinkHover": ("danger", lambda c: lighter(c, 125), "Qt.lighter(danger, 1.25)"),
    "offKnob": ("toggleOff", lambda c: lighter(c, 135), "Qt.lighter(toggleOff, 1.35)"),
}
for name, (source, work, how) in derived.items():
    if name not in mine:
        problems.append("Theme.java has no %s (%s)" % (name, how))
        continue
    for i, theme in enumerate(("dark", "light")):
        want = work(tokens[source][i]) if source in tokens else None
        have = mine[name][i]
        if want is not None and any(abs(a - b) > 1 for a, b in zip(rgb(want), rgb(have))):
            problems.append("%s in the %s theme is %s, but %s there is about %s" % (name, theme, hexa(have), how, hexa(want)))

# The other numbers the QML spells out: the toggle's border, the dim, the shadow.
def need(pattern, where, text, what):
    if not re.search(pattern, text):
        problems.append("%s: the QML no longer has %s" % (where, what))

toggle = open(qml_path.replace("Main.qml", "Toggle.qml"), encoding="utf-8").read() if qml_path.endswith("Main.qml") else ""
if toggle:
    need(r'border\.color:\s*Qt\.rgba\(0,\s*0,\s*0,\s*0\.18\)', "toggleOffBorder", toggle, "a 0.18 black border on an off toggle")
    need(r'Qt\.lighter\(root\.offColor,\s*1\.35\)', "offKnob", toggle, "an off knob of Qt.lighter(offColor, 1.35)")
    need(r'color:\s*root\.checked\s*\?\s*"white"', "onKnob", toggle, "a white knob while on")
extra = {"toggleOffBorder": 0x2E000000, "onKnob": 0xFFFFFFFF, "onDanger": 0xFFFFFFFF, "backdrop": 0x73000000}
for name, value in extra.items():
    if not re.search(r'\b%s\s*=\s*0x%08X\s*;' % (name, value), java, re.I):
        problems.append("Theme.java: %s is not %s" % (name, hexa(value)))
if not re.search(r'popupShadow\s*=\s*dark\s*\?\s*0x80000000\s*:\s*0x33000000\s*;', java):
    problems.append("Theme.java: popupShadow is not black at 0.5 (dark) and 0.2 (light)")
if not re.search(r'bannerFill\s*=\s*dark\s*\?\s*0x1FFFBF33\s*:\s*0x2EFFBF33\s*;', java) or not re.search(r'bannerBorder\s*=\s*0x73FFBF33\s*;', java):
    problems.append("Theme.java: the credential banner is not rgba(255, 191, 51) at 0.12 / 0.18, with a border at 0.45")
if qml_path.endswith("Main.qml"):
    base = qml_path[:-len("Main.qml")]
    settings = open(base + "pages/SettingsPage.qml", encoding="utf-8").read()
    need(r'Qt\.rgba\(1,\s*0\.75,\s*0\.2,\s*theme\.dark\s*\?\s*0\.12\s*:\s*0\.18\)', "bannerFill", settings, "the banner fill rgba(1, 0.75, 0.2, 0.12 / 0.18)")
    need(r'border\.color:\s*Qt\.rgba\(1,\s*0\.75,\s*0\.2,\s*0\.45\)', "bannerBorder", settings, "the banner border at 0.45")
    need(r'color:\s*"#000000";\s*opacity:\s*0\.45', "backdrop", open(base + "AppPickerOverlay.qml", encoding="utf-8").read(), "a black backdrop at 0.45")
    need(r'shadowOpacity:\s*theme\.dark\s*\?\s*0\.5\s*:\s*0\.2', "popupShadow", qml, "a popup shadow at 0.5 / 0.2")

# Every colour Theme.java has is a token or one of these.
known = set(tokens) | set(derived)
for name in mine:
    if name not in known:
        problems.append("Theme.java has %s, which the QML does not have and this script does not derive" % name)

for p in problems:
    print(p)
sys.exit(1 if problems else 0)
PY

indent() { sed 's/^/    /'; }

fail=0
# check <name> <expect: pass|fail> <qml> <java>
check() {
  local name=$1 expect=$2 qml=$3 java=$4 got=pass out
  out="$(python3 "$TMP/compare.py" "$qml" "$java")" || got=fail
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
  fi
}

REAL_QML="$ROOT/qml/Main.qml"
REAL_JAVA="$ROOT/android/app/src/main/java/org/freetunnel/android/Theme.java"

# Fixtures: the real files, changed in one place. They sit in fake trees so that the
# script's reading of Toggle.qml and the pages beside Main.qml finds the real ones.
mkdir -p "$TMP/qml/pages"
cp "$ROOT/qml/Toggle.qml" "$ROOT/qml/AppPickerOverlay.qml" "$TMP/qml/"
cp "$ROOT/qml/pages/SettingsPage.qml" "$TMP/qml/pages/"
cp "$REAL_QML" "$TMP/qml/Main.qml"
cp "$REAL_JAVA" "$TMP/Good.java"
check "the fixtures' unchanged copies" pass "$TMP/qml/Main.qml" "$TMP/Good.java"

sed 's/bg = pick(dark, 0xFF181818,/bg = pick(dark, 0xFF191818,/' "$REAL_JAVA" > "$TMP/dark-differs.java"
check "a dark value that differs" fail "$TMP/qml/Main.qml" "$TMP/dark-differs.java"
sed 's/accentText = pick(dark, 0xFF181818, 0xFFFFFFFF)/accentText = pick(dark, 0xFF181818, 0xFFFFFFFE)/' "$REAL_JAVA" > "$TMP/light-differs.java"
check "a light value that differs" fail "$TMP/qml/Main.qml" "$TMP/light-differs.java"
sed '/^ *warn = pick/d' "$REAL_JAVA" > "$TMP/missing.java"
check "a token the QML has and Java lacks" fail "$TMP/qml/Main.qml" "$TMP/missing.java"
sed 's/infoBg = pick(dark, 0x29B0B0B0,/infoBg = pick(dark, 0x28B0B0B0,/' "$REAL_JAVA" > "$TMP/info-differs.java"
check "an infoBg whose alpha differs" fail "$TMP/qml/Main.qml" "$TMP/info-differs.java"
sed 's/primaryHover = pick(dark, 0xFF9D9D9D,/primaryHover = pick(dark, 0xFF8D9D9D,/' "$REAL_JAVA" > "$TMP/derived-differs.java"
check "a derived value that is not what Qt works out" fail "$TMP/qml/Main.qml" "$TMP/derived-differs.java"
sed 's/^\( *\)danger = pick(dark, \(.*\)$/\1danger = pick(dark, \2\n\1mystery = pick(dark, 0xFF000000, 0xFFFFFFFF);/' "$REAL_JAVA" > "$TMP/unknown.java"
check "a colour that is neither token nor derived" fail "$TMP/qml/Main.qml" "$TMP/unknown.java"
sed 's/^\( *\)readonly property color danger: .*$/\1readonly property color danger: dark ? "#e06a6a" : "#a32d2e"/' "$REAL_QML" > "$TMP/qml/Main.qml"
check "a token the QML changed" fail "$TMP/qml/Main.qml" "$TMP/Good.java"
sed 's/^\( *\)readonly property color danger: .*$/\1readonly property color danger: dark ? "#e06a6a" : "#a32d2d"\n\1readonly property color brandNew: dark ? "#111111" : "#eeeeee"/' "$REAL_QML" > "$TMP/qml/Main.qml"
check "a new token in the QML" fail "$TMP/qml/Main.qml" "$TMP/Good.java"
cp "$REAL_QML" "$TMP/qml/Main.qml"
check "the unchanged QML again" pass "$TMP/qml/Main.qml" "$TMP/Good.java"

check "Theme.java against qml/Main.qml" pass "$REAL_QML" "$REAL_JAVA"
exit "$fail"
