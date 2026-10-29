#!/usr/bin/env python3
"""The Android app's icons, made from the desktop's.

The desktop draws assets/icons/*.svg and assets/logo.svg. The Android app draws the
same shapes from VectorDrawables, so that an icon is the same icon in both. This writes
android/app/src/main/res/drawable/ic_*.xml from those SVG files:

  - <path> data goes over as it is (only its spacing is made explicit: Android's
    parser is stricter than a browser about numbers run together);
  - <rect rx>, <line>, <circle> and <polyline> become path data, one to one;
  - stroke 2 on the 24 grid with round caps and joins, as the SVG says;
  - a filled <circle> stays filled (the speedometer's hub), and the logo's four
    arches keep their own greys: the app never tints the logo.

Tinted icons are white in the file and take their colour from the view that shows
them. The plus signs are not icons on the desktop either: it draws two rectangles,
16 by 1.8 in a 22 box on Configs and 10 by 1.6 in a 14 box on Home, and so do these.
Nor are the arrows and ticks (▾ ✓ ↓ ↑ ←): the desktop draws them as characters of its font.
Inter, which it is set in, has ✓ ↓ ↑ ← of its own, and these are drawn here as the outlines of
those glyphs, read from assets/fonts/inter/Inter-Regular.ttf, the file the desktop draws them
from, at the pixel size the QML sets them in (see INTER_GLYPHS): the same shape and size, which
sit on the line as the desktop's do, whatever font a TextView would fall back to. Inter has no ▾
(U+25BE): the desktop's system font draws it, and it stays a shape of the size that font gave it
(7.5 by 7, measured from the desktop's renders).

  scripts/android-icons-from-svg.py           writes the drawables
  scripts/android-icons-from-svg.py --check   fails if one is out of date

tests/test_android_icons.sh runs --check, with fixtures.
"""
import argparse
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# SVG file -> drawable. ic_servers was the Configs icon's old name.
ICONS = {
    "connection": "ic_connection",
    "network": "ic_network",
    "settings": "ic_settings",
    "log": "ic_log",
    "speedometer": "ic_ping",
    "export": "ic_export",
    "edit": "ic_edit",
    "close": "ic_close",
}
LOGO = "ic_logo"
# SVG files in assets/icons that the Android app has no use for.
DESKTOP_ONLY = set()
# The drawn plus signs: drawable, box, arm length, arm thickness (QML: ConfigsPage.qml
# and HomePage.qml draw them as two Rectangles with radius 1).
PLUSES = {
    "ic_add": (22, 16, 1.8),
    "ic_add_home": (14, 10, 1.6),
}

# The characters the desktop draws as text that Inter has no glyph for, and so the system's font draws there:
# drawable -> (character, width, height, path data, stroke width, or None for a filled shape).
GLYPHS = {
    "ic_dropdown": ("▾", 8, 7, "M0.25,0 H7.75 L4,7 Z", None),
}
# The characters the desktop draws as text that Inter has: drawable -> (character, pixel size, where the QML sets it). The
# pixel size is the QML's, and a QML pixel is a dp here; the glyph is the one in Inter Regular, which none of these Texts leave.
# The ✓ is the one drawable of two Texts: SettingsPage.qml's update tick (14) and Main.qml's select popup (no size of its own,
# so the system's, which no one number stands for); it is drawn at 14, as it has been.
INTER_GLYPHS = {
    "ic_check": ("✓", 14, "SettingsPage.qml: the update tick; Main.qml: the select popup's"),
    "ic_arrow_down": ("↓", 14, "HomePage.qml: the speed tile"),
    "ic_arrow_up": ("↑", 14, "HomePage.qml: the speed tile"),
    "ic_back": ("←", 20, "AppPickerOverlay.qml and CreateConfigOverlay.qml: the card's back arrow"),
}
INTER_FILE = Path("fonts") / "inter" / "Inter-Regular.ttf"

NS = "{http://www.w3.org/2000/svg}"
HEADER = ("<!-- Made by scripts/android-icons-from-svg.py from {source}. "
          "Change that, not this file. -->\n")
