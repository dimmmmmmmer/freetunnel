import QtQuick
import QtQuick.Layouts

Item {
    id: hk
    required property var shell
    required property var theme
    property string label: ""
    property string value: ""
    // Set, switched on, and not in effect: refused, or held by another app.
    property bool unavailable: false
    signal captured(string seq)
    Layout.fillWidth: true
    Layout.preferredHeight: 42
    property bool capturing: false
    // A key was pressed with nothing that makes it safe to take system-wide.
    property bool needsModifier: false
    onCapturingChanged: {
        needsModifier = false
        // A combo FreeTunnel has registered is handed to that registration and
        // never reaches the field, so pressing it ran its action instead.
        backend.suspendHotkeys(capturing)
    }
    Component.onDestruction: if (capturing) backend.suspendHotkeys(false)

    // Capturing relies on this Item holding active focus (Keys.onPressed below).
    // Active focus is unique per window, so starting capture on another field —
    // or clicking empty space (the root focusSink steals focus) — drops it here
    // and must end this field's capture, otherwise several fields show "Press…"
    // at once and a half-started capture stays stuck on the old value.
    onActiveFocusChanged: if (!hk.activeFocus) hk.capturing = false
    RowLayout {
        anchors.fill: parent
        Text { text: hk.label; color: theme.text; font.pixelSize: 14 }
        Item { Layout.fillWidth: true }
        Rectangle {
            Layout.preferredHeight: 28
            Layout.preferredWidth: Math.max(96, lbl.implicitWidth + 24)
            radius: 6
            color: hk.capturing ? theme.infoBg : (hkMa.containsMouse ? theme.border : theme.surface)
            Behavior on color { ColorAnimation { duration: 120 } }
            border.width: hk.capturing ? 1 : 0; border.color: theme.accent
            Text {
                id: lbl; anchors.centerIn: parent
                text: !hk.capturing ? (hk.value ? shell.keyGlyphs(hk.value) : "—")
                      : !hk.needsModifier ? qsTr("Press…")
                      : Qt.platform.os === "osx" ? qsTr("Add ⌘, ⌥ or ⌃…")
                      : qsTr("Add Ctrl or Alt…")
                color: hk.capturing ? theme.text
                       : !hk.value ? theme.textFaint
                       : hk.unavailable ? theme.danger : theme.text
                font.pixelSize: 13
            }
            MouseArea { id: hkMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: { hk.capturing = true; hk.forceActiveFocus() } }
        }
    }
    // While capturing, claim the key press before the window's Shortcuts see it.
    // Without this, binding a combo that is already a window shortcut is
    // impossible — ⌘Q / Ctrl+Q would quit the app mid-capture (Shortcut outranks
    // Keys.onPressed), and Ctrl+V would fire the config paste on the configs page.
    Keys.onShortcutOverride: function(e) { if (hk.capturing) e.accepted = true }
    Keys.onPressed: function(e) {
        if (!hk.capturing) return
        e.accepted = true
        if (e.key === Qt.Key_Escape) { hk.capturing = false; return }
        if (e.key === Qt.Key_Control || e.key === Qt.Key_Shift
                || e.key === Qt.Key_Alt || e.key === Qt.Key_Meta) return
        const held = e.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        // Backspace or Delete on its own unbinds this action.
        if (!held && (e.key === Qt.Key_Backspace || e.key === Qt.Key_Delete)) {
            hk.capturing = false
            hk.captured("")
            return
        }
        // A global hotkey takes its combo from every other application, so a key
        // on its own — Enter to confirm, Tab to move on, a letter — would stop
        // working everywhere else. F1–F12 are the only keys safe without Ctrl,
        // Alt or Meta (⌘, ⌥ or ⌃ on macOS); anything else waits for one.
        if (!held && !(e.key >= Qt.Key_F1 && e.key <= Qt.Key_F12)) {
            hk.needsModifier = true
            return
        }
        var parts = []
        if (e.modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (e.modifiers & Qt.AltModifier) parts.push("Alt")
        if (e.modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (e.modifiers & Qt.MetaModifier) parts.push("Meta")
        var kn = shell.keyName(e.key, e.text)
        if (kn === "") {
            // Non-Latin layout (e.g. Russian): key()/text() are Cyrillic. Recover
            // the Latin letter from the physical key position so the captured
            // shortcut is the same whatever layout was active.
            kn = backend.physicalLetterForKey(e.nativeScanCode, e.nativeVirtualKey)
        }
        if (kn === "") return
        parts.push(kn)
        hk.capturing = false
        hk.captured(parts.join("+"))
    }
}
