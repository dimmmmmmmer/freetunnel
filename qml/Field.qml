import QtQuick

// Labelled text input used by the config form.
Column {
    property alias label: lbl.text
    property alias text: input.text
    property string placeholder: ""
    property bool password: false
    width: parent ? parent.width : 240
    spacing: 4

    Text { id: lbl; color: "#6b7280"; font.pixelSize: 13 }
    Rectangle {
        width: parent.width; height: 34; radius: 8
        color: "#ffffff"; border.color: input.activeFocus ? "#185fa5" : "#e7e9ee"; border.width: 1
        TextInput {
            id: input
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter; clip: true
            font.pixelSize: 14; color: "#1b1d21"
            echoMode: password ? TextInput.Password : TextInput.Normal
        }
        Text {
            text: placeholder; color: "#9aa1ab"; font.pixelSize: 14
            anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length === 0 && !input.activeFocus
        }
    }
}
