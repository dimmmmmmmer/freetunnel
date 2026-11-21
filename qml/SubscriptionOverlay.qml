import QtQuick
import QtQuick.Layouts

Item {
    required property var shell
    required property var backend
    required property var theme
    anchors.fill: parent
    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: shell.overlay = "" } }
    Shortcut { sequence: "Escape"; enabled: !shell.windowPopupOpen; onActivated: shell.overlay = "" }
    Rectangle {
        anchors.centerIn: parent; width: Math.min(parent.width - 28, 372)
        height: content.implicitHeight + 32; radius: 14; color: theme.bg; border.color: theme.border
        ColumnLayout {
            id: content; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 16; spacing: 14
            Text { text: qsTr("Subscription"); font.pixelSize: 16; color: theme.text }
            Field {
                id: source; objectName: "subscriptionSource"; Layout.fillWidth: true
                label: qsTr("HTTPS subscription URL")
                labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder
                fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint
                Component.onCompleted: source.input.inputMethodHints = Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 16
                Text { text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14
                    MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: shell.overlay = "" } }
                Rectangle { width: 88; height: 34; radius: 8; color: theme.accent
                    Text { anchors.centerIn: parent; text: qsTr("Add"); color: theme.accentText; font.pixelSize: 14 }
                    MouseArea { anchors.fill: parent; enabled: source.text.trim().length > 0
                        onClicked: { if (backend.beginSubscriptionImport(source.text)) shell.overlay = "" } }
                }
            }
        }
    }
}
