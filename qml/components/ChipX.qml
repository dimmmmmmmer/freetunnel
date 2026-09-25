import QtQuick

Item {
    id: cx
    required property var theme
    property bool overAccent: false
    signal clicked()
    implicitWidth: 18; implicitHeight: 18
    Icon {
        anchors.centerIn: parent
        width: 14; height: 14
        svg: "qrc:/icons/close.svg"
        theme: cx.theme
        color: cxMa.containsMouse ? theme.danger
              : (cx.overAccent ? theme.accentText : theme.textDim)
    }
    // An arrow cursor, as on every button; the hand is for links (see WindowControls).
    MouseArea { id: cxMa; anchors.fill: parent; anchors.margins: -6
                hoverEnabled: true; onClicked: cx.clicked() }
}
