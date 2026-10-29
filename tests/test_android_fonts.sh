#!/usr/bin/env bash
# The Android app's typeface is the desktop's: Inter, the two files in assets/fonts/inter, Regular
# (400) for all text and Medium (500) where the desktop sets font.weight: Font.Medium. Nothing else is
# bundled, so any other weight would be made up from one of those two (a bold from Regular or Medium,
# an italic by skewing), which the desktop does not draw. The log, the certificate box and the TV's
# address are monospace, as the desktop's are.
#
# 1. Every setTypeface( under android/app/src/main takes Fonts.regular(...), Fonts.medium(...) or
#    Typeface.MONOSPACE. Fonts.java is the one place Inter is loaded, and so the one place the code
#    names the system's families (as a fallback if a file cannot be read): nothing else uses
#    Typeface.DEFAULT, SANS_SERIF, SERIF or create(), which would be Roboto, or a font made from one.
# 2. Nothing asks for a weight Inter is not bundled in: no Typeface.BOLD, ITALIC or DEFAULT_BOLD, no
#    StyleSpan, fake bold, skew, Html.fromHtml or font variation, in any file.
# 3. The resources: android:fontFamily is @font/inter_regular and nowhere else, and no android:textStyle,
#    typeface or font weight. The font files are not in the source tree (the build copies them into
#    res/font from assets/fonts/inter, so there is one copy of each in the repository).
# 4. The build, the desktop's resource list and the checksums name the same files, and the code reads
#    the two resources the build makes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# code <file>: the file with its comments taken out, and what is inside its string and character
# literals, so that a word in prose or in a string is no use of a class (as in test_android_tv_dialogs.sh).
code() {
  perl -0777 -pe 's{("(?:\\.|[^"\\\n])*")|(\x27(?:\\.|[^\x27\\\n])*\x27)|/\*.*?\*/|//[^\n]*}{defined $1 ? "\"\"" : defined $2 ? "\x27\x27" : ""}gse' "$1"
}
# xml <file>: the file with its comments taken out.
xml() {
  perl -0777 -pe 's/<!--.*?-->//gs' "$1"
}

# stray_typefaces <file>: each setTypeface( whose argument is none of the three, and fails if any.
stray_typefaces() {
  code "$1" | grep -n 'setTypeface(' | grep -v -E 'setTypeface\((Fonts\.(regular|medium)\([A-Za-z.]*\)|Typeface\.MONOSPACE)\)' && return 1
  return 0
}
# system_faces <file>: each use of the system's families, and fails if any.
system_faces() {
  code "$1" | grep -n -E '\bTypeface\.(DEFAULT|DEFAULT_BOLD|SANS_SERIF|SERIF|create[A-Za-z]*)\b' && return 1
  return 0
}
# heavy_faces <file>: each way to a weight or a slant that is not bundled, and fails if any.
heavy_faces() {
  code "$1" | grep -n -E '\bTypeface\.(BOLD|BOLD_ITALIC|ITALIC|DEFAULT_BOLD)\b|\bStyleSpan\b|\bsetFakeBoldText\b|\bsetTextSkewX\b|\bHtml\.fromHtml\b|\bsetFontVariationSettings\b|\bFontVariationAxis\b' && return 1
  return 0
}
# xml_faces <file>: each font attribute in a resource file but the theme's Inter Regular, and fails if any.
xml_faces() {
  xml "$1" | grep -n -E 'android:(fontFamily|textStyle|typeface|fontWeight|textFontWeight|fontVariationSettings|fontFeatureSettings)' \
    | grep -v -E 'name="android:fontFamily">@font/inter_regular<' && return 1
  return 0
}

indent() { sed 's/^/    /'; }

fail=0
# check <name> <function> <expect: pass|fail> <file>
check() {
  local name=$1 function=$2 expect=$3 file=$4 got=pass out
  out="$($function "$file")" || got=fail
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
  fi
}
# snippet <file> <line>...
snippet() { local file=$1; shift; printf '%s\n' "$@" > "$TMP/$file"; }