VECTOR = ('<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="{w}dp" '
          'android:height="{h}dp" android:viewportWidth="{vw}" android:viewportHeight="{vh}">\n{paths}</vector>\n')


def num(value):
    """A number as the shortest text that reads back to the same value."""
    value = float(value)
    if value == int(value):
        return str(int(value))
    return ("%.4f" % value).rstrip("0").rstrip(".")


def fnum(text):
    return float(text)


def tokens(d):
    return re.findall(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", d)


def path_data(d, where):
    """Path data with every number separate. The commands, and so the shape, stay."""
    out = []
    parts = tokens(d)
    # Anything the tokenizer could not read would change the shape silently.
    if "".join(re.sub(r"[\s,]", "", x) for x in parts) != re.sub(r"[\s,]", "", d):
        raise SystemExit("%s: path data this script cannot read: %s" % (where, d))
    i = 0
    command = None
    while i < len(parts):
        if re.match(r"[A-Za-z]", parts[i]):
            command = parts[i]
            out.append(command)
            i += 1
            continue
        if command in ("A", "a"):
            # rx ry rotation large-arc sweep x y: the two flags are 0 or 1 alone.
            args = parts[i:i + 7]
            if len(args) < 7 or args[3] not in ("0", "1") or args[4] not in ("0", "1"):
                raise SystemExit("%s: an arc this script cannot read: %s" % (where, d))
            out.append(" ".join(a if k in (3, 4) else num(a) for k, a in enumerate(args)))
            i += 7
        else:
            # Written out whole (".33" as "0.33"), for any parser, as the same number.
            if float(num(parts[i])) != float(parts[i]):
                raise SystemExit("%s: a number with more digits than this script keeps: %s" % (where, parts[i]))
            out.append(num(parts[i]))
            i += 1
    # "M 12 3" reads as well as "M12 3": no space after a command.
    return re.sub(r"([A-Za-z]) ", r"\1", " ".join(out))


def rect_path(e):
    x, y, w, h = (fnum(e.get(k, "0")) for k in ("x", "y", "width", "height"))
    rx = fnum(e.get("rx", e.get("ry", "0")))
    ry = fnum(e.get("ry", e.get("rx", "0")))
    rx, ry = min(rx, w / 2), min(ry, h / 2)
    if rx == 0 and ry == 0:
        return "M%s,%s H%s V%s H%s Z" % (num(x), num(y), num(x + w), num(y + h), num(x))
    return ("M%s,%s H%s A%s,%s 0 0 1 %s,%s V%s A%s,%s 0 0 1 %s,%s H%s A%s,%s 0 0 1 %s,%s V%s A%s,%s 0 0 1 %s,%s Z" % (
        num(x + rx), num(y), num(x + w - rx),
        num(rx), num(ry), num(x + w), num(y + ry),
        num(y + h - ry),
        num(rx), num(ry), num(x + w - rx), num(y + h),
        num(x + rx),
        num(rx), num(ry), num(x), num(y + h - ry),
        num(y + ry),
        num(rx), num(ry), num(x + rx), num(y)))


def line_path(e):
    return "M%s,%s L%s,%s" % tuple(num(fnum(e.get(k))) for k in ("x1", "y1", "x2", "y2"))


def circle_path(e):
    cx, cy, r = (fnum(e.get(k)) for k in ("cx", "cy", "r"))
    return "M%s,%s A%s,%s 0 1 1 %s,%s A%s,%s 0 1 1 %s,%s Z" % (
        num(cx), num(cy - r), num(r), num(r), num(cx), num(cy + r), num(r), num(r), num(cx), num(cy - r))


def polyline_path(e):
    points = re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)", e.get("points"))
    pairs = [(points[i], points[i + 1]) for i in range(0, len(points) - 1, 2)]
    return "M%s,%s " % pairs[0] + " ".join("L%s,%s" % p for p in pairs[1:])


