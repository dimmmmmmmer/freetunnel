import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
// See AppPickerOverlay.qml: Qt.labs.platform's FileDialog needs either a native
// helper or Qt Widgets, and on a desktop with neither it opens nothing at all.
// Choosing a config file was broken the same way, on the same platforms.
import QtQuick.Dialogs as Dialogs
import "../components"

Item {
    id: cfgRoot
    required property var shell
    required property var backend
    required property var theme

    // Which config the export menu / "Save .toml" dialog is acting on, and which
    // one the delete confirmation is about. Both dialogs live at window level and
    // outlive the row they were opened from — a background import (deep link from
    // a second instance, a paste) prepends to the list — so the action remembers
    // the config's file and finds its row when it runs. A remembered row number
    // exported the neighbouring config, password and all, or deleted it.
    property string exportPath: ""
    property string exportName: ""
    property string deletePath: ""

    // The row the remembered config is on now, or -1 with the user told why.
    function rowOf(path) {
        const i = backend.configIndex(path)
        if (i < 0)
            shell.showToast(qsTr("That configuration is no longer there."))
        return i
    }
    function exportPicked(v) {
        if (v === "toml") { tomlSaveDlg.open(); return }
        if (v !== "link") return
        const row = rowOf(cfgRoot.exportPath)
        if (row < 0) return
        var lnk = backend.configDeepLink(row)
        if (lnk && lnk.length > 0) {
            backend.copyToClipboard(lnk)
            shell.showToast(qsTr("Deep-link copied — it contains the password, share it carefully"))
        } else {
            shell.showToast(qsTr("Couldn’t build deep-link"))
        }
    }
    function exportToml(fileUrl) {
        const row = rowOf(cfgRoot.exportPath)
        if (row < 0) return
        shell.showToast(backend.exportConfigToml(row, fileUrl)
                        ? qsTr("Config exported — the file contains the password") : qsTr("Export failed"))
    }
    function deleteConfirmed() {
        const row = rowOf(cfgRoot.deletePath)
        if (row >= 0)
            backend.removeConfig(row)
    }
    // A config name is user- or import-controlled: a "/" (or "\" on Windows)
    // would be read as a path separator by the save dialog, so flatten it.
    function exportFileName(name) {
        var s = name.replace(/[\/\\]/g, "-")
        return s.length > 0 ? s : "config"
    }

    // Home's + and "Add a config" come here for the add menu. The flag is cleared
    // on every load, so it can never open the menu on a later visit.
    Component.onCompleted: if (shell.openAddMenu) { shell.openAddMenu = false; importMenu.open = true }

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
            MouseArea { id: pingMa; objectName: "pingButton"; anchors.fill: parent; hoverEnabled: true; onClicked: backend.pingConfigs() }
        }
    }
    Text {
        objectName: "addConfigHint"
        // Hidden under its own menu, which at the default height cut it in half.
        visible: backend.configs.length === 0 && !importMenu.open
        // Above the (empty) list, which fills the same area and, being a
        // Flickable, took the click itself.
        z: 1
        anchors.centerIn: parent
        // A link, like the same words on Home: in placeholder grey, with nothing
        // on hover, it read as a hint and not as something to click.
        text: qsTr("Add a config"); font.pixelSize: 15; font.weight: Font.Medium
        color: hintMa.containsMouse ? theme.accent : theme.text; font.underline: hintMa.containsMouse
        MouseArea { id: hintMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: importMenu.open = true }
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
        // Clear it from one place: the dragged delegate normally does this on
        // release/cancel, but a model reset destroys that delegate with the
        // button still held (see the Connections below).
        function endDrag() { dragFrom = -1; dragTo = -1; interactive = true }
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
                        cfgList.endDrag()
                        cfgDelegate.dragY = 0
                        if (to !== from) backend.moveConfig(from, to)
                    } else if (!moved) {
                        backend.selectConfig(index)
                    }
                }
                onCanceled: {
                    if (cfgList.dragFrom === index)
                        cfgList.endDrag()
                    cfgDelegate.dragY = 0; moved = false
                }
            }
            // The name takes what is left, so the rest keeps to what it needs: the
            // three icons share one group of 26 px cells, not 30 px cells with the
            // row's gap between each. At the default width the connected config,
            // with its ping and badge, was left about 60 px for its name.
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6; spacing: 8
                Image { source: "qrc:/assets/logo.svg"; Layout.preferredWidth: 22; Layout.preferredHeight: 22
                        sourceSize: Qt.size(44,44); opacity: index === backend.activeIndex ? 1 : 0.4 }
                Text { objectName: "configName"; text: modelData; color: theme.text; font.pixelSize: 14
                       Layout.fillWidth: true; elide: Text.ElideRight
                       font.weight: index === backend.activeIndex ? Font.Medium : Font.Normal }
                Text { visible: index < backend.pings.length && backend.pings[index] !== ""
                       text: index < backend.pings.length ? backend.pings[index] : ""
                       color: theme.textDim; font.pixelSize: 12 }
                // On the active config while it is up or on its way up; gone the
                // moment teardown starts (disconnecting), so it never lingers on a
                // config whose tunnel is going down. A connect, a switch to another
                // config (m_reapplying keeps disconnecting false and reports
                // connecting) and a reconnect say "connecting…" here, as Home and
                // the tray do: the badge used to just vanish until the tunnel was up.
                // Worded by connected, so if both flags are ever true it says up.
                Rectangle { objectName: "connectionBadge"
                    visible: index === backend.activeIndex && (backend.connected || backend.connecting) && !backend.disconnecting
                    radius: 10; color: theme.infoBg; implicitWidth: ab.width+16; implicitHeight: 20
                    Text { id: ab; anchors.centerIn: parent
                           text: backend.connected ? qsTr("connected") : qsTr("connecting…")
                           color: backend.connected ? theme.success : theme.textDim; font.pixelSize: 11; font.weight: Font.Medium } }
                Row { Layout.fillHeight: true
                    Item { width: 26; height: parent.height
                        Icon { anchors.centerIn: parent; width: 17; height: 17; svg: "qrc:/icons/export.svg"
                               color: expMa.containsMouse ? cfgRoot.theme.text : cfgRoot.theme.textDim; theme: cfgRoot.theme }
                        MouseArea { id: expMa; anchors.fill: parent; hoverEnabled: true
                            onClicked: {
                                cfgRoot.exportPath = backend.configPath(index); cfgRoot.exportName = modelData
                                shell.showSelect(parent,
                                    [{v: "toml", t: qsTr("Export .toml…")}, {v: "link", t: qsTr("Copy deep-link")}], "",
                                    cfgRoot.exportPicked)
                            } } }
                    // A pencil: it opens the editor. It was ⋯, which promises a menu,
                    // while the menu was the export icon's beside it.
                    Item { width: 26; height: parent.height
                        Icon { objectName: "editConfigIcon"; anchors.centerIn: parent; width: 16; height: 16; svg: "qrc:/icons/edit.svg"
                               color: dotsMa.containsMouse ? theme.text : theme.textDim; theme: cfgRoot.theme }
                        MouseArea { id: dotsMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { shell.editIndex = index; shell.overlay = "create" } } }
                    Item { width: 26; height: parent.height
                        Icon { anchors.centerIn: parent; width: 16; height: 16; svg: "qrc:/icons/close.svg"
                               color: delMa.containsMouse ? theme.danger : theme.textDim; theme: cfgRoot.theme }
                        MouseArea { id: delMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: {
                                        cfgRoot.deletePath = backend.configPath(index)
                                        shell.showConfirm(qsTr("Delete config “%1”?").arg(shell.elideMiddle(modelData, 36)),
                                                          qsTr("Delete"), cfgRoot.deleteConfirmed)
                                    } } }
                }
            }
        }
    }
    // A model reset (a background import from a second instance) destroys the
    // delegate that owns an in-flight drag while its button is still held, so
    // that row's onReleased/onCanceled never fire — without this the list would
    // stay non-interactive with dragFrom set, breaking scrolling and every later
    // reorder until the page is reloaded.
    Connections {
        target: backend
        function onConfigsChanged() { cfgList.endDrag() }
    }
    // Click-away backdrop + Esc to dismiss the import menu. It takes hover too, so
    // what it covers does not light up for a click that only closes the menu. A
    // double-click is taken whole (here and by the rows): Home's logo, + and "Add a
    // config" open this menu on their first click, and the second landed on it,
    // closing it at once or running whichever row was under the pointer.
    MouseArea { anchors.fill: parent; z: 9; visible: importMenu.open; hoverEnabled: true
                onClicked: importMenu.open = false; onDoubleClicked: {} }
    // Same standing-down rule as everywhere else: a confirm dialog opened over
    // this menu (a deep link can do that unprompted) owns Escape, and leaving both
    // enabled makes Qt call the press ambiguous and run neither.
    Shortcut { sequence: "Escape"; enabled: importMenu.open && !shell.windowPopupOpen
               onActivated: importMenu.open = false }
    // Import / create dropdown (collapsed by default).
    Rectangle {
        id: importMenu; objectName: "importMenu"; property bool open: false
        visible: opacity > 0; opacity: open ? 1 : 0; z: 10
        Behavior on opacity { NumberAnimation { duration: 130 } }
        transform: Translate { y: importMenu.open ? 0 : -8
                               Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } } }
        anchors.top: parent.top; anchors.topMargin: 44; anchors.horizontalCenter: parent.horizontalCenter
        // As wide as its widest item needs, not a fixed 240 px that left a broad
        // empty band beside three short labels. The window's own select popup
        // sizes itself the same way.
        width: Math.min(cfgRoot.width - 16,
                        Math.max(140, Math.ceil(Math.max(mrPaste.labelWidth, mrFile.labelWidth,
                                                         mrCreate.labelWidth)) + 40))
        height: menuCol.height + 12; radius: 10; color: theme.bg
        border.color: theme.border; border.width: 1
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true; shadowColor: "#000000"
            shadowOpacity: theme.dark ? 0.5 : 0.2; shadowBlur: 0.7; shadowVerticalOffset: 5
        }
        Column { id: menuCol; width: parent.width - 10; x: 5; y: 6; spacing: 1
            component MenuRow: Rectangle {
                property alias text: mrLbl.text
                readonly property real labelWidth: mrLbl.implicitWidth
                property alias hovered: mrMa.containsMouse
                signal triggered()
                width: parent.width; height: 40; radius: 6
                color: mrMa.containsMouse ? theme.surface : theme.bg
                Behavior on color { ColorAnimation { duration: 120 } }
                Text { id: mrLbl; anchors.verticalCenter: parent.verticalCenter; x: 9
                       width: parent.width - 18; elide: Text.ElideRight
                       color: theme.text; font.pixelSize: 14 }
                MouseArea { id: mrMa; anchors.fill: parent; hoverEnabled: true; onClicked: parent.triggered()
                            onDoubleClicked: {} }
            }
            MenuRow { id: mrPaste; text: qsTr("Paste from clipboard")
                onTriggered: { importMenu.open = false; backend.importFromClipboard() } }
            MenuRow { id: mrFile; text: qsTr("From file…")
                onTriggered: { importMenu.open = false; fileDlg.open() } }
            Item { width: parent.width; height: 11
                Rectangle { anchors.centerIn: parent; width: parent.width - 16; height: 1; color: theme.border } }
            MenuRow { id: mrCreate; text: qsTr("Create new…")
                onTriggered: { importMenu.open = false; shell.editIndex = -1; shell.overlay = "create" } }
        }
    }
    Dialogs.FileDialog {
        id: fileDlg; objectName: "configImportDialog"; title: qsTr("Select a config")
        nameFilters: ["TOML (*.toml)", qsTr("All files (*)")]
        onAccepted: backend.importFile(fileDlg.selectedFile.toString())
    }
    Dialogs.FileDialog {
        id: tomlSaveDlg; objectName: "configExportDialog"; title: qsTr("Export config")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["TOML (*.toml)"]; defaultSuffix: "toml"
        // Encoded: a name may hold '#' or '%' now, and in a URL '#' starts the
        // fragment, so "Work #2" was offered as "Work .toml".
        selectedFile: "file:" + encodeURIComponent(cfgRoot.exportFileName(cfgRoot.exportName) + ".toml")
        onAccepted: cfgRoot.exportToml(tomlSaveDlg.selectedFile.toString())
    }
}
