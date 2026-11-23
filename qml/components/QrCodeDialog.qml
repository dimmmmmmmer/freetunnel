import QtQuick
import QtQuick.Window

// A config's deep link as a QR code, for a phone to scan (the Android app's Scan QR
// code, first in its + menu): the Configs page's export menu opens it with the
// modules Backend::configQrCode made. Window level and modal, as ConfirmDialog is,
// and dismissed the same ways: Close, Escape, Return, or a click on the dim.
//
// The code is always dark on white, whatever the theme: a phone's scanner looks
// for dark modules on a light ground, and a dark theme's light-on-dark code is one
// that many of them do not read. It sits in a white square with the four-module
// quiet zone the standard asks for, so the card's colour never touches it.
Item {
    id: qd
    required property var theme
    // What the card says over the code: the config's name, quoted.
    property string title: ""
    // Modules a side, and the modules row by row, '1' dark (see QrMatrix.h).
    property int size: 0
    property string modules: ""
    anchors.fill: parent
    visible: false
    z: 2000

    property Item focusBefore: null
    function open() {
        if (!visible)
            focusBefore = Window.activeFocusItem
        visible = true
        forceActiveFocus()
        qrCanvas.requestPaint()
    }
    function close() { visible = false }
    onVisibleChanged: {
        if (visible)
            return
        // The link holds the password: nothing of it stays in the item once the
        // code is off the screen.
        modules = ""
        size = 0
        const back = focusBefore
        focusBefore = null
        if (back && back.visible && back.enabled)
            back.forceActiveFocus()
    }
    onModulesChanged: qrCanvas.requestPaint()

    // Whole pixels a module, so that every module is the same size and its edges
    // are sharp: a scanner reads a blurred or uneven grid badly. As large as the
    // window leaves room for, up to 8 px, including a compact code on a small window.
    readonly property int quiet: 4
    readonly property int modulePx: size <= 0 ? 0
            : Math.max(1, Math.min(8, Math.floor(Math.min(qrCard.width - 28,
                height - qrTitle.implicitHeight - qrHint.implicitHeight - 116) / (size + 2 * quiet))))
    readonly property int codePx: modulePx * (size + 2 * quiet)

    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: qd.close() } }
    Rectangle {
        id: qrCard
        objectName: "qrCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 420)
        height: qrCol.implicitHeight + 24
        radius: 12; color: theme.bg; border.color: theme.border; border.width: 1
        // A click on the card is not one on the dim behind it.
        TapHandler {}
        Column {
            id: qrCol; width: parent.width - 28; anchors.centerIn: parent; spacing: 12
            Text { id: qrTitle; objectName: "qrTitle"; width: parent.width; wrapMode: Text.Wrap
                   horizontalAlignment: Text.AlignHCenter
                   text: qd.title; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
            Rectangle {
                objectName: "qrCode"
                anchors.horizontalCenter: parent.horizontalCenter
                width: qd.codePx; height: qd.codePx; color: "#ffffff"
                Canvas {
                    id: qrCanvas
                    anchors.fill: parent
                    antialiasing: false
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = "#ffffff"
                        ctx.fillRect(0, 0, width, height)
                        const n = qd.size, px = qd.modulePx, m = qd.modules
                        if (n <= 0 || px <= 0 || m.length !== n * n)
                            return
                        ctx.fillStyle = "#000000"
                        const off = qd.quiet * px
                        for (let y = 0; y < n; ++y) {
                            // A run of dark modules in a row is one rectangle.
                            let x = 0
                            while (x < n) {
                                if (m.charAt(y * n + x) !== "1") { ++x; continue }
                                let end = x
                                while (end < n && m.charAt(y * n + end) === "1") ++end
                                ctx.fillRect(off + x * px, off + y * px, (end - x) * px, px)
                                x = end
                            }
                        }
                    }
                }
            }
            Text { id: qrHint; objectName: "qrHint"; width: parent.width; wrapMode: Text.Wrap
                   horizontalAlignment: Text.AlignHCenter
                   text: qsTr("Scan it with FreeTunnel for Android: + → Scan QR code. It contains the password: show it only to the phone you are adding this server to.")
                   color: theme.textDim; font.pixelSize: 12 }
            Rectangle { objectName: "qrCloseButton"
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(76, qrCloseText.implicitWidth + 26); height: 32; radius: 8
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qrCloseText.text
                Accessible.onPressAction: qd.close()
                color: qrCloseMa.containsMouse ? theme.surfaceHover : theme.surface
                Behavior on color { ColorAnimation { duration: 120 } }
                Text { id: qrCloseText; anchors.centerIn: parent; text: qsTr("Close"); color: theme.text; font.pixelSize: 14 }
                MouseArea { id: qrCloseMa; anchors.fill: parent; hoverEnabled: true; onClicked: qd.close() } }
        }
    }
    // escapeOwner as in ConfirmDialog: a question that comes up over this card (a
    // deep link can put one there unprompted) takes the keys while it is up, and
    // two enabled shortcuts on one key make Qt run neither.
    property bool escapeOwner: true
    Shortcut { sequences: ["Escape", "Return", "Enter"]; enabled: qd.visible && qd.escapeOwner; onActivated: qd.close() }
    // Tab stays in it, as in ConfirmDialog: the page under the dim is out of reach.
    Keys.onTabPressed: function(e) { e.accepted = true }
    Keys.onBacktabPressed: function(e) { e.accepted = true }
}
