import QtQuick

// Labelled text input used by the config form. Colours are passed in so it
// follows the active theme.
Column {
    property alias label: lbl.text
    property alias text: input.text
    property string placeholder: ""
    property bool password: false
    property color labelColor: "#6b7280"
    property color fieldBg: "#ffffff"
    property color fieldBorder: "#e7e9ee"
    property color fieldFocus: "#4b5563"
    property color textColor: "#1b1d21"
    property color placeholderColor: "#9aa1ab"
    width: parent ? parent.width : 240
    spacing: 4

    Text { id: lbl; color: labelColor; font.pixelSize: 13 }
    Rectangle {
        width: parent.width; height: 34; radius: 8
        color: fieldBg; border.color: input.activeFocus ? fieldFocus : fieldBorder; border.width: 1
        TextInput {
            id: input
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter; clip: true
            font.pixelSize: 14; color: textColor
            echoMode: password ? TextInput.Password : TextInput.Normal
        }
        Text {
            text: placeholder; color: placeholderColor; font.pixelSize: 14
            anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length === 0 && !input.activeFocus
        }
    }
}