def stroke_attrs(svg, element):
    """What the SVG's stroke says, as the vector's: all of it must be what Android is told."""
    width = element.get("stroke-width", svg.get("stroke-width", "1"))
    cap = svg.get("stroke-linecap", "butt")
    join = svg.get("stroke-linejoin", "miter")
    return ('android:strokeColor="#ffffff" android:strokeWidth="%s" android:strokeLineCap="%s" android:strokeLineJoin="%s"'
            % (num(width), cap, join))


def icon(svg_path):
    svg = ET.parse(svg_path).getroot()
    box = svg.get("viewBox").split()
    if box[0] != "0" or box[1] != "0":
        raise SystemExit("%s: a viewBox that does not start at 0 0" % svg_path)
    if svg.get("fill") != "none":
        raise SystemExit("%s: this script draws outlined icons, with fill=\"none\" on the svg" % svg_path)
    # One <path> in the drawable for each element in the SVG, in the same order. Not
    # merged into one: a relative moveto that starts an SVG path is absolute, and
    # after another path's closing it would not be.
    shapes = []
    for e in svg.iter():
        tag = e.tag.replace(NS, "")
        if tag == "svg":
            continue
        if tag == "path":
            d = path_data(e.get("d"), svg_path)
        elif tag == "rect":
            d = rect_path(e)
        elif tag == "line":
            d = line_path(e)
        elif tag == "circle":
            d = circle_path(e)
        elif tag == "polyline":
            d = polyline_path(e)
        else:
            raise SystemExit("%s: an element this script does not convert: <%s>" % (svg_path, tag))
        if e.get("fill") == "currentColor" and e.get("stroke") == "none":
            shapes.append('    <path android:pathData="%s" android:fillColor="#ffffff"/>\n' % d)
        elif e.get("fill", "none") == "none" and e.get("stroke", "currentColor") == "currentColor":
            shapes.append('    <path android:pathData="%s" android:fillColor="@android:color/transparent" %s/>\n'
                          % (d, stroke_attrs(svg, e)))
        else:
            raise SystemExit("%s: a <%s> with a fill or stroke this script does not know" % (svg_path, tag))
    paths = "".join(shapes)
    w, h = box[2], box[3]
    return HEADER.format(source="assets/icons/" + svg_path.name) + \
        VECTOR.format(w=num(w), h=num(h), vw=num(w), vh=num(h), paths=paths)


def logo(svg_path):
    """The mark as it is: four filled arches in their own greys, never tinted."""
    svg = ET.parse(svg_path).getroot()
    box = svg.get("viewBox").split()
    paths = ""
    for e in svg.iter():
        if e.tag.replace(NS, "") != "path":
            continue
        fill = e.get("fill")
        if not fill or not re.match(r"#[0-9a-fA-F]{6}$", fill):
            raise SystemExit("%s: an arch without a plain colour" % svg_path)
        paths += '    <path android:pathData="%s" android:fillColor="%s"/>\n' % (path_data(e.get("d"), svg_path), fill.lower())
    if not paths:
        raise SystemExit("%s: no arches" % svg_path)
    return HEADER.format(source="assets/logo.svg") + VECTOR.format(
        w=num(box[2]), h=num(box[3]), vw=num(box[2]), vh=num(box[3]), paths=paths)


def capsule(x, y, w, h):
    """A rectangle with the ends of its short sides round, as QML's radius 1 gives one this thin."""
    r = min(1.0, w / 2, h / 2)
    return "M%s,%s H%s A%s,%s 0 0 1 %s,%s V%s A%s,%s 0 0 1 %s,%s H%s A%s,%s 0 0 1 %s,%s V%s A%s,%s 0 0 1 %s,%s Z" % (
        num(x + r), num(y), num(x + w - r), num(r), num(r), num(x + w), num(y + r), num(y + h - r),
        num(r), num(r), num(x + w - r), num(y + h), num(x + r), num(r), num(r), num(x), num(y + h - r),
        num(y + r), num(r), num(r), num(x + r), num(y))


