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
                anchors.centerIn: parent; visible: logFlick.count === 0
                text: qsTr("Logs will appear after connecting"); color: theme.textFaint; font.pixelSize: 13
            }
            // One delegate per line, virtualised: only the ~20 visible rows are
            // realised and new lines are inserted incrementally (LogModel), so a
            // log update costs O(visible) instead of re-parsing the whole 500-line
            // log as a single RichText/QTextDocument (which froze the UI during the
            // connect-time core-log storm) — and the scroll position is kept when
            // auto-scroll is off, since the model is never reset out from under it.
            ListView {
                id: logFlick
                anchors.fill: parent; anchors.margins: 12; clip: true
                property bool autoScroll: true
                model: backend.logModel
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 400
                function toBottom() { if (autoScroll) positionViewAtEnd() }
                onCountChanged: if (autoScroll) Qt.callLater(positionViewAtEnd)
                delegate: Row {
                    required property string time
                    required property string level
                    required property string msg
                    width: logFlick.width
                    spacing: 6
                    Text {
                        id: timeText
                        text: time
                        color: theme.textFaint; font.family: "Menlo"; font.pixelSize: 11
                    }
                    Text {
                        id: levelText
                        text: level
                        color: level === "ERROR" ? theme.danger
                             : level === "WARN" ? theme.warn
                             : level === "CORE" ? theme.accent : theme.textDim
                        font.family: "Menlo"; font.pixelSize: 11
                    }
                    Text {
                        width: logFlick.width - timeText.width - levelText.width - 12
                        text: msg
                        color: theme.text; font.family: "Menlo"; font.pixelSize: 11
                        wrapMode: Text.Wrap; textFormat: Text.PlainText
                    }
                }
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
