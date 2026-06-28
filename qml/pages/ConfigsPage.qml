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

    // Which config the export "Save .toml" dialog is acting on.
    property int exportIndex: -1
    property string exportName: ""

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
        // Active drag-reorder state, shared across delegates.
        property int dragFrom: -1
        property int dragTo: -1
        delegate: Item {
            id: cfgDelegate
            required property int index
            required property string modelData
            width: cfgList.width; height: 56

            // Drag-to-reorder by the row body: the dragged row follows the cursor
            // while every other row slides to open a gap at the drop slot, so it's
            // obvious where it will land. moveConfig() persists the order on drop.
            readonly property bool dragging: cfgList.dragFrom === index
            property real dragY: 0
            readonly property real slotShift: {
                if (cfgList.dragFrom < 0 || dragging) return 0
                if (cfgList.dragFrom < cfgList.dragTo
                        && index > cfgList.dragFrom && index <= cfgList.dragTo) return -height
                if (cfgList.dragFrom > cfgList.dragTo
                        && index < cfgList.dragFrom && index >= cfgList.dragTo) return height
                return 0
            }
            z: dragging ? 2 : 1
            transform: Translate {
                y: cfgDelegate.dragging ? cfgDelegate.dragY : cfgDelegate.slotShift
                // The dragged row tracks the cursor instantly; the rest animate
                // the gap open and closed.
                Behavior on y { enabled: !cfgDelegate.dragging
                                NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
            }
            Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: 4; radius: 8
                // Stay highlighted as a single block even over the ⋯/✕ buttons.
                color: (cfgDelegate.dragging || rowMa.containsMouse || expMa.containsMouse
                        || dotsMa.containsMouse || delMa.containsMouse)
                       ? theme.surface : theme.bg
                Behavior on color { ColorAnimation { duration: 120 } } }
            // Whole-row handler: a plain click selects, a vertical drag reorders.
            MouseArea {
                id: rowMa; anchors.fill: parent; hoverEnabled: true; preventStealing: true
                property real pressInList: 0
                property bool moved: false
                onPressed: (mouse) => { pressInList = mapToItem(cfgList, 0, mouse.y).y
                                        moved = false; cfgDelegate.dragY = 0 }
                onPositionChanged: (mouse) => {
                    // hoverEnabled fires this on plain hover too — only react while
                    // the button is actually held, or rows drift around on hover.
                    if (!(mouse.buttons & Qt.LeftButton)) return
                    var dy = mapToItem(cfgList, 0, mouse.y).y - pressInList
                    if (!moved && Math.abs(dy) < 6) return
                    moved = true
                    if (cfgList.dragFrom < 0) {
                        cfgList.dragFrom = index; cfgList.dragTo = index; cfgList.interactive = false
                    }
                    cfgDelegate.dragY = dy
                    cfgList.dragTo = Math.max(0, Math.min(backend.configs.length - 1,
                                                          index + Math.round(dy / cfgDelegate.height)))
                }
                onReleased: {
                    if (cfgList.dragFrom === index) {
                        var from = cfgList.dragFrom, to = cfgList.dragTo
                        cfgList.dragFrom = -1; cfgList.dragTo = -1; cfgList.interactive = true
                        cfgDelegate.dragY = 0
                        if (to !== from) backend.moveConfig(from, to)
                    } else if (!moved) {
                        backend.selectConfig(index)
                    }
                }
                onCanceled: {
                    if (cfgList.dragFrom === index) {
                        cfgList.dragFrom = -1; cfgList.dragTo = -1; cfgList.interactive = true
                    }
                    cfgDelegate.dragY = 0; moved = false
                }
            }
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
                    Icon { anchors.centerIn: parent; width: 17; height: 17; svg: "qrc:/icons/export.svg"
                           color: expMa.containsMouse ? cfgRoot.theme.text : cfgRoot.theme.textDim; theme: cfgRoot.theme }
                    MouseArea { id: expMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: shell.showSelect(parent,
                            [{v: "toml", t: qsTr("Export .toml…")}, {v: "link", t: qsTr("Copy deep-link")}], "",
                            function(v) {
                                if (v === "toml") { cfgRoot.exportIndex = index; cfgRoot.exportName = modelData; tomlSaveDlg.open() }
                                else if (v === "link") {
                                    var lnk = backend.configDeepLink(index)
                                    if (lnk && lnk.length > 0) { backend.copyToClipboard(lnk); shell.showToast(qsTr("Deep-link copied — it contains the password, share it carefully")) }
                                    else shell.showToast(qsTr("Couldn’t build deep-link"))
                                }
                            }) } }
                Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                    Icon { anchors.centerIn: parent; width: 18; height: 18; svg: "qrc:/icons/more.svg"
                           color: dotsMa.containsMouse ? theme.text : theme.textDim; theme: cfgRoot.theme }
                    MouseArea { id: dotsMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: { shell.editIndex = index; shell.overlay = "create" } } }
                Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                    Icon { anchors.centerIn: parent; width: 16; height: 16; svg: "qrc:/icons/close.svg"
                           color: delMa.containsMouse ? theme.danger : theme.textDim; theme: cfgRoot.theme }
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
    Platform.FileDialog {
        id: tomlSaveDlg; title: qsTr("Export config")
        fileMode: Platform.FileDialog.SaveFile
        nameFilters: ["TOML (*.toml)"]; defaultSuffix: "toml"
        currentFile: "file:" + (cfgRoot.exportName.length ? cfgRoot.exportName : "config") + ".toml"
        onAccepted: shell.showToast(backend.exportConfigToml(cfgRoot.exportIndex, tomlSaveDlg.file.toString())
                                    ? qsTr("Config exported — the file contains the password") : qsTr("Export failed"))
    }
}