def plus(box, arm, thickness):
    low = (box - thickness) / 2
    start = (box - arm) / 2
    paths = "    <path android:pathData=\"%s %s\" android:fillColor=\"#ffffff\"/>\n" % (
        capsule(start, low, arm, thickness), capsule(low, start, thickness, arm))
    return ("<!-- Made by scripts/android-icons-from-svg.py: the desktop draws this plus as two rectangles, "
            "%s by %s in a %s box. -->\n" % (num(arm), num(thickness), num(box))) + VECTOR.format(
        w=num(box), h=num(box), vw=num(box), vh=num(box), paths=paths)


def glyph(character, w, h, d, stroke):
    """A character the desktop draws as text, as a shape: filled, or a line of this width with round ends."""
    if stroke is None:
        shape = '    <path android:pathData="%s" android:fillColor="#ffffff"/>\n' % d
    else:
        shape = ('    <path android:pathData="%s" android:fillColor="@android:color/transparent" android:strokeColor="#ffffff" '
                 'android:strokeWidth="%s" android:strokeLineCap="round" android:strokeLineJoin="round"/>\n' % (d, num(stroke)))
    return ("<!-- Made by scripts/android-icons-from-svg.py: the desktop draws %s as a character of the system's font, Inter having none, "
            "and this is it at the size that font gives it. -->\n" % character) + VECTOR.format(w=num(w), h=num(h), vw=num(w), vh=num(h), paths=shape)


