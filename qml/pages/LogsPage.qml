import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import ".."

Item {
    required property var shell
    required property var backend
    required property var theme

    ColumnLayout {
        anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
        anchors.topMargin: 6; anchors.bottomMargin: 12; spacing: 8
        Item { Layout.fillWidth: true; Layout.preferredHeight: 24
            Text { id: clrTxt; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                   text: qsTr("Clear"); font.pixelSize: 13
                   color: clrMa.containsMouse ? theme.text : theme.accent
                   font.underline: clrMa.containsMouse }
            MouseArea { id: clrMa; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        width: clrTxt.implicitWidth + 8; height: 22; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: backend.clearLogs() }
            Text { id: cpyTxt; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                   text: qsTr("Copy"); font.pixelSize: 13
                   color: cpyMa.containsMouse ? theme.text : theme.accent
                   font.underline: cpyMa.containsMouse }
            MouseArea { id: cpyMa; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        width: cpyTxt.implicitWidth + 8; height: 22; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { backend.copyToClipboard(backend.logText()); shell.showToast(qsTr("Log copied")) } }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: theme.surface
            clip: true
            Text {
                anchors.centerIn: parent; visible: backend.logModel.count === 0
                text: qsTr("Logs will appear after connecting"); color: theme.textFaint; font.pixelSize: 13
            }
            // One plain-text editor for the whole log (not a virtualized list) so
            // the entire thing — timestamps included — can be selected and copied by
            // hand. Plain text (no per-line RichText/QTextDocument) keeps it cheap,
            // and refreshes are coalesced so a connect-time core-log burst can't
            // thrash the layout. Trade-off: no per-level colour.
            Flickable {
                id: logFlick
                anchors.fill: parent; anchors.margins: 12; clip: true
                contentWidth: width; contentHeight: logView.implicitHeight
                property bool autoScroll: true
                function toBottom() { if (autoScroll) contentY = Math.max(0, contentHeight - height) }
                TextEdit {
                    id: logView
                    width: logFlick.width
                    readOnly: true; selectByMouse: true; persistentSelection: true
                    wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText
                    font.family: "Menlo"; font.pixelSize: 11
                    color: theme.text; selectionColor: theme.accent
                    Component.onCompleted: text = backend.logText()
                    onTextChanged: Qt.callLater(logFlick.toBottom)
                }
            }
            // Coalesce model growth into at most one text rebuild per interval; skip
            // while the user holds a selection so a live update can't wipe it.
            Timer {
                id: logRefresh; interval: 180
                onTriggered: if (logView.selectionStart === logView.selectionEnd)
                                 logView.text = backend.logText()
            }
            Connections {
                target: backend.logModel
                function onCountChanged() { if (!logRefresh.running) logRefresh.start() }
            }
        }
        RowLayout { Layout.fillWidth: true; spacing: 8
            Text { Layout.fillWidth: true; text: backend.logPath; font.pixelSize: 11
                   color: logPathMa.containsMouse ? theme.accent : theme.textDim
                   font.underline: logPathMa.containsMouse; elide: Text.ElideMiddle
                   MouseArea { id: logPathMa; anchors.fill: parent; hoverEnabled: true
                               cursorShape: Qt.PointingHandCursor; onClicked: backend.openLogFolder() } }
            Text { text: qsTr("Auto-scroll"); color: theme.textDim; font.pixelSize: 12 }
            Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: logFlick.autoScroll; implicitWidth: 34; implicitHeight: 20
                     onToggled: function(v){ logFlick.autoScroll = v } }
        }
    }
}