# The rules themselves, on snippets: what is allowed passes, each way round them fails.
snippet good \
  '// Typeface.DEFAULT, Typeface.BOLD and setTypeface(Typeface.DEFAULT) named in a comment are no use of them.' \
  '        v.setTypeface(Fonts.regular(context));' \
  '        v.setTypeface(Fonts.medium(this));' \
  '        input.setTypeface(Typeface.MONOSPACE);' \
  '        paint.setTypeface(Fonts.regular(this));' \
  '        String s = "Typeface.BOLD";'
check "Inter and monospace" stray_typefaces pass "$TMP/good"
check "no system family" system_faces pass "$TMP/good"
check "no weight that is not bundled" heavy_faces pass "$TMP/good"
snippet default '        b.setTypeface(Typeface.DEFAULT);'
check "setTypeface(Typeface.DEFAULT)" stray_typefaces fail "$TMP/default"
check "Typeface.DEFAULT" system_faces fail "$TMP/default"
snippet system '        v.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));'
check "a system family made with create()" stray_typefaces fail "$TMP/system"
check "Typeface.create" system_faces fail "$TMP/system"
snippet sans '        Typeface face = Typeface.SANS_SERIF;'
check "Typeface.SANS_SERIF" system_faces fail "$TMP/sans"
snippet asset '        Typeface face = Typeface.createFromAsset(getAssets(), "x.ttf");'
check "a font from the assets" system_faces fail "$TMP/asset"
snippet field '        v.setTypeface(MEDIUM);'
check "a typeface held in a constant of its own" stray_typefaces fail "$TMP/field"
snippet ui '        v.setTypeface(Ui.MEDIUM);'
check "Ui.MEDIUM, which is gone" stray_typefaces fail "$TMP/ui"
snippet derived '        v.setTypeface(Typeface.create(Fonts.regular(this), 600, false));'
check "a weight made from Inter Regular" stray_typefaces fail "$TMP/derived"
check "and made by create()" system_faces fail "$TMP/derived"
snippet bold '        v.setTypeface(Fonts.regular(this), Typeface.BOLD);'
check "Inter Regular asked for in bold" stray_typefaces fail "$TMP/bold"
check "Typeface.BOLD" heavy_faces fail "$TMP/bold"
snippet italic '        v.setTypeface(Fonts.regular(this), Typeface.ITALIC);'
check "Typeface.ITALIC" heavy_faces fail "$TMP/italic"
snippet span '        text.setSpan(new StyleSpan(Typeface.BOLD), 0, 3, 0);'
check "a StyleSpan" heavy_faces fail "$TMP/span"
snippet fake '        v.getPaint().setFakeBoldText(true);'
check "fake bold" heavy_faces fail "$TMP/fake"
snippet skew '        v.getPaint().setTextSkewX(-0.25f);'
check "a skew" heavy_faces fail "$TMP/skew"
snippet html '        v.setText(Html.fromHtml("<b>x</b>", 0));'
check "Html.fromHtml" heavy_faces fail "$TMP/html"
snippet variation '        v.setFontVariationSettings("wght 600");'
check "a font variation" heavy_faces fail "$TMP/variation"

cat > "$TMP/theme-good.xml" <<'EOF'
<resources>
    <style name="AppTheme" parent="android:style/Theme.Material.NoActionBar">
        <!-- android:fontFamily sans, android:textStyle bold, named in a comment. -->
        <item name="android:fontFamily">@font/inter_regular</item>
        <item name="android:windowLightStatusBar">false</item>
    </style>
</resources>
EOF
check "the theme names Inter Regular" xml_faces pass "$TMP/theme-good.xml"
sed 's|@font/inter_regular|sans|' "$TMP/theme-good.xml" > "$TMP/theme-sans.xml"
check "the theme names the system's sans" xml_faces fail "$TMP/theme-sans.xml"
sed 's|@font/inter_regular|@font/inter_medium|' "$TMP/theme-good.xml" > "$TMP/theme-medium.xml"
check "the theme names Medium for all text" xml_faces fail "$TMP/theme-medium.xml"
sed 's|<item name="android:windowLightStatusBar">false</item>|<item name="android:textStyle">bold</item>|' "$TMP/theme-good.xml" > "$TMP/theme-bold.xml"
check "the theme makes all text bold" xml_faces fail "$TMP/theme-bold.xml"
sed 's|<item name="android:windowLightStatusBar">false</item>|<item name="android:textFontWeight">600</item>|' "$TMP/theme-good.xml" > "$TMP/theme-weight.xml"
check "the theme sets a font weight" xml_faces fail "$TMP/theme-weight.xml"