class Font:
    """Outlines out of a TrueType file (glyf), with nothing but the standard library: the cmap (format 4 or 12), loca, glyf and head
    tables, simple glyphs and composites that are only moved. It is for the few glyphs of Inter here and refuses what it does not read."""

    def __init__(self, path):
        try:
            self.data = Path(path).read_bytes()
        except OSError as error:
            raise SystemExit("%s: cannot be read (%s); the glyphs of Inter are drawn from it" % (path, error))
        self.path = path
        if self.data[:4] not in (b"\x00\x01\x00\x00", b"true"):
            raise SystemExit("%s: not a TrueType file" % path)
        count = struct.unpack(">H", self.data[4:6])[0]
        self.tables = {}
        for i in range(count):
            tag, _, offset, length = struct.unpack(">4sIII", self.data[12 + 16 * i:28 + 16 * i])
            self.tables[tag.decode("latin-1")] = offset
        for needed in ("head", "cmap", "loca", "glyf"):
            if needed not in self.tables:
                raise SystemExit("%s: no %s table: not the TrueType file it should be" % (path, needed))
        head = self.tables["head"]
        self.units = struct.unpack(">H", self.data[head + 18:head + 20])[0]
        self.long_loca = struct.unpack(">h", self.data[head + 50:head + 52])[0] == 1

    def glyph_of(self, character):
        data, cmap = self.data, self.tables["cmap"]
        code = ord(character)
        wanted_subtable = None
        for i in range(struct.unpack(">H", data[cmap + 2:cmap + 4])[0]):
            platform, encoding, offset = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
            if (platform, encoding) in ((3, 10), (0, 4), (0, 6)):
                wanted_subtable = cmap + offset   # the full range wins
                break
            if (platform, encoding) in ((3, 1), (0, 3)) and wanted_subtable is None:
                wanted_subtable = cmap + offset
        if wanted_subtable is None:
            raise SystemExit("%s: no Unicode cmap" % self.path)
        table = wanted_subtable
        form = struct.unpack(">H", data[table:table + 2])[0]
        if form == 12:
            for i in range(struct.unpack(">I", data[table + 12:table + 16])[0]):
                first, last, glyph = struct.unpack(">III", data[table + 16 + 12 * i:table + 28 + 12 * i])
                if first <= code <= last:
                    return glyph + code - first
            return 0
        if form == 4:
            segments = struct.unpack(">H", data[table + 6:table + 8])[0] // 2
            ends = struct.unpack(">%dH" % segments, data[table + 14:table + 14 + 2 * segments])
            starts = struct.unpack(">%dH" % segments, data[table + 16 + 2 * segments:table + 16 + 4 * segments])
            deltas = struct.unpack(">%dh" % segments, data[table + 16 + 4 * segments:table + 16 + 6 * segments])
            offsets_at = table + 16 + 6 * segments
            offsets = struct.unpack(">%dH" % segments, data[offsets_at:offsets_at + 2 * segments])
            for i in range(segments):
                if starts[i] <= code <= ends[i]:
                    if offsets[i] == 0:
                        return (code + deltas[i]) & 0xFFFF
                    at = offsets_at + 2 * i + offsets[i] + 2 * (code - starts[i])
                    glyph = struct.unpack(">H", data[at:at + 2])[0]
                    return (glyph + deltas[i]) & 0xFFFF if glyph else 0
            return 0
        raise SystemExit("%s: a cmap of format %d, which this script does not read" % (self.path, form))

    def contours(self, glyph):
        """The glyph's outline: a list of contours, each a list of (x, y, on the curve), in font units, y up."""
        data, loca = self.data, self.tables["loca"]
        if self.long_loca:
            start, end = struct.unpack(">II", data[loca + 4 * glyph:loca + 4 * glyph + 8])
        else:
            start, end = (v * 2 for v in struct.unpack(">HH", data[loca + 2 * glyph:loca + 2 * glyph + 4]))
        if start == end:
            return []
        at = self.tables["glyf"] + start
        count = struct.unpack(">h", data[at:at + 2])[0]
        at += 10
        if count < 0:
            out = []
            while True:
                flags, part = struct.unpack(">HH", data[at:at + 4])
                at += 4
                if flags & 1:
                    dx, dy = struct.unpack(">hh", data[at:at + 4])
                    at += 4
                else:
                    dx, dy = struct.unpack(">bb", data[at:at + 2])
                    at += 2
                if not flags & 2 or flags & (0x08 | 0x40 | 0x80):
                    raise SystemExit("%s: glyph %d is a composite this script does not read" % (self.path, glyph))
                out += [[(x + dx, y + dy, on) for x, y, on in contour] for contour in self.contours(part)]
                if not flags & 0x20:
                    return out
        ends = struct.unpack(">%dH" % count, data[at:at + 2 * count])
        at += 2 * count
        at += 2 + struct.unpack(">H", data[at:at + 2])[0]   # the instructions
        points = ends[-1] + 1
        flags = []
        while len(flags) < points:
            flag = data[at]
            at += 1
            flags.append(flag)
            if flag & 8:
                flags += [flag] * data[at]
                at += 1
        coordinates = []
        for short, same_or_positive in ((2, 16), (4, 32)):
            value, axis = 0, []
            for flag in flags:
                if flag & short:
                    step = data[at]
                    at += 1
                    value += step if flag & same_or_positive else -step
                elif not flag & same_or_positive:
                    value += struct.unpack(">h", data[at:at + 2])[0]
                    at += 2
                axis.append(value)
            coordinates.append(axis)
        out, first = [], 0
        for last in ends:
            out.append([(coordinates[0][i], coordinates[1][i], bool(flags[i] & 1)) for i in range(first, last + 1)])
            first = last + 1
        return out


def contour_path(points, place):
    """One TrueType contour as path data: lines and quadratic curves, an implied point between two off-curve ones."""
    for i, point in enumerate(points):
        if point[2]:
            points = points[i:] + points[:i]
            break
    else:
        points = [((points[0][0] + points[-1][0]) / 2, (points[0][1] + points[-1][1]) / 2, True)] + points
    first = points[0]
    steps = ["M%s,%s" % place(first)]
    control = None
    for x, y, on in points[1:] + [first]:
        if on:
            steps.append("L%s,%s" % place((x, y)) if control is None else "Q%s,%s %s,%s" % (place(control) + place((x, y))))
            control = None
        else:
            if control is not None:
                middle = ((control[0] + x) / 2, (control[1] + y) / 2)
                steps.append("Q%s,%s %s,%s" % (place(control) + place(middle)))
            control = (x, y)
    steps.append("Z")
    return " ".join(steps)


