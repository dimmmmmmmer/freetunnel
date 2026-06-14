import QtQuick

// Small reusable on/off switch.
Rectangle {
    id: root
    property bool checked: false
    property color accent: "#185fa5"
    implicitWidth: 38
    implicitHeight: 22
    radius: height / 2
    color: checked ? accent : "#d3d6dc"

    Rectangle {
        width: 16; height: 16; radius: 8; color: "white"
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? parent.width - width - 3 : 3
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
    MouseArea { anchors.fill: parent; onClicked: root.checked = !root.checked }
}
