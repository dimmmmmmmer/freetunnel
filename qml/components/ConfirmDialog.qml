import QtQuick
import QtQuick.Window

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
    // It takes the keyboard while it is up, so Return and Escape answer it. A
    // hotkey field left recording underneath claimed every key first, and Return
    // meant for this dialog was saved as a system-wide hotkey instead. Focus goes
    // back where it was when the dialog closes.
    property Item focusBefore: null
    function open() {
        if (!visible)
            focusBefore = Window.activeFocusItem
        visible = true
        forceActiveFocus()
    }
    function close() { visible = false }
    onVisibleChanged: {
        armed = false
        if (visible) {
            armTimer.restart()
            return
        }
        armTimer.stop()
        const back = focusBefore
        focusBefore = null
        if (back && back.visible && back.enabled)
            back.forceActiveFocus()
    }

    // The message's natural width: its widest line. Not TextMetrics, which takes
    // text as one line, newlines and all, so a two-line question was sized as both
    // lines end to end and the card spread across the window around a short text.
    // A Text of its own, not cdText, whose width is bound to the card's.
    Text { id: cdNatural; visible: false; text: cd.text; font.pixelSize: 14 }

    // Buttons cannot wrap the way text does. When the row is wider than the card
    // can be, the buttons give up their padding and minimum width, and a label
    // that still does not fit elides, rather than the row running into the card's
    // edges or past them.
    readonly property real buttonRoom: width - 24 - 28
    readonly property real fullButtonRow: Math.max(76, c1t.implicitWidth + 26)
            + (altText !== "" ? Math.max(76, c3t.implicitWidth + 26) + 8 : 0)
            + Math.max(76, c2t.implicitWidth + 26) + 8
    readonly property bool compactButtons: fullButtonRow > buttonRoom
    readonly property int buttonCount: altText !== "" ? 3 : 2
    function buttonWidth(label) {
        if (!compactButtons)
            return Math.max(76, label.implicitWidth + 26)
        return Math.min(label.implicitWidth + 16, (buttonRoom - 8 * (buttonCount - 1)) / buttonCount)
    }

    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; onClicked: cd.visible = false } }
    Rectangle {
        objectName: "confirmCard"
        anchors.centerIn: parent
        // Fit to message + buttons; cdText.width = parent.width made implicitWidth
        // stretch to the old fixed 252 px cap and left empty side margins. The text
        // wraps, so it stays within the usual margin; the buttons cannot, so they
        // may take some of it — three Russian ones ran into the card's border at
        // the default width.
        width: Math.min(parent.width - 24,
                        Math.max(btnRow.implicitWidth + 28,
                                 Math.min(parent.width - 56,
                                          Math.ceil(cdNatural.implicitWidth) + 28)))
        height: cdCol.implicitHeight + 24
        radius: 12; color: theme.bg; border.color: theme.border; border.width: 1
        // A click on the card is not a click on the backdrop behind it: it used
        // to fall through and cancel, as the message or the gap between two
        // buttons were clicked. The editor and the app picker do the same.
        TapHandler {}
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
                // Wrap, not WordWrap: a long hostname has no space to break at, and
                // WordWrap let it run past both edges of the card, cut off at each
                // end — on the one line of an import prompt the user can trust.
                Text { id: cdText; objectName: "confirmText"; width: cdFlick.width
                       wrapMode: Text.Wrap
                       text: cd.text
                       color: theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
            }
            Row { id: btnRow; anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                Rectangle { objectName: "cancelButton"
                    width: cd.buttonWidth(c1t); height: 32; radius: 8
                    color: c1.containsMouse ? theme.border : theme.surface
                    Text { id: c1t; anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14
                           width: Math.min(implicitWidth, parent.width - 8); elide: Text.ElideRight }
                    MouseArea { id: c1; anchors.fill: parent; hoverEnabled: true; onClicked: cd.visible = false } }
                Rectangle { objectName: "alternateButton"
                    visible: cd.altText !== ""
                    width: visible ? cd.buttonWidth(c3t) : 0; height: 32; radius: 8
                    color: c3.containsMouse ? theme.border : theme.surface
                    Text { id: c3t; anchors.centerIn: parent; text: cd.altText; color: theme.text; font.pixelSize: 14
                           width: Math.min(implicitWidth, parent.width - 8); elide: Text.ElideRight }
                    MouseArea { id: c3; anchors.fill: parent; hoverEnabled: true
                                onClicked: { cd.visible = false; cd.alternate() } } }
                Rectangle { objectName: "confirmButton"
                    width: cd.buttonWidth(c2t); height: 32; radius: 8
                    color: c2.containsMouse ? Qt.darker(theme.danger, 1.15) : theme.danger
                    Text { id: c2t; anchors.centerIn: parent; text: cd.confirmText; color: "white"; font.pixelSize: 14
                           width: Math.min(implicitWidth, parent.width - 8); elide: Text.ElideRight }
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
    // The same when the keys come back to it from a dialog over it that has just
    // closed: a Return pressed twice, or held, to answer that one reached this one
    // at once.
    onEscapeOwnerChanged: {
        if (visible && escapeOwner) {
            armed = false
            armTimer.restart()
        }
    }
    // And Tab stays in it while it is up. The editor's fields under the dimmed
    // backdrop are in the tab chain, and typing went into the hidden form.
    Keys.onTabPressed: function(e) { e.accepted = true }
    Keys.onBacktabPressed: function(e) { e.accepted = true }
    // Only for the two-button form. The three-button one is the deep-link name
    // collision, where the primary action replaces an existing config with one a
    // link chose — there is no answer safe enough to be the default, so that one
    // is decided by clicking.
    Shortcut { sequences: ["Return", "Enter"]
               enabled: cd.visible && cd.escapeOwner && cd.armed && cd.altText === ""
               onActivated: { cd.visible = false; cd.confirmed() } }
}
