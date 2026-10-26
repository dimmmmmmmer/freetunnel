#!/usr/bin/env bash
# Every dialog in MainActivity goes through its show(), which gives the buttons a
# focus ring: on a TV remote, a dialog shown any other way has buttons that never
# show which one is focused. This fails on a .show() anywhere else but a Toast's.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# bare_shows <file>: prints each .show() that bypasses show(), and fails if any.
# The one allowed is the first line of show(AlertDialog dialog) itself.
bare_shows() {
  awk '/\.show\(\)/ && !/Toast\.makeText\(.*\)\.show\(\);/ &&
       !(prev ~ /private void show\(AlertDialog dialog\) \{$/ && /^ *dialog\.show\(\);$/) { print FNR ": " $0; found = 1 }
       { prev = $0 }
       END { exit found }' "$1"
}

indent() { sed 's/^/    /'; }

fail=0
# check <name> <expect: pass|fail> <file>
check() {
  local name=$1 expect=$2 file=$3 got=pass out
  out="$(bare_shows "$file")" || got=fail
  if [ "$got" = "$expect" ]; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
  fi
}

cat > "$TMP/good" <<'EOF'
    private void toast(String text) { Toast.makeText(this, text, Toast.LENGTH_LONG).show(); }
    private void show(AlertDialog dialog) {
        dialog.show();
    }
        show(new AlertDialog.Builder(this).setMessage(m));
EOF
check "show() and toasts" pass "$TMP/good"
printf '%s\n' '        new AlertDialog.Builder(this).setMessage(m)' '                .setPositiveButton(ok, null).show();' > "$TMP/builder"
check "a builder shown directly" fail "$TMP/builder"
printf '%s\n' '        editor.show();' > "$TMP/dialog"
check "a dialog shown directly" fail "$TMP/dialog"
printf '%s\n' '    private void confirm() {' '        dialog.show();' > "$TMP/elsewhere"
check "a dialog named dialog, shown elsewhere" fail "$TMP/elsewhere"

check "MainActivity.java" pass "$ROOT/android/app/src/main/java/org/freetunnel/android/MainActivity.java"
exit "$fail"
