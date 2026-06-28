import QtQuick

Item {
    id: cx
    required property var theme
    property bool onAccent: false
    signal clicked()
    implicitWidth: 18; implicitHeight: 18
    Icon {
        anchors.centerIn: parent
        width: 14; height: 14
        svg: "qrc:/icons/close.svg"
        theme: cx.theme
        color: cxMa.containsMouse ? theme.danger
              : (cx.onAccent ? "white" : theme.textDim)
    }
    MouseArea { id: cxMa; anchors.fill: parent; anchors.margins: -6
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cx.clicked() }
}
