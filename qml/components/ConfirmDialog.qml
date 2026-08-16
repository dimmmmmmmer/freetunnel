import QtQuick

Item {
    id: cd
    required property var theme
    property string text: ""
    property string confirmText: qsTr("Delete")
    // Optional middle action, e.g. "Replace" alongside "Add copy". Empty hides it,
    // so every existing two-button caller is unaffected.
    property string altText: ""
    signal confirmed()
    signal alternate()
    anchors.fill: parent
    visible: false
    z: 2000
    function open() { visible = true }
    function close() { visible = false }

    TextMetrics { id: cdMetrics; font.pixelSize: 14; text: cd.text }

    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; onClicked: cd.visible = false } }
    Rectangle {
        anchors.centerIn: parent
        // Fit to message + buttons; cdText.width = parent.width made implicitWidth
        // stretch to the old fixed 252 px cap and left empty side margins.
        width: Math.min(parent.width - 56,
                        Math.max(btnRow.implicitWidth + 28,
                                 Math.ceil(cdMetrics.boundingRect.width) + 28))
        height: cdCol.implicitHeight + 24
        radius: 12; color: theme.bg; border.color: theme.border; border.width: 1
        Column {
            id: cdCol; width: parent.width - 28; anchors.centerIn: parent; spacing: 14
            // The message must never be cut off. A deep-link confirmation carries
            // the warning that the link disables certificate verification, and it
            // sits at the end of the text — eliding at four lines dropped exactly
            // the sentence the user needs in order to answer the question. Scroll
            // instead, and only once the text outgrows the window.
            Flickable {
                id: cdFlick
                width: parent.width
                height: Math.min(cdText.implicitHeight, cd.height - 160)
                contentHeight: cdText.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                Text { id: cdText; width: cdFlick.width
                       wrapMode: Text.WordWrap
                       text: cd.text
                       color: theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
            }
            Row { id: btnRow; anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                Rectangle { objectName: "cancelButton"
                    width: Math.max(76, c1t.implicitWidth + 26); height: 32; radius: 8
                    color: c1.containsMouse ? theme.border : theme.surface
                    Text { id: c1t; anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                    MouseArea { id: c1; anchors.fill: parent; hoverEnabled: true; onClicked: cd.visible = false } }
                Rectangle { objectName: "alternateButton"
                    visible: cd.altText !== ""
                    width: visible ? Math.max(76, c3t.implicitWidth + 26) : 0; height: 32; radius: 8
                    color: c3.containsMouse ? theme.border : theme.surface
                    Text { id: c3t; anchors.centerIn: parent; text: cd.altText; color: theme.text; font.pixelSize: 14 }
                    MouseArea { id: c3; anchors.fill: parent; hoverEnabled: true
                                onClicked: { cd.visible = false; cd.alternate() } } }
                Rectangle { objectName: "confirmButton"
                    width: Math.max(76, c2t.implicitWidth + 26); height: 32; radius: 8
                    color: c2.containsMouse ? Qt.darker(theme.danger, 1.15) : theme.danger
                    Text { id: c2t; anchors.centerIn: parent; text: cd.confirmText; color: "white"; font.pixelSize: 14 }
                    MouseArea { id: c2; anchors.fill: parent; hoverEnabled: true
                                onClicked: { cd.visible = false; cd.confirmed() } } }
            }
        }
    }
    // escapeOwner lets an outer dialog stand down while a more-inner one is up:
    // two visible confirm dialogs would otherwise both claim the key and Qt would
    // report the press as ambiguous, so neither would fire. It governs Return as
    // well as Escape, for the same reason.
    property bool escapeOwner: true
    Shortcut { sequences: ["Escape"]
               enabled: cd.visible && cd.escapeOwner
               onActivated: cd.visible = false }

    // Return confirms — but not the instant the dialog appears. A deep link can
    // put this dialog on screen with no warning while the user is typing, and a
    // keystroke already on its way to something else must not answer a question
    // the user has not seen yet. A moment's arming costs nothing to someone who
    // actually reads the dialog.
    property bool armed: false
    Timer { id: armTimer; interval: 400; onTriggered: cd.armed = true }
    onVisibleChanged: {
        armed = false
        if (visible)
            armTimer.restart()
        else
            armTimer.stop()
    }
    // Only for the two-button form. The three-button one is the deep-link name
    // collision, where the primary action replaces an existing config with one a
    // link chose — there is no answer safe enough to be the default, so that one
    // is decided by clicking.
    Shortcut { sequences: ["Return", "Enter"]
               enabled: cd.visible && cd.escapeOwner && cd.armed && cd.altText === ""
               onActivated: { cd.visible = false; cd.confirmed() } }
}
