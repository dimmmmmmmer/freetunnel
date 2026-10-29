#!/usr/bin/env bash
# The Android device tests name things in the app: a control by its key, a private field or
# method by reflection, a string by its resource, a member of one of the app's classes. They run
# only on an emulator, late in Android CI, so a name changed on one side is found there, half an
# hour in, unless scripts/check-android-device-tests.py reads both sides first. This runs it
#
# 1. on the repository as it is, which must have nothing to report, and
# 2. on a copy of the app with one name changed, a key, a field, a method, a string, a class's
#    constant, each of which it must report, and on a small tree it must pass and fail by kind.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/check-android-device-tests.py"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail=0
indent() { sed 's/^/    /'; }
# expect <name> <pass|fail> <android dir> [text the report must contain]
expect() {
  local name=$1 want=$2 dir=$3 contains=${4:-} got=pass out
  out="$(python3 -I "$CHECK" "$dir" 2>&1)" || got=fail
  if [ "$got" != "$want" ]; then
    echo "not ok - $name: expected $want, got $got"
    indent <<<"$out"
    fail=1
  elif [ -n "$contains" ] && ! grep -qF -- "$contains" <<<"$out"; then
    echo "not ok - $name: the report does not say \"$contains\""
    indent <<<"$out"
    fail=1
  else
    echo "ok - $name"
  fi
}

# 1. The repository itself.
expect "the device tests name only what the app has" pass "$ROOT/android"

# 2a. A copy of the real app with one name changed. A rename is made everywhere it is written on the app's side, as a
# real one would be, and the tests are left as they are.
mutate() {
  local name=$1 contains=$2 file=$3 sedexpr=$4 copy="$TMP/real-$1"
  mkdir -p "$copy/app/src"
  cp -r "$ROOT/android/app/src/main" "$ROOT/android/app/src/androidTest" "$copy/app/src/"
  # file: one file, or a directory (every Java file under it).
  if [ -d "$copy/app/src/main/$file" ]; then find "$copy/app/src/main/$file" -name '*.java' -exec sed -i -E "$sedexpr" {} +
  else sed -i -E "$sedexpr" "$copy/app/src/main/$file"; fi
  expect "$name" fail "$copy" "$contains"
}
mutate "a control's key renamed in the app" 'home:config' java/org/freetunnel/android/MainActivity.java 's/"home:config"/"home:server"/g'
mutate "a family of keys renamed in the app" 'config:0:select' java 's/"config:"/"row:"/g'
mutate "a private method renamed in the app" '"showConfirm"' java/org/freetunnel/android/MainActivity.java 's/\bshowConfirm\b/askConfirm/g'
mutate "a private field renamed in the app" '"pageScroll"' java/org/freetunnel/android/MainActivity.java 's/\bpageScroll\b/pageScroller/g'
mutate "a method that takes another parameter" 'showImportConfirmation takes' java/org/freetunnel/android/MainActivity.java 's/(void showImportConfirmation\(String name, boolean namedAfterFile, RoutingConfig.Import checked, String origin), boolean link\) \{/\1) {/'
mutate "a string resource renamed" 'config_added' res/values/strings.xml 's/name="config_added"/name="config_was_added"/'
mutate "a class's constant renamed" 'NAME' java/org/freetunnel/android/LogFile.java 's/\bNAME\b/FILE_NAME/g'
mutate "a class's method renamed" 'append' java/org/freetunnel/android/LogFile.java 's/\bappend\b/add/g'

