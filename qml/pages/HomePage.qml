import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import "../components"

Item {
    id: homeRoot
    required property var shell
    required property var backend
    required property var theme

    // Speed badges pinned to the bottom — independent of the logo/config block.
    RowLayout {
        id: speedRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        spacing: 12
        Repeater {
            model: [ { v: backend.downSpeed, c: theme.success, a: "↓" },
                     { v: backend.upSpeed, c: theme.textDim, a: "↑" } ]
            Rectangle {
                required property var modelData
                Layout.preferredWidth: 116; Layout.preferredHeight: 44; radius: 8; color: theme.tile
                Row {
                    anchors.centerIn: parent; spacing: 5
                    Text { anchors.verticalCenter: parent.verticalCenter
                           text: parent.parent.modelData.a; color: parent.parent.modelData.c; font.pixelSize: 14 }
                    Text { anchors.verticalCenter: parent.verticalCenter
                           text: parent.parent.modelData.v + qsTr(" MB/s"); color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
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
                            // Tint the mark theme.success while connected — same green
                            // as the "connected" badge and the tray icon.
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                colorization: backend.connected ? 1.0 : 0.0
                                colorizationColor: theme.success
                                Behavior on colorization { NumberAnimation { duration: 220 } }
                            }
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
                MouseArea { id: heroMa; anchors.fill: parent; onClicked: backend.toggle() }
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
                        Row {
                            id: cfgLabelRow; spacing: 6
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                id: cfgLabel
                                property color labelColor: cfgSelMa.containsMouse ? theme.accent : theme.text
                                text: backend.configs.length > 0 ? backend.activeConfig : qsTr("Add a config")
                                width: backend.configs.length > 0 ? Math.min(implicitWidth, 260) : implicitWidth
                                elide: Text.ElideRight
                                color: labelColor
                                font.underline: cfgSelMa.containsMouse
                                font.pixelSize: 15; font.weight: Font.Medium
                            }
                            Text { id: cfgArrow
                                   visible: backend.configs.length > 0; text: "▾"
                                   color: cfgLabel.labelColor; font.pixelSize: 15 }
                        }
                        MouseArea { id: cfgSelMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (backend.configs.length === 0) { shell.currentPage = 1; return }
                                        if (!cfgPopup.open) {
                                            var p = cfgSel.mapToItem(homeRoot, 0, 0)
                                            cfgPopup.x = p.x + cfgSel.width / 2 - cfgPopup.width / 2
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
    Shortcut { sequence: "Escape"; enabled: cfgPopup.open; onActivated: cfgPopup.open = false }
    // Config picker dropdown, anchored under the selector.
    Rectangle {
        id: cfgPopup
        property bool open: false
        visible: opacity > 0; z: 100
        opacity: open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 130 } }
        transform: Translate { y: cfgPopup.open ? 0 : -8
                               Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } } }
        width: 250
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