# fonts_agree <gradle> <qrc> <sums> <fonts java> <theme>: the build, the desktop's resource list and the
# checksums name the same files, and the code and the theme read the two resources the build makes.
fonts_agree() {
  local gradle=$1 qrc=$2 sums=$3 java=$4 theme=$5 built listed pinned problem=0
  built="$(grep -oE "assets/fonts/inter/[A-Za-z0-9._-]+\.ttf" "$gradle" | sed 's|.*/||' | sort -u)"
  listed="$(grep -oE 'alias="[^"]+\.ttf"' "$qrc" | sed 's/^alias="//; s/"$//' | sort -u)"
  pinned="$(awk '{ print $2 }' "$sums" | sort -u)"
  if [ -z "$built" ] || [ "$built" != "$listed" ] || [ "$built" != "$pinned" ]; then
    echo "the build copies:        $(tr '\n' ' ' <<<"$built")"
    echo "fonts.qrc lists:         $(tr '\n' ' ' <<<"$listed")"
    echo "SHA256SUMS pins:         $(tr '\n' ' ' <<<"$pinned")"
    problem=1
  fi
  for resource in inter_regular inter_medium; do
    if ! grep -q "'$resource.ttf'" "$gradle"; then
      echo "the build does not make res/font/$resource.ttf"
      problem=1
    fi
    if ! code "$java" | grep -q "R\\.font\\.$resource\\b"; then
      echo "the code does not read R.font.$resource"
      problem=1
    fi
  done
  if ! xml "$theme" | grep -q '@font/inter_regular<'; then
    echo "the theme does not name @font/inter_regular"
    problem=1
  fi
  # Exactly the two resources are read: a third weight is not bundled.
  local reads
  reads="$(code "$java" | grep -oE 'R\.font\.[a-z_]+' | sort -u | tr '\n' ' ')"
  if [ "$reads" != "R.font.inter_medium R.font.inter_regular " ]; then
    echo "the code reads: $reads"
    problem=1
  fi
  return "$problem"
}

mkdir -p "$TMP/agree"
cat > "$TMP/agree/build.gradle" <<'EOF'
    regular.set(rootProject.file('../assets/fonts/inter/Inter-Regular.ttf'))
    medium.set(rootProject.file('../assets/fonts/inter/Inter-Medium.ttf'))
        install(regular.get().asFile, 'inter_regular.ttf', sums, fonts)
        install(medium.get().asFile, 'inter_medium.ttf', sums, fonts)
EOF
cat > "$TMP/agree/fonts.qrc" <<'EOF'
        <file alias="Inter-Regular.ttf">inter/Inter-Regular.ttf</file>
        <file alias="Inter-Medium.ttf">inter/Inter-Medium.ttf</file>
EOF
cat > "$TMP/agree/SHA256SUMS" <<'EOF'
40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82  Inter-Regular.ttf
97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872  Inter-Medium.ttf
EOF
cat > "$TMP/agree/Fonts.java" <<'EOF'
        if (regular == null) regular = load(context, R.font.inter_regular, "inter_regular", "sans-serif");
        if (medium == null) medium = load(context, R.font.inter_medium, "inter_medium", "sans-serif-medium");
EOF
cp "$TMP/theme-good.xml" "$TMP/agree/styles.xml"
agree() { fonts_agree "$TMP/agree/build.gradle" "$TMP/agree/fonts.qrc" "$TMP/agree/SHA256SUMS" "$TMP/agree/Fonts.java" "$TMP/agree/styles.xml"; }
# agree_with <expect> <name> <file in agree/> <sed expression>: the fixtures with that one file changed.
agree_with() {
  local expect=$1 name=$2 file=$3 edit=$4 got=pass out
  cp -r "$TMP/agree" "$TMP/agree.saved"
  sed -i -E "$edit" "$TMP/agree/$file"
  out="$(agree 2>&1)" || got=fail
  rm -rf "$TMP/agree" && mv "$TMP/agree.saved" "$TMP/agree"
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
  fi
}
if out="$(agree 2>&1)"; then echo "ok - the build, fonts.qrc, SHA256SUMS, the code and the theme agree"; else
  echo "not ok - the fixtures that should agree do not"; indent <<<"$out"; fail=1; fi
agree_with fail "a font the build copies and fonts.qrc does not list" build.gradle 's/Inter-Medium\.ttf/Inter-Black.ttf/'
agree_with fail "a font fonts.qrc lists and SHA256SUMS does not pin" SHA256SUMS 's/Inter-Medium\.ttf/Inter-Bold.ttf/'
agree_with fail "a font in fonts.qrc that the build does not copy" fonts.qrc 's/Inter-Medium\.ttf/Inter-Black.ttf/g'
agree_with fail "the build leaving Medium out" build.gradle "s/'inter_medium.ttf'/'inter_med.ttf'/"
agree_with fail "the code reading a resource the build does not make" Fonts.java 's/R\.font\.inter_medium/R.font.inter_semibold/'
agree_with fail "the code reading a third weight" Fonts.java 's/$/ R.font.inter_bold;/'
agree_with fail "the theme naming nothing of Inter" styles.xml 's|@font/inter_regular|sans|'

# The app.
MAIN="$ROOT/android/app/src/main"
files=0
for file in "$MAIN"/java/org/freetunnel/android/*.java "$MAIN"/java/org/freetunnel/android/*.kt; do
  [ -f "$file" ] || continue
  files=$((files + 1))
  base="$(basename "$file")"
  for rule in stray_typefaces heavy_faces; do
    if ! out="$($rule "$file")"; then
      echo "not ok - $base: $rule"
      indent <<<"$out"
      fail=1
    fi
  done
  # Fonts.java is where Inter is loaded and the system's family stands in if a file cannot be read.
  if [ "$base" != "Fonts.java" ] && ! out="$(system_faces "$file")"; then
    echo "not ok - $base uses the system's typeface"
    indent <<<"$out"
    fail=1
  fi
done
[ -f "$MAIN/java/org/freetunnel/android/Fonts.java" ] || { echo "not ok - Fonts.java is gone"; fail=1; }
xmls=0
while IFS= read -r file; do
  xmls=$((xmls + 1))
  if ! out="$(xml_faces "$file")"; then
    echo "not ok - ${file#"$ROOT"/} has a font attribute"
    indent <<<"$out"
    fail=1
  fi
done < <(find "$MAIN/res" -name '*.xml' | sort)
copies="$(find "$ROOT/android" \( -name '*.ttf' -o -name '*.otf' -o -name '*.ttc' -o -name '*.woff' -o -name '*.woff2' \) -not -path '*/build/*' -print)"
if [ -n "$copies" ] || [ -e "$MAIN/res/font" ]; then
  echo "not ok - a font file is in android/ (the build copies them from assets/fonts/inter):"
  indent <<<"$copies${copies:+$'\n'}$([ -e "$MAIN/res/font" ] && echo "$MAIN/res/font")"
  fail=1
fi
if ! out="$(fonts_agree "$ROOT/android/app/build.gradle" "$ROOT/assets/fonts/fonts.qrc" "$ROOT/assets/fonts/inter/SHA256SUMS" \
    "$MAIN/java/org/freetunnel/android/Fonts.java" "$MAIN/res/values/styles.xml" 2>&1)"; then
  echo "not ok - the build, fonts.qrc, SHA256SUMS, the code and the theme disagree"
  indent <<<"$out"
  fail=1
fi
[ "$fail" -ne 0 ] || echo "ok - the $files source files and $xmls resource files set text in Inter and monospace only, with no weight that is not bundled"
exit "$fail"
