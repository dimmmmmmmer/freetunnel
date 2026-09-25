import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import "../components"

Item {
    id: homeRoot
    required property var shell
    required property var backend
    required property var theme

    // How far above the bottom the window's toast sits here. In the middle of the
    // gap between the config selector and the speed tiles when it fits there; a
    // longer one covers the tiles, whole, and not the selector, the one thing here
    // people click. Across the middle of the tiles, where it used to sit, it cut
    // their labels in half.
    function toastMargin(toastHeight) {
        var gapBottom = speedRow.anchors.bottomMargin + speedRow.height
        var gapTop = height - (homeCol.parent.y + homeCol.y + homeCol.height)
        var room = gapTop - gapBottom
        return toastHeight + 8 <= room ? gapBottom + (room - toastHeight) / 2
                                       : speedRow.anchors.bottomMargin
    }

    // The picker is placed and sized when it opens; a resize would leave it
    // hanging away from the selector it belongs to.
    onWidthChanged: cfgPopup.open = false
    onHeightChanged: cfgPopup.open = false

    // Speed badges pinned to the bottom — independent of the logo/config block.
    RowLayout {
        id: speedRow
        objectName: "speedTiles"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        spacing: 12
        Repeater {
            // Constant model — the live values are bound *inside* the delegate.
            // Putting backend.downSpeed/upSpeed in the model instead made the
            // model a fresh array on every tick (1 Hz), tearing both tiles down
            // and rebuilding them each second.
            model: [ { up: false, a: "↓" }, { up: true, a: "↑" } ]
            Rectangle {
                id: speedTile
                required property var modelData
                Layout.preferredWidth: 116; Layout.preferredHeight: 44; radius: 8; color: theme.tile
                Row {
                    anchors.centerIn: parent; spacing: 5
                    Text { anchors.verticalCenter: parent.verticalCenter
                           text: speedTile.modelData.a
                           color: speedTile.modelData.up ? theme.textDim : theme.success; font.pixelSize: 14 }
                    Text { anchors.verticalCenter: parent.verticalCenter
                           text: (speedTile.modelData.up ? backend.upSpeed : backend.downSpeed) + qsTr(" MB/s")
                           color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                }
            }
        }
    }
    // Logo + status + config sit above the badges; hero height follows content.
    Item {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: speedRow.top; anchors.bottomMargin: 22
        ColumnLayout {
            id: homeCol
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 6
            width: parent.width
            spacing: 0
            Item {
                id: hero
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 200
                Layout.preferredHeight: heroCol.height
                readonly property bool sessionActive: backend.connected || backend.connecting || backend.disconnecting
                Column {
                    id: heroCol
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6
                    Item {
                        width: 132; height: 132
                        Image {
                            id: heroLogo
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: hero.sessionActive ? 0 : 12
                            Behavior on y { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                            source: "qrc:/assets/logo.svg"; width: 132; height: 132
                            sourceSize: Qt.size(264, 264)
                            opacity: backend.connected ? 1.0 : ((backend.connecting || backend.disconnecting) ? 0.7 : 0.5)
                            Behavior on opacity { NumberAnimation { duration: 220 } }
                            scale: (heroMa.pressed ? 0.96 : 1.0) * ((backend.connecting || backend.disconnecting) ? pulse.value : 1.0)
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                            QtObject {
                                id: pulse; property real value: 1.0
                            }
                            SequentialAnimation {
                                running: backend.connecting || backend.disconnecting; loops: Animation.Infinite
                                NumberAnimation { target: pulse; property: "value"; to: 1.05; duration: 750; easing.type: Easing.InOutSine }
                                NumberAnimation { target: pulse; property: "value"; to: 0.97; duration: 750; easing.type: Easing.InOutSine }
                            }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: hero.sessionActive
                        opacity: hero.sessionActive ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                        text: backend.disconnecting ? qsTr("Disconnecting…")
                              : backend.connecting ? qsTr("Connecting…")
                              : (backend.connected ? backend.sessionTime : "")
                        color: theme.textDim
                        font.pixelSize: 15; font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: heroMa
                    objectName: "connectionLogo"
                    anchors.fill: parent
                    onClicked: backend.toggle()
                    // Handled, so the second click of a double-click is not a second
                    // toggle. People double-click anything that looks like an icon,
                    // and it connected and at once cancelled.
                    onDoubleClicked: {}
                }
            }
            Item { Layout.preferredHeight: 22 }
            Item {
                id: cfgSel
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: cfgRow.implicitWidth; implicitHeight: cfgRow.implicitHeight
                Row { id: cfgRow; spacing: 6
                    Item {
                        id: cfgLabelBlock
                        implicitHeight: 24
                        implicitWidth: cfgLabelRow.implicitWidth
                        // The ▾ follows the text as painted, so an elided name keeps it
                        // as close as a whole one does.
                        Item {
                            id: cfgLabelRow
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: Math.ceil(cfgLabel.contentWidth)
                                           + (cfgArrow.visible ? 6 + cfgArrow.implicitWidth : 0)
                            implicitHeight: cfgLabel.implicitHeight
                            Text {
                                id: cfgLabel
                                objectName: "activeConfigLabel"
                                property color labelColor: cfgSelMa.containsMouse ? theme.accent : theme.text
                                text: backend.configs.length > 0 ? backend.activeConfig : qsTr("Add a config")
                                // As much of the page as the selector's other parts leave,
                                // not a fixed 260 px that cut names the window had room for.
                                width: Math.min(implicitWidth,
                                                homeRoot.width - 36 - 6 - cfgArrow.implicitWidth - 6 - 22)
                                elide: Text.ElideRight
                                color: labelColor
                                font.underline: cfgSelMa.containsMouse
                                font.pixelSize: 15; font.weight: Font.Medium
                            }
                            Text { id: cfgArrow
                                   x: Math.ceil(cfgLabel.contentWidth) + 6
                                   anchors.verticalCenter: cfgLabel.verticalCenter
                                   visible: backend.configs.length > 0; text: "▾"
                                   color: cfgLabel.labelColor; font.pixelSize: 15 }
                        }
                        MouseArea { id: cfgSelMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (backend.configs.length === 0) { shell.currentPage = 1; return }
                                        if (!cfgPopup.open) {
                                            // As wide as the longest name needs, within the
                                            // page: a fixed 250 px left a broad empty band
                                            // around short names and still cut long ones.
                                            var widest = 0
                                            for (var i = 0; i < backend.configs.length; ++i) {
                                                pickerMetrics.text = backend.configs[i]
                                                widest = Math.max(widest, pickerMetrics.advanceWidth)
                                            }
                                            cfgPopup.width = Math.min(homeRoot.width - 16,
                                                                      Math.max(140, Math.ceil(widest) + 30))
                                            var p = cfgSel.mapToItem(homeRoot, 0, 0)
                                            cfgPopup.x = Math.max(8, Math.min(homeRoot.width - cfgPopup.width - 8,
                                                                              p.x + cfgSel.width / 2 - cfgPopup.width / 2))
                                            cfgPopup.y = p.y + cfgSel.height + 6
                                        }
                                        cfgPopup.open = !cfgPopup.open
                                    } }
                    }
                    Rectangle {
                        visible: backend.configs.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22; radius: 6
                        // Fade out to a transparent surface (same RGB, 0 alpha) so the
                        // hover animation doesn't flash dark by lerping from black.
                        color: addCfgMa.containsMouse ? theme.surface : Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0)
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Item { anchors.centerIn: parent; width: 14; height: 14
                            Rectangle { anchors.centerIn: parent; width: 10; height: 1.6; radius: 1; color: theme.accent }
                            Rectangle { anchors.centerIn: parent; width: 1.6; height: 10; radius: 1; color: theme.accent }
                        }
                        MouseArea { id: addCfgMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: shell.currentPage = 1 }
                    }
                }
            }
        }
    }
    // Click-away backdrop + Esc to dismiss the config picker.
    MouseArea { anchors.fill: parent; z: 90; visible: cfgPopup.open
                onClicked: cfgPopup.open = false }
    // Stand down while a window-level popup or confirm dialog is up: it owns
    // Escape then, and two enabled shortcuts on one key make Qt report the press
    // as ambiguous, so Escape would do nothing at all — including not closing the
    // dialog the user is actually looking at.
    Shortcut { sequence: "Escape"; enabled: cfgPopup.open && !shell.windowPopupOpen
               onActivated: cfgPopup.open = false }
    // Config names are single lines, so TextMetrics measures them right.
    TextMetrics { id: pickerMetrics; font.pixelSize: 14 }
    // Config picker dropdown, anchored under the selector.
    Rectangle {
        id: cfgPopup
        objectName: "configPicker"
        property bool open: false
        visible: opacity > 0; z: 100
        opacity: open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 130 } }
        transform: Translate { y: cfgPopup.open ? 0 : -8
                               Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } } }
        width: 140
        height: picker.height + 12
        radius: 10; color: theme.bg; border.color: theme.border; border.width: 1
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true; shadowColor: "#000000"
            shadowOpacity: theme.dark ? 0.5 : 0.2; shadowBlur: 0.7; shadowVerticalOffset: 5
        }
        Column {
            anchors.fill: parent; anchors.margins: 5
            ListView {
                id: picker; width: parent.width
                height: Math.min(contentHeight, 3 * 40); clip: true
                model: backend.configs
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: picker.width; height: 40; radius: 6
                    color: pma.containsMouse ? theme.surface : theme.bg
                    Text { anchors.verticalCenter: parent.verticalCenter; x: 10
                           width: parent.width - 20; elide: Text.ElideRight
                           text: parent.modelData
                           color: parent.index === backend.activeIndex ? theme.accent : theme.text
                           font.pixelSize: 14 }
                    MouseArea { id: pma; anchors.fill: parent; hoverEnabled: true
                                onClicked: { backend.selectConfig(parent.index); cfgPopup.open = false } }
                }
            }
        }
    }
}
