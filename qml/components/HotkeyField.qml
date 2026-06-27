import QtQuick
import QtQuick.Layouts

Item {
    id: hk
    required property var shell
    required property var theme
    property string label: ""
    property string value: ""
    signal captured(string seq)
    Layout.fillWidth: true
    Layout.preferredHeight: 42
    property bool capturing: false

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
                text: hk.capturing ? qsTr("Press…") : (hk.value ? shell.keyGlyphs(hk.value) : "—")
                color: (hk.value || hk.capturing) ? theme.text : theme.textFaint
                font.pixelSize: 13
            }
            MouseArea { id: hkMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: { hk.capturing = true; hk.forceActiveFocus() } }
        }
    }
    Keys.onPressed: function(e) {
        if (!hk.capturing) return
        e.accepted = true
        if (e.key === Qt.Key_Escape) { hk.capturing = false; return }
        if (e.key === Qt.Key_Control || e.key === Qt.Key_Shift
                || e.key === Qt.Key_Alt || e.key === Qt.Key_Meta) return
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
            kn = backend.physicalLetterForScanCode(e.nativeScanCode)
        }
        if (kn === "") return
        parts.push(kn)
        hk.capturing = false
        hk.captured(parts.join("+"))
    }
}
