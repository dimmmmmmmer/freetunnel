import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Qt.labs.platform as Platform
import "../components"

Item {
    id: cfgRoot
    required property var shell
    required property var backend
    required property var theme

    // Cmd/Ctrl+V tries to import a config from the clipboard.
    Shortcut { sequences: [StandardKey.Paste]; onActivated: backend.importFromClipboard() }
    // Header: Add (+) opens the import/create menu, Ping (speedometer).
    RowLayout {
        id: cfgHdr
        anchors.top: parent.top; anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter; spacing: 16
        Rectangle {
            Layout.preferredWidth: 40; Layout.preferredHeight: 32; radius: 8
            color: addMa.containsMouse ? theme.surface : theme.bg
            Behavior on color { ColorAnimation { duration: 120 } }
            // Drawn plus — strokes kept thin so its weight matches the
            // (stroked) speedometer icon instead of looking heavier/darker.
            Item { anchors.centerIn: parent; width: 22; height: 22
                Rectangle { anchors.centerIn: parent; width: 16; height: 1.8; radius: 1; antialiasing: true; color: theme.accent }
                Rectangle { anchors.centerIn: parent; width: 1.8; height: 16; radius: 1; antialiasing: true; color: theme.accent }
            }
            MouseArea { id: addMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: importMenu.open = !importMenu.open }
        }
        Rectangle {
            visible: backend.configs.length > 0
            Layout.preferredWidth: 40; Layout.preferredHeight: 32; radius: 8
            color: pingMa.containsMouse ? theme.surface : theme.bg
            Behavior on color { ColorAnimation { duration: 120 } }
            Icon { anchors.centerIn: parent; width: 22; height: 22; svg: "qrc:/icons/speedometer.svg"; color: cfgRoot.theme.accent; theme: cfgRoot.theme }
            MouseArea { id: pingMa; anchors.fill: parent; hoverEnabled: true; onClicked: backend.pingConfigs() }
        }
    }
    Text {
        visible: backend.configs.length === 0
        anchors.centerIn: parent
        text: qsTr("Add a config"); color: theme.textFaint; font.pixelSize: 15
        MouseArea { anchors.fill: parent; onClicked: importMenu.open = true }
    }
    ListView {
        id: cfgList
        anchors.top: cfgHdr.bottom; anchors.topMargin: 8; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 18; anchors.rightMargin: 18
        clip: true; spacing: 0
        model: backend.configs
        delegate: Item {
            required property int index
            required property string modelData
            width: cfgList.width; height: 56
            Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: 4; radius: 8
                // Stay highlighted as a single block even when the cursor is
                // over the ⋯/✕ buttons (their hover would otherwise drop it).
                color: (rowMa.containsMouse || dotsMa.containsMouse || delMa.containsMouse)
                       ? theme.surface : theme.bg
                Behavior on color { ColorAnimation { duration: 120 } } }
            MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: backend.selectConfig(index) }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6; spacing: 10
                Image { source: "qrc:/assets/logo.svg"; Layout.preferredWidth: 22; Layout.preferredHeight: 22
                        sourceSize: Qt.size(44,44); opacity: index === backend.activeIndex ? 1 : 0.4 }
                Text { text: modelData; color: theme.text; font.pixelSize: 14
                       Layout.fillWidth: true; elide: Text.ElideRight
                       font.weight: index === backend.activeIndex ? Font.Medium : Font.Normal }
                Text { visible: index < backend.pings.length && backend.pings[index] !== ""
                       text: index < backend.pings.length ? backend.pings[index] : ""
                       color: theme.textDim; font.pixelSize: 12 }
                Rectangle { visible: index === backend.activeIndex && backend.connected
                    radius: 10; color: theme.infoBg; implicitWidth: ab.width+16; implicitHeight: 20
                    Text { id: ab; anchors.centerIn: parent; text: qsTr("connected"); color: theme.success; font.pixelSize: 11; font.weight: Font.Medium } }
                Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                    Text { anchors.centerIn: parent; text: "⋯"; font.pixelSize: 20
                           color: dotsMa.containsMouse ? theme.text : theme.textDim }
                    MouseArea { id: dotsMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: { shell.editIndex = index; shell.overlay = "create" } } }
                Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                    Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 17
                           color: delMa.containsMouse ? theme.danger : theme.textDim }
                    MouseArea { id: delMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: shell.showConfirm(qsTr("Delete config “%1”?").arg(shell.elideMiddle(modelData, 36)),
                                    qsTr("Delete"), function(){ backend.removeConfig(index) }) } }
            }
        }
    }
    // Click-away backdrop + Esc to dismiss the import menu.
    MouseArea { anchors.fill: parent; z: 9; visible: importMenu.open
                onClicked: importMenu.open = false }
    Shortcut { sequence: "Escape"; enabled: importMenu.open; onActivated: importMenu.open = false }
    // Import / create dropdown (collapsed by default).
    Rectangle {
        id: importMenu; property bool open: false
        visible: opacity > 0; opacity: open ? 1 : 0; z: 10
        Behavior on opacity { NumberAnimation { duration: 130 } }
        transform: Translate { y: importMenu.open ? 0 : -8
                               Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } } }
        anchors.top: parent.top; anchors.topMargin: 44; anchors.horizontalCenter: parent.horizontalCenter
        width: 240; height: menuCol.height + 12; radius: 10; color: theme.bg
        border.color: theme.border; border.width: 1
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true; shadowColor: "#000000"
            shadowOpacity: theme.dark ? 0.5 : 0.2; shadowBlur: 0.7; shadowVerticalOffset: 5
        }
        Column { id: menuCol; width: parent.width - 10; x: 5; y: 6; spacing: 1
            component MenuRow: Rectangle {
                property alias text: mrLbl.text
                property alias hovered: mrMa.containsMouse
                signal triggered()
                width: parent.width; height: 40; radius: 6
                color: mrMa.containsMouse ? theme.surface : theme.bg
                Behavior on color { ColorAnimation { duration: 120 } }
                Text { id: mrLbl; anchors.verticalCenter: parent.verticalCenter; x: 9
                       color: theme.text; font.pixelSize: 14 }
                MouseArea { id: mrMa; anchors.fill: parent; hoverEnabled: true; onClicked: parent.triggered() }
            }
            MenuRow { text: qsTr("Paste from clipboard")
                onTriggered: { importMenu.open = false; backend.importFromClipboard() } }
            MenuRow { text: qsTr("From file…")
                onTriggered: { importMenu.open = false; fileDlg.open() } }
            Item { width: parent.width; height: 11
                Rectangle { anchors.centerIn: parent; width: parent.width - 16; height: 1; color: theme.border } }
            MenuRow { text: qsTr("Create new…")
                onTriggered: { importMenu.open = false; shell.editIndex = -1; shell.overlay = "create" } }
        }
    }
    Platform.FileDialog {
        id: fileDlg; title: qsTr("Select a config")
        nameFilters: ["TOML (*.toml)", qsTr("All files (*)")]
        onAccepted: backend.importFile(fileDlg.file.toString())
    }
}
