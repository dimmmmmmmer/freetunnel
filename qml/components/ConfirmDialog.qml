import QtQuick

Item {
    id: cd
    required property var theme
    property string text: ""
    property string confirmText: qsTr("Delete")
    signal confirmed()
    anchors.fill: parent
    visible: false
    z: 2000
    function open() { visible = true }
    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; onClicked: cd.visible = false } }
    Rectangle {
        anchors.centerIn: parent
        // Narrow card: elide long names instead of stretching edge-to-edge.
        width: Math.min(parent.width - 56, 252)
        height: cdCol.implicitHeight + 24
        radius: 12; color: theme.bg; border.color: theme.border; border.width: 1
        Column {
            id: cdCol; width: parent.width - 28; anchors.centerIn: parent; spacing: 14
            Text { id: cdText; width: parent.width
                   wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight
                   text: cd.text
                   color: theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
            Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                Rectangle { width: Math.max(76, c1t.implicitWidth + 26); height: 32; radius: 8
                    color: c1.containsMouse ? theme.border : theme.surface
                    Text { id: c1t; anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                    MouseArea { id: c1; anchors.fill: parent; hoverEnabled: true; onClicked: cd.visible = false } }
                Rectangle { width: Math.max(76, c2t.implicitWidth + 26); height: 32; radius: 8
                    color: c2.containsMouse ? Qt.darker(theme.danger, 1.15) : theme.danger
                    Text { id: c2t; anchors.centerIn: parent; text: cd.confirmText; color: "white"; font.pixelSize: 14 }
                    MouseArea { id: c2; anchors.fill: parent; hoverEnabled: true
                                onClicked: { cd.visible = false; cd.confirmed() } } }
            }
        }
    }
    Shortcut { sequences: ["Escape"]; enabled: cd.visible; onActivated: cd.visible = false }
}
