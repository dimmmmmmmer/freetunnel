import QtQuick

// Small on/off switch. `checked` is driven by a binding; clicking emits
// `toggled(newValue)` so the owner can update its model/backend.
Rectangle {
    id: root
    property bool checked: false
    property color accent: "#6b7280"
    property color offColor: "#c4c8d0"
    signal toggled(bool value)

    implicitWidth: 38
    implicitHeight: 22
    radius: height / 2
    color: checked ? accent : offColor
    Behavior on color { ColorAnimation { duration: 120 } }

    Rectangle {
        width: 16; height: 16; radius: 8; color: "white"
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? parent.width - width - 3 : 3
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
    MouseArea { anchors.fill: parent; onClicked: root.toggled(!root.checked) }
}
