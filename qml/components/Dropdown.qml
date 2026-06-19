import QtQuick
import QtQuick.Layouts

Item {
    id: dd
    required property var shell
    required property var theme
    property string label: ""
    property var model: []     // [{ v: "...", t: "..." }]
    property string value: ""
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
            Layout.preferredWidth: Math.min(ddRow.implicitWidth, 132)
            Layout.maximumWidth: 132
            Layout.fillHeight: true
            Row { id: ddRow; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                Text { anchors.verticalCenter: parent.verticalCenter
                       width: Math.min(implicitWidth, 112); elide: Text.ElideRight
                       text: dd.labelFor(dd.value)
                       color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 14 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "▾"
                       color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 16 }
            }
            MouseArea { id: ddMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: shell.showSelect(ddVal, dd.model, dd.value, function(v){ dd.picked(v) }) }
        }
    }
}
