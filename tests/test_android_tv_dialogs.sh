#!/usr/bin/env bash
# What the Android app may put on the screen: no system dialog, and every overlay through
# the one helper. The app's dialogs, menus, pickers, confirmations and notices are all
# drawn in its own window, in the desktop's look (qml/ConfirmDialog.qml, Dropdown,
# CreateConfigOverlay, AppPickerOverlay), and a TV remote depends on that.
#
# 1. No system dialog, menu, popup or notice anywhere under android/app/src/main/java: not
#    android.app.AlertDialog, AlertDialog.Builder, android.app.Dialog, DialogFragment,
#    PopupWindow, PopupMenu, ListPopupWindow or Spinner, which draw in the system's look,
#    in a window of their own, and need a ring of their own to show a remote's focus.
#    Nor the system's Toast, except where no window of the app's own can show one:
#    QuickTileService and VpnPermissionActivity.
#    Only screens the OS owns stay system UI (the VPN consent, the package installer, the file
#    pickers, Android's settings), and none of them is made with these classes.
# 2. Every overlay goes through showOverlay(), the one helper that adds to overlayLayer (the cards)
#    and popupLayer (the popups and the confirmations, which are above the toast): it is
#    what traps the focus, hides the page behind it, gives the focus back and handles Back. An
#    overlayLayer.addView( or popupLayer.addView( anywhere else, in MainActivity or in any other file,
#    would be an overlay with none of that; and so would a new OverlayLayer( made anywhere else.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# code <file>: the file with its comments taken out, and what is inside its string and character
# literals, so that a word in prose or in a string is no use of a class. Done in one pass, from the left, so that
# a comment mark inside a string (the MIME type "*/*") starts nothing, and a quote inside a comment ends nothing.
code() {
  perl -0777 -pe 's{("(?:\\.|[^"\\\n])*")|(\x27(?:\\.|[^\x27\\\n])*\x27)|/\*.*?\*/|//[^\n]*}{defined $1 ? "\"\"" : defined $2 ? "\x27\x27" : ""}gse' "$1"
}

# banned_classes <file>: prints each line that uses a system dialog, menu or popup, and fails if any.
banned_classes() {
  code "$1" | grep -n -E '(AlertDialog|\bDialog\b|\bDialogFragment\b|\bPopupWindow\b|\bPopupMenu\b|\bListPopupWindow\b|\bSpinner\b)' && return 1
  return 0
}

# banned_toast <file>: prints each use of the system's Toast, and fails if any.
banned_toast() {
  code "$1" | grep -n -E '\bToast\b' && return 1
  return 0
}