# 2b. A small tree, by kind.
tree() {
  local dir=$1
  mkdir -p "$dir/app/src/main/java/org/x" "$dir/app/src/main/res/values" "$dir/app/src/androidTest/java/org/x"
  cat > "$dir/app/src/main/java/org/x/MainActivity.java" <<'EOF'
public final class MainActivity {
    private int page;
    private void render() { }
    void build() { ui.focus(view, "tab:" + i, Look.TILE); ui.focus(view, "home:toggle", Look.HERO); ui.focus(v, "config:" + i + ":select", Look.ROW); }
}
EOF
  cat > "$dir/app/src/main/java/org/x/LogFile.java" <<'EOF'
final class LogFile {
    static final String NAME = "freetunnel.log";
    static void append(File file, String entry) { }
}
EOF
  cat > "$dir/app/src/main/java/org/x/AndroidUpdater.kt" <<'EOF'
object AndroidUpdater { @Volatile var state = 0; private var release: String? = null }
EOF
  cat > "$dir/app/src/main/res/values/strings.xml" <<'EOF'
<resources><string name="config_added">"Config added: %1$s"</string></resources>
EOF
  cat > "$dir/app/src/androidTest/java/org/x/T.kt" <<'EOF'
class T {
    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name)
    @Test fun a() {
        field("page"); method("render")
        updater("state", 1)
        focusOn(a, "tab:3"); click(a, "config:0:select"); click(a, "home:toggle")
        LogFile.append(f, "x"); val n = LogFile.NAME
        context.getString(R.string.config_added, "x")
        // "no:such" in a comment is no key
    }
}
EOF
}
tree "$TMP/good"
expect "a tree that matches passes" pass "$TMP/good"
tree "$TMP/key"; sed -i 's/"home:toggle", Look.HERO/"home:logo", Look.HERO/' "$TMP/key/app/src/main/java/org/x/MainActivity.java"
expect "a key that is gone" fail "$TMP/key" 'home:toggle'
tree "$TMP/field"; sed -i 's/private int page;//' "$TMP/field/app/src/main/java/org/x/MainActivity.java"
expect "a field that is gone" fail "$TMP/field" '"page"'
tree "$TMP/updater"; sed -i 's/@Volatile var state = 0;//' "$TMP/updater/app/src/main/java/org/x/AndroidUpdater.kt"
expect "an updater field that is gone" fail "$TMP/updater" '"state"'
tree "$TMP/string"; sed -i 's/config_added/config_made/' "$TMP/string/app/src/main/res/values/strings.xml"
expect "a string that is gone" fail "$TMP/string" 'config_added'
tree "$TMP/other-key"; sed -i 's/focusOn(a, "tab:3")/focusOn(a, "nav:3")/' "$TMP/other-key/app/src/androidTest/java/org/x/T.kt"
expect "a test that asks for a key the app never had" fail "$TMP/other-key" 'nav:3'
tree "$TMP/const"; sed -i 's/String NAME/String FILE/' "$TMP/const/app/src/main/java/org/x/LogFile.java"
expect "a constant that is gone" fail "$TMP/const" 'LogFile has no "NAME"'

# 2c. runOnMainSync throws on the main thread, so a block run there may not run another, not even through a helper that does.
tree "$TMP/nested"
cat >> "$TMP/nested/app/src/androidTest/java/org/x/T.kt" <<'FIXTURE'
class N {
    private fun <T> onMain(block: () -> T): T = block()
    private fun focused() = onMain { 1 }
    @Test fun direct() { onMain { onMain { 2 } } }
    @Test fun throughAHelper() { onMain { focused() } }
}
FIXTURE
expect "an onMain inside an onMain, direct or through a helper" fail "$TMP/nested" 'onMain does not nest'
tree "$TMP/not-nested"
cat >> "$TMP/not-nested/app/src/androidTest/java/org/x/T.kt" <<'FIXTURE'
class N {
    private fun <T> onMain(block: () -> T): T = block()
    private fun focused() = onMain { 1 }
    @Test fun inSequence() { onMain { 2 }; focused(); val text = "onMain { onMain { } } in a string" /* onMain { onMain { } } */ }
}
FIXTURE
expect "onMain blocks in a row, and the word in a string or a comment, pass" pass "$TMP/not-nested"

# 2d. A test whose eventually() starts with the message it fails with takes no call without one.
tree "$TMP/no-message"
cat >> "$TMP/no-message/app/src/androidTest/java/org/x/T.kt" <<'FIXTURE'
class M {
    private fun eventually(what: String, timeoutMs: Long = 10_000, condition: () -> Boolean) { }
    @Test fun ok() { eventually("it comes") { true }; eventually("and later", 5_000) { true } }
    @Test fun bad() { eventually { true } }
}
FIXTURE
expect "an eventually() with no message" fail "$TMP/no-message" 'eventually() without its message'
tree "$TMP/message"
cat >> "$TMP/message/app/src/androidTest/java/org/x/T.kt" <<'FIXTURE'
class M {
    private fun eventually(what: String, timeoutMs: Long = 10_000, condition: () -> Boolean) { }
    private fun waitFor(timeoutMs: Long, condition: () -> Boolean) { }
    @Test fun ok() { eventually("it comes") { true }; eventually("and later", 5_000) { true }; waitFor(5_000) { true } }
}
FIXTURE
expect "eventually() with its message passes" pass "$TMP/message"

# 2e. The tour may not lose a screen: a copy of the real app and tests, with a picture dropped from the tour.
mkdir -p "$TMP/tour/app/src"
cp -r "$ROOT/android/app/src/main" "$ROOT/android/app/src/androidTest" "$TMP/tour/app/src/"
sed -i 's/shot("logs-off"/shot("logs-words"/' "$TMP/tour/app/src/androidTest/java/org/freetunnel/android/UiTourDeviceTest.kt"
expect "a screen dropped from the tour" fail "$TMP/tour" 'the tour takes no picture "logs-off"'

exit $fail
