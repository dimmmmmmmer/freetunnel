import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: dd
    required property var shell
    required property var theme
    property string label: ""
    property var model: []     // [{ v: "...", t: "..." }]
    property string value: ""
    property real maximumValueWidth: 132
    signal picked(string v)
    Layout.fillWidth: true
    Layout.preferredHeight: 42
    implicitHeight: 42
    function labelFor(v) {
        for (var i = 0; i < model.length; i++) if (model[i].v === v) return model[i].t
        return ""
    }
    RowLayout {
        anchors.fill: parent
        spacing: 8
        Text {
            text: dd.label; color: theme.text; font.pixelSize: 14
            Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
        }
        // Cap the value column so long labels (e.g. on Settings) don't shove
        // the row past the page margin on narrow windows.
        Item {
            id: ddVal
            Layout.preferredWidth: Math.min(ddRow.implicitWidth, dd.maximumValueWidth)
            Layout.maximumWidth: dd.maximumValueWidth
            Layout.fillHeight: true
            Row { id: ddRow; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                Text { anchors.verticalCenter: parent.verticalCenter
                       width: Math.min(implicitWidth, dd.maximumValueWidth - 20); elide: Text.ElideRight
                       text: dd.labelFor(dd.value)
                       color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 14 }
                Shape {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 5
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeWidth: 0
                        fillColor: ddMa.containsMouse ? theme.text : theme.textDim
                        startX: 0; startY: 0
                        PathLine { x: 8; y: 0 }
                        PathLine { x: 4; y: 5 }
                        PathLine { x: 0; y: 0 }
                    }
                }
            }
            MouseArea { id: ddMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: shell.showSelect(ddVal, dd.model, dd.value, function(v){ dd.picked(v) }) }
        }
    }
}