# stray_adds <file>: prints each overlayLayer.addView( or new OverlayLayer( outside showOverlay,
# and fails if any. showOverlay runs from its declaration to the first line that is a lone 4-space }.
stray_adds() {
  awk '/^    (private )?[A-Za-z.<>]+ showOverlay\(/ { inside = 1 }
       /(overlayLayer\.addView\(|popupLayer\.addView\(|new OverlayLayer\()/ && !inside { print FNR ": " $0; found = 1 }
       inside && /^    }$/ { inside = 0 }
       END { exit found }' "$1"
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

# The rules themselves, on snippets: what is allowed passes, each way round them fails.
cat > "$TMP/good" <<'EOF'
// A ConfirmDialog, a Dialog and a Toast named in a comment are no use of them.
/* An AlertDialog.Builder in a block comment, and a
   PopupWindow across two lines of it. */
    private void toast(String text) { showToast(text); }
    private void closeDialog(Overlay overlay) { closeOverlay(overlay); }
    private final class ConfirmDialogCard { }
    String url = "https://example.invalid/Dialog";
EOF
check "overlays and in-app toasts" banned_classes pass "$TMP/good"
check "a Toast named only in a comment" banned_toast pass "$TMP/good"
printf '%s\n' 'import android.app.AlertDialog;' > "$TMP/alert"
check "an AlertDialog import" banned_classes fail "$TMP/alert"
printf '%s\n' '        new AlertDialog.Builder(this).setMessage(m).show();' > "$TMP/builder"
check "an AlertDialog.Builder" banned_classes fail "$TMP/builder"
printf '%s\n' '        new androidx.appcompat.app.AlertDialog.Builder(this);' > "$TMP/appcompat"
check "an AppCompat AlertDialog" banned_classes fail "$TMP/appcompat"
printf '%s\n' 'import android.app.Dialog;' > "$TMP/dialog-import"
check "an android.app.Dialog import" banned_classes fail "$TMP/dialog-import"
printf '%s\n' '        Dialog dialog = new Dialog(this);' > "$TMP/dialog"
check "a Dialog" banned_classes fail "$TMP/dialog"
printf '%s\n' 'class Mine : DialogFragment() {' > "$TMP/fragment"
check "a DialogFragment" banned_classes fail "$TMP/fragment"
printf '%s\n' '        PopupWindow popup = new PopupWindow(view);' > "$TMP/popup-window"
check "a PopupWindow" banned_classes fail "$TMP/popup-window"
printf '%s\n' '        PopupMenu menu = new PopupMenu(this, anchor);' > "$TMP/popup-menu"
check "a PopupMenu" banned_classes fail "$TMP/popup-menu"
printf '%s\n' '        ListPopupWindow list = new ListPopupWindow(this);' > "$TMP/list-popup"
check "a ListPopupWindow" banned_classes fail "$TMP/list-popup"
printf '%s\n' '        Spinner spinner = new Spinner(this);' > "$TMP/spinner"
check "a Spinner" banned_classes fail "$TMP/spinner"
# A comment mark inside a string starts no comment: what follows the MIME type is still read.
printf '%s\n' '        intent.setType("*/*");' '        PopupMenu menu = new PopupMenu(this, anchor);' > "$TMP/mime"
check "a PopupMenu after a string holding /*" banned_classes fail "$TMP/mime"
printf '%s\n' '        Toast.makeText(this, text, Toast.LENGTH_LONG).show();' > "$TMP/toast"
check "a system Toast" banned_toast fail "$TMP/toast"
printf '%s\n' 'import android.widget.Toast' > "$TMP/toast-import"
check "an import of the system's Toast" banned_toast fail "$TMP/toast-import"

cat > "$TMP/overlay-good" <<'EOF'
    private Overlay showOverlay(String name, OverlayLayer.Kind kind, View content, String anchorKey, Runnable onClose) {
        Overlay open = overlay(name);
        OverlayLayer layer = new OverlayLayer(this, kind, backdrop, panel);
        overlayLayer.addView(layer, new FrameLayout.LayoutParams(-1, -1));
        return overlay;
    }
    private void render() {
        pageRoot.addView(bar);
    }
EOF
check "overlayLayer.addView inside showOverlay" stray_adds pass "$TMP/overlay-good"
cat > "$TMP/overlay-stray" <<'EOF'
    private Overlay showOverlay(String name, OverlayLayer.Kind kind, View content, String anchorKey, Runnable onClose) {
        overlayLayer.addView(layer, new FrameLayout.LayoutParams(-1, -1));
        return overlay;
    }
    private void myOwnPopup() {
        overlayLayer.addView(popup);
    }
EOF
check "overlayLayer.addView in another method" stray_adds fail "$TMP/overlay-stray"
cat > "$TMP/overlay-before" <<'EOF'
    private void earlier() {
        overlayLayer.addView(popup);
    }
    private Overlay showOverlay(String name, OverlayLayer.Kind kind, View content, String anchorKey, Runnable onClose) {
        overlayLayer.addView(layer, new FrameLayout.LayoutParams(-1, -1));
    }
EOF
check "overlayLayer.addView before showOverlay" stray_adds fail "$TMP/overlay-before"
printf '%s\n' '        host.overlayLayer.addView(card);' > "$TMP/overlay-other-file"
check "overlayLayer.addView in some other file" stray_adds fail "$TMP/overlay-other-file"
printf '%s\n' '        OverlayLayer mine = new OverlayLayer(this, kind, backdrop, panel);' > "$TMP/overlay-new"
check "a new OverlayLayer outside showOverlay" stray_adds fail "$TMP/overlay-new"
printf '%s\n' '    private void myOwnConfirm() {' '        popupLayer.addView(confirm);' '    }' > "$TMP/popup-stray"
check "popupLayer.addView in another method" stray_adds fail "$TMP/popup-stray"
cat > "$TMP/popup-good" <<'EOF'
    private Overlay showOverlay(String name, OverlayLayer.Kind kind, View content, String anchorKey, Runnable onClose, boolean passive) {
        FrameLayout parent = kind == OverlayLayer.Kind.CARD ? overlayLayer : popupLayer;
        parent.addView(layer, new FrameLayout.LayoutParams(-1, -1));
        popupLayer.addView(layer, new FrameLayout.LayoutParams(-1, -1));
        return overlay;
    }
EOF
check "popupLayer.addView inside showOverlay" stray_adds pass "$TMP/popup-good"

# The app.
MAIN="$ROOT/android/app/src/main/java/org/freetunnel/android"
files=0
toast_allowed=0
for file in "$MAIN"/*.java "$MAIN"/*.kt; do
  [ -f "$file" ] || continue
  files=$((files + 1))
  base="$(basename "$file")"
  if ! out="$(banned_classes "$file")"; then
    echo "not ok - $base uses a system dialog, menu or popup"
    indent <<<"$out"
    fail=1
  fi
  # The tile and the VPN consent screen have no window of the app's own to draw a toast in.
  case "$base" in
    QuickTileService.java|VpnPermissionActivity.java) toast_allowed=$((toast_allowed + 1)) ;;
    *)
      if ! out="$(banned_toast "$file")"; then
        echo "not ok - $base uses the system's Toast"
        indent <<<"$out"
        fail=1
      fi ;;
  esac
  # showOverlay lives in MainActivity.java; no other file adds to the layer or makes one.
  if [ "$base" != "MainActivity.java" ]; then
    if ! out="$(stray_adds "$file")"; then
      echo "not ok - $base adds to overlayLayer"
      indent <<<"$out"
      fail=1
    fi
  fi
done
if ! out="$(stray_adds "$MAIN/MainActivity.java")"; then
  echo "not ok - MainActivity.java adds to overlayLayer outside showOverlay"
  indent <<<"$out"
  fail=1
fi
# The two that may use the system's Toast are still there, or the exception above is for nothing.
if [ "$toast_allowed" -ne 2 ]; then
  echo "not ok - QuickTileService.java and VpnPermissionActivity.java are expected: found $toast_allowed of them"
  fail=1
fi
[ "$fail" -ne 0 ] || echo "ok - none of the $files source files uses a system dialog, menu, popup or Toast (bar the two that must), or adds an overlay but through showOverlay"
exit "$fail"
