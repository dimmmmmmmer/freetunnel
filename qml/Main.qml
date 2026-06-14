import QtQuick
import QtQuick.Layouts

// FreeTunnel main window shell: centered top nav + page area.
// Consumes a `backend` context object (real app injects the VPN backend;
// the preview harness injects a mock).
Window {
    id: win
    visible: true
    width: 420
    height: 560
    color: theme.bg
    title: "FreeTunnel"

    // Flat light theme (accent = FreeTunnel blue). Dark mode handled later.
    QtObject {
        id: theme
        readonly property color bg: "#ffffff"
        readonly property color surface: "#f3f4f6"
        readonly property color text: "#1b1d21"
        readonly property color textDim: "#6b7280"
        readonly property color accent: "#185fa5"
        readonly property color border: "#e5e7eb"
        readonly property color ok: "#1d9e75"
    }

    property int currentPage: 0
    readonly property var navIcons: ["connection", "network", "configs", "settings", "log"]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- top nav (centered, icons only) ----
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 6
            spacing: 8
            Item { Layout.fillWidth: true }
            Repeater {
                model: win.navIcons
                Rectangle {
                    width: 46; height: 38; radius: 8
                    color: index === win.currentPage ? Qt.rgba(0.094, 0.373, 0.647, 0.12) : "transparent"
                    Image {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        source: "qrc:/assets/icons/" + modelData + ".svg"
                        sourceSize: Qt.size(44, 44)
                        opacity: index === win.currentPage ? 1.0 : 0.55
                    }
                    MouseArea { anchors.fill: parent; onClicked: win.currentPage = index }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ---- page area ----
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: win.currentPage === 0 ? homePage : stubPage
        }
    }

    // ---------------- Home (Подключение) ----------------
    Component {
        id: homePage
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width
                spacing: 14

                // Ring = connect/disconnect button; colour shows state.
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 200

                    Canvas {
                        id: ring
                        anchors.fill: parent
                        property bool on: backend.connected
                        onOnChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            var cx = width / 2, cy = height / 2, r = 86;
                            ctx.lineWidth = 9;
                            ctx.lineCap = "round";
                            ctx.strokeStyle = on ? "#185fa5" : "#d8dbe0";
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.stroke();
                        }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "qrc:/assets/logo.png"
                            width: 56; height: 56
                            sourceSize: Qt.size(112, 112)
                            opacity: backend.connected ? 1.0 : 0.4
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: backend.connected ? backend.sessionTime : "Выключено"
                            color: theme.text
                            font.pixelSize: backend.connected ? 22 : 15
                            font.weight: Font.Medium
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: backend.toggle() }
                }

                // Config selector (no frame) + chevron.
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6
                    Text { text: backend.activeConfig; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                    Text { text: "▾"; color: theme.textDim; font.pixelSize: 15 }
                }

                // Speed tiles (only when connected).
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    visible: backend.connected
                    Repeater {
                        model: [
                            { label: "Загрузка", value: backend.downSpeed, color: "#1d9e75" },
                            { label: "Отправка", value: backend.upSpeed, color: "#378add" }
                        ]
                        Rectangle {
                            width: 140; height: 56; radius: 8; color: theme.surface
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text { text: modelData.label; color: theme.textDim; font.pixelSize: 12 }
                                Text {
                                    text: modelData.value + " МБ/с"
                                    color: theme.text; font.pixelSize: 19; font.weight: Font.Medium
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // placeholder for the other tabs (built next)
    Component {
        id: stubPage
        Item {
            Text {
                anchors.centerIn: parent
                text: "Экран в разработке"
                color: theme.textDim
                font.pixelSize: 15
            }
        }
    }
}