def inter_glyph(font, character, size, where):
    """A character Inter has, as a filled shape: its outline in the font's file, at this pixel size, in a box that is its ink."""
    glyph = font.glyph_of(character)
    contours = font.contours(glyph) if glyph else []
    if not contours:
        raise SystemExit("%s has no glyph for %s (U+%04X), which %s draws" % (font.path, character, ord(character), where))
    xs = [x for contour in contours for x, _, _ in contour]
    ys = [y for contour in contours for _, y, _ in contour]
    scale = size / font.units
    left, top = min(xs), max(ys)

    def place(point):
        return (num((point[0] - left) * scale), num((top - point[1]) * scale))

    w, h = num((max(xs) - left) * scale), num((top - min(ys)) * scale)
    shape = '    <path android:pathData="%s" android:fillColor="#ffffff"/>\n' % " ".join(contour_path(c, place) for c in contours)
    return ("<!-- Made by scripts/android-icons-from-svg.py: the desktop draws %s as a character of Inter (%s), and this is its outline in "
            "%s at %s px. -->\n" % (character, where, INTER_FILE.name, size)) + VECTOR.format(w=w, h=h, vw=w, vh=h, paths=shape)


def wanted(assets, res):
    """Every drawable this script owns: path -> text."""
    out = {}
    icons = Path(assets) / "icons"
    known = set(ICONS) | DESKTOP_ONLY
    for svg in sorted(icons.glob("*.svg")):
        if svg.stem not in known:
            raise SystemExit("%s has no drawable: add it to ICONS (or DESKTOP_ONLY) in this script" % svg)
    for name, drawable in ICONS.items():
        out[Path(res) / (drawable + ".xml")] = icon(icons / (name + ".svg"))
    out[Path(res) / (LOGO + ".xml")] = logo(Path(assets) / "logo.svg")
    for drawable, (box, arm, thickness) in PLUSES.items():
        out[Path(res) / (drawable + ".xml")] = plus(box, arm, thickness)
    for drawable, (character, w, h, d, stroke) in GLYPHS.items():
        out[Path(res) / (drawable + ".xml")] = glyph(character, w, h, d, stroke)
    try:
        font = Font(Path(assets) / INTER_FILE)
        for drawable, (character, size, where) in INTER_GLYPHS.items():
            out[Path(res) / (drawable + ".xml")] = inter_glyph(font, character, size, where)
    except (struct.error, IndexError):
        raise SystemExit("%s: damaged, or not the font it should be: the outlines of the arrows and ticks cannot be read" % (Path(assets) / INTER_FILE))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if a drawable differs from what the SVG files give")
    parser.add_argument("--assets", default=str(ROOT / "assets"), help="the directory with icons/, logo.svg and fonts/inter/Inter-Regular.ttf")
    parser.add_argument("--res", default=str(ROOT / "android/app/src/main/res/drawable"), help="the drawable directory")
    args = parser.parse_args()
    made = wanted(args.assets, args.res)
    if not args.check:
        for path, text in made.items():
            path.write_text(text, encoding="utf-8")
        print("wrote %d drawables in %s" % (len(made), args.res))
        return 0
    problems = []
    for path, text in sorted(made.items()):
        if not path.is_file():
            problems.append("%s is missing" % path.name)
        elif path.read_text(encoding="utf-8") != text:
            problems.append("%s differs from what scripts/android-icons-from-svg.py makes of its SVG" % path.name)
    for stale in sorted(Path(args.res).glob("ic_*.xml")):
        if stale not in made:
            problems.append("%s is not made from an SVG: delete it, or add it to the script" % stale.name)
    for problem in problems:
        print("android-icons: " + problem, file=sys.stderr)
    if problems:
        print("android-icons: run scripts/android-icons-from-svg.py and commit what it writes", file=sys.stderr)
        return 1
    print("android icons: %d drawables match assets/" % len(made))
    return 0


if __name__ == "__main__":
    sys.exit(main())
