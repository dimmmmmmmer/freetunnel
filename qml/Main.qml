import QtQuick
import QtQuick.Layouts

// FreeTunnel main window: centered top nav + pages, plus back-arrow sub-screens.
// Consumes a `backend` context object (mock in preview; real app injects VPN).
Window {
    id: win
    visible: true
    width: 420
    height: 620
    color: theme.bg
    title: "FreeTunnel"

    QtObject {
        id: theme
        readonly property color bg: "#ffffff"
        readonly property color surface: "#f3f4f6"
        readonly property color text: "#1b1d21"
        readonly property color textDim: "#6b7280"
        readonly property color textFaint: "#9aa1ab"
        readonly property color accent: "#185fa5"
        readonly property color border: "#e7e9ee"
        readonly property color success: "#1d9e75"
        readonly property color warn: "#ba7517"
        readonly property color danger: "#a32d2d"
        readonly property color infoBg: Qt.rgba(0.094, 0.373, 0.647, 0.12)
    }

    property int currentPage: 0
    property string overlay: "" // "", "create", "adapters"
    readonly property var navIcons: ["connection", "network", "configs", "settings", "log"]

    // ---------- main content (nav + page) ----------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: win.overlay === ""

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 6
            spacing: 8
            Item { Layout.fillWidth: true }
            Repeater {
                model: win.navIcons
                Rectangle {
                    required property int index
                    required property string modelData
                    width: 46; height: 38; radius: 8
                    color: index === win.currentPage ? theme.infoBg : "transparent"
                    Image {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        source: "qrc:/icons/" + parent.modelData + ".svg"
                        sourceSize: Qt.size(44, 44)
                        opacity: parent.index === win.currentPage ? 1.0 : 0.5
                    }
                    MouseArea { anchors.fill: parent; onClicked: win.currentPage = parent.index }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: [homePage, splitPage, configsPage, settingsPage, logsPage][win.currentPage]
        }
    }

    // ---------- sub-screen overlay ----------
    Loader {
        anchors.fill: parent
        active: win.overlay !== ""
        sourceComponent: win.overlay === "create" ? createConfig
                       : win.overlay === "adapters" ? adaptersPage : null
    }

    // shared bits ------------------------------------------------------------
    component SectionLabel: Text {
        color: theme.textFaint; font.pixelSize: 12
        leftPadding: 2; bottomPadding: 4
    }
    component Sep: Rectangle { height: 1; color: theme.border; Layout.fillWidth: true }

    // ===================== Home (Подключение) =====================
    Component {
        id: homePage
        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width
                spacing: 14
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 200; Layout.preferredHeight: 200
                    Canvas {
                        anchors.fill: parent
                        property bool on: backend.connected
                        onOnChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d"); ctx.reset();
                            ctx.lineWidth = 9; ctx.lineCap = "round";
                            ctx.strokeStyle = on ? "#185fa5" : "#d8dbe0";
                            ctx.beginPath(); ctx.arc(width/2, height/2, 86, 0, 2*Math.PI); ctx.stroke();
                        }
                    }
                    Column {
                        anchors.centerIn: parent; spacing: 4
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "qrc:/assets/logo.png"; width: 56; height: 56
                            sourceSize: Qt.size(112, 112)
                            opacity: backend.connected ? 1.0 : 0.4
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: backend.connected ? backend.sessionTime : "Выключено"
                            color: theme.text
                            font.pixelSize: backend.connected ? 22 : 15; font.weight: Font.Medium
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: backend.toggle() }
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter; spacing: 6
                    Text { text: backend.activeConfig; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                    Text { text: "▾"; color: theme.textDim; font.pixelSize: 15 }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter; spacing: 12; visible: backend.connected
                    Repeater {
                        model: [ { l: "Загрузка", v: backend.downSpeed, c: theme.success, a: "↓" },
                                 { l: "Отправка", v: backend.upSpeed, c: "#378add", a: "↑" } ]
                        Rectangle {
                            required property var modelData
                            width: 140; height: 56; radius: 8; color: theme.surface
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 14
                                anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                Row { spacing: 5
                                    Text { text: parent.parent.parent.modelData.a; color: parent.parent.parent.modelData.c; font.pixelSize: 13 }
                                    Text { text: parent.parent.parent.modelData.l; color: theme.textDim; font.pixelSize: 12 }
                                }
                                Text { text: parent.parent.modelData.v + " МБ/с"; color: theme.text; font.pixelSize: 19; font.weight: Font.Medium }
                            }
                        }
                    }
                }
            }
        }
    }

    // ===================== Split tunnel =====================
    Component {
        id: splitPage
        Flickable {
            contentHeight: scol.height; clip: true
            ColumnLayout {
                id: scol; width: parent.width
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 18; anchors.rightMargin: 18
                spacing: 0
                RowLayout {
                    Layout.topMargin: 6; Layout.bottomMargin: 8; spacing: 8
                    Text { text: "Профиль"; color: theme.textFaint; font.pixelSize: 12 }
                    Text { text: "Default ▾"; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                    Item { Layout.fillWidth: true }
                    Text { text: "✎"; color: theme.textDim; font.pixelSize: 16 }
                    Text { text: "🗑"; color: theme.textDim; font.pixelSize: 15 }
                }
                RowLayout { Layout.fillWidth: true; height: 42
                    Text { text: "Включить"; color: theme.text; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    Toggle { checked: true }
                }
                Sep {}
                RowLayout { Layout.fillWidth: true; height: 42
                    Text { text: "Режим"; color: theme.text; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    Text { text: "Указанное — мимо VPN ▾"; color: theme.textDim; font.pixelSize: 14 }
                }
                Item { Layout.preferredHeight: 12 }
                RowLayout { Layout.fillWidth: true
                    SectionLabel { text: "Приложения" }
                    Item { Layout.fillWidth: true }
                    Text { text: "＋ Добавить"; color: theme.accent; font.pixelSize: 12 }
                }
                Repeater {
                    model: [ { n: "Google Chrome", on: true }, { n: "Telegram", on: false }, { n: "Steam", on: true } ]
                    ColumnLayout { required property var modelData; Layout.fillWidth: true
                        RowLayout { Layout.fillWidth: true; height: 40
                            Text { text: "▣"; color: theme.textDim; font.pixelSize: 16 }
                            Text { text: parent.parent.modelData.n; color: theme.text; font.pixelSize: 14; leftPadding: 8 }
                            Item { Layout.fillWidth: true }
                            Toggle { checked: parent.parent.modelData.on; implicitWidth: 34; implicitHeight: 20 }
                        }
                        Sep {}
                    }
                }
                Item { Layout.preferredHeight: 12 }
                RowLayout { Layout.fillWidth: true
                    SectionLabel { text: "Домены — мимо VPN" }
                    Item { Layout.fillWidth: true }
                    Text { text: "🧹 Очистить все"; color: theme.danger; font.pixelSize: 12 }
                }
                Flow {
                    Layout.fillWidth: true; Layout.topMargin: 4; spacing: 6
                    Repeater {
                        model: ["github.com", "*.gov.ru", "netflix.com"]
                        Rectangle {
                            required property string modelData
                            radius: 12; color: theme.surface
                            implicitWidth: chipRow.width + 20; implicitHeight: 26
                            Row { id: chipRow; anchors.centerIn: parent; spacing: 5
                                Text { text: parent.parent.modelData; color: theme.text; font.pixelSize: 13 }
                                Text { text: "×"; color: theme.textDim; font.pixelSize: 14 }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 8; height: 36; radius: 8
                    color: theme.bg; border.color: theme.border; border.width: 1
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: "добавить домен и Enter"; color: theme.textFaint; font.pixelSize: 13 }
                }
                Item { Layout.preferredHeight: 16 }
            }
        }
    }

    // ===================== Configs =====================
    Component {
        id: configsPage
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 0
            RowLayout {
                Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 8; Layout.bottomMargin: 8; spacing: 24
                Text { text: "⤓ Импорт ▾"; color: theme.accent; font.pixelSize: 15; font.weight: Font.Medium }
                Text { text: "＋ Создать"; color: theme.textDim; font.pixelSize: 15
                    MouseArea { anchors.fill: parent; onClicked: win.overlay = "create" } }
            }
            Repeater {
                model: [ { n: "Германия · Франкфурт", s: "frankfurt.example.com:443 · HTTP/2", a: true },
                         { n: "Нидерланды · Амстердам", s: "ams.example.com:443 · HTTP/3", a: false },
                         { n: "США · Нью-Йорк", s: "ny.example.com:443 · HTTP/2", a: false } ]
                ColumnLayout { required property var modelData; Layout.fillWidth: true
                    RowLayout { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; spacing: 12
                        Image { source: "qrc:/assets/logo.png"; width: 22; height: 22; sourceSize: Qt.size(44,44)
                                opacity: parent.parent.modelData.a ? 1 : 0.35 }
                        ColumnLayout { spacing: 1
                            Text { text: parent.parent.parent.modelData.n; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                            Text { text: parent.parent.parent.modelData.s; color: theme.textDim; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle { visible: parent.parent.modelData.a; radius: 10; color: Qt.rgba(0.11,0.62,0.46,0.16)
                            implicitWidth: ab.width+16; implicitHeight: 20
                            Text { id: ab; anchors.centerIn: parent; text: "активен"; color: theme.success; font.pixelSize: 11; font.weight: Font.Medium } }
                        Text { text: "⋮"; color: theme.textDim; font.pixelSize: 18; leftPadding: 6 }
                    }
                    Sep {}
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // ===================== Settings =====================
    Component {
        id: settingsPage
        Flickable {
            contentHeight: setcol.height; clip: true
            ColumnLayout {
                id: setcol; anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 0
                Item { Layout.preferredHeight: 6 }
                SectionLabel { text: "Основное" }
                RowLayout { Layout.fillWidth: true; height: 42; Text { text: "Язык"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Text { text: "English ▾"; color: theme.textDim; font.pixelSize: 14 } }
                Sep {}
                RowLayout { Layout.fillWidth: true; height: 42; Text { text: "Тема"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Text { text: "Система ▾"; color: theme.textDim; font.pixelSize: 14 } }
                Sep {}
                RowLayout { Layout.fillWidth: true; height: 42; Text { text: "Запускать при входе в систему"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Toggle { checked: true } }
                Sep {}
                RowLayout { Layout.fillWidth: true; height: 42; Text { text: "Подключаться автоматически"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Toggle { checked: false } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: "Безопасность" }
                RowLayout { Layout.fillWidth: true; height: 42
                    Text { text: "Kill switch"; color: theme.text; font.pixelSize: 14 }
                    Text { text: "блокировать трафик вне VPN"; color: theme.textFaint; font.pixelSize: 12; leftPadding: 6 }
                    Item { Layout.fillWidth: true } Toggle { checked: true } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: "Обслуживание" }
                RowLayout { Layout.fillWidth: true; height: 42
                    Text { text: "Сетевые адаптеры"; color: theme.text; font.pixelSize: 14 }
                    Text { text: "Windows"; color: theme.textFaint; font.pixelSize: 12; leftPadding: 6 }
                    Item { Layout.fillWidth: true } Text { text: "›"; color: theme.textDim; font.pixelSize: 16 }
                    MouseArea { anchors.fill: parent; onClicked: win.overlay = "adapters" } }
                Sep {}
                RowLayout { Layout.fillWidth: true; height: 42; Text { text: "Проверить обновления"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Text { text: "1.0.0"; color: theme.textFaint; font.pixelSize: 13 } }
                Item { Layout.preferredHeight: 14 }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                       text: "FreeTunnel 1.0.0 · ядро TrustTunnel"; color: theme.textFaint; font.pixelSize: 12 }
                Item { Layout.preferredHeight: 14 }
            }
        }
    }

    // ===================== Logs =====================
    Component {
        id: logsPage
        ColumnLayout {
            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 8
            RowLayout { Layout.fillWidth: true; Layout.topMargin: 6
                Item { Layout.fillWidth: true }
                Text { text: "Уровень: INFO ▾"; color: theme.textDim; font.pixelSize: 13; rightPadding: 12 }
                Text { text: "⧉"; color: theme.textDim; font.pixelSize: 16; rightPadding: 10 }
                Text { text: "🗑"; color: theme.textDim; font.pixelSize: 15 }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: theme.surface
                Column {
                    anchors.fill: parent; anchors.margins: 12; spacing: 3
                    Repeater {
                        model: [ { t: "12:04:32", l: "INFO", c: "#185fa5", m: "connecting frankfurt.example.com:443 (HTTP/2)" },
                                 { t: "12:04:33", l: "WARN", c: "#ba7517", m: "DNS upstream 1.1.1.1 slow, retrying" },
                                 { t: "12:04:34", l: "INFO", c: "#1d9e75", m: "tunnel established (utun5)" },
                                 { t: "12:04:34", l: "INFO", c: "#6b7280", m: "post-quantum key exchange: X25519MLKEM768" },
                                 { t: "12:06:11", l: "ERROR", c: "#a32d2d", m: "connection reset, reconnecting (1)" },
                                 { t: "12:06:13", l: "INFO", c: "#1d9e75", m: "tunnel re-established" } ]
                        Row { required property var modelData; spacing: 6
                            Text { text: parent.modelData.t; color: theme.textFaint; font.pixelSize: 11; font.family: "Menlo" }
                            Text { text: parent.modelData.l; color: parent.modelData.c; font.pixelSize: 11; font.family: "Menlo" }
                            Text { text: parent.modelData.m; color: theme.text; font.pixelSize: 11; font.family: "Menlo" }
                        }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true; Layout.bottomMargin: 12
                Text { text: "📂 ~/Library/Application Support/FreeTunnel/freetunnel.log"; color: theme.accent; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Text { text: "Авто-прокрутка"; color: theme.textDim; font.pixelSize: 12; rightPadding: 8 }
                Toggle { checked: true; implicitWidth: 34; implicitHeight: 20 }
            }
        }
    }

    // ===================== Create config (sub-screen) =====================
    Component {
        id: createConfig
        Rectangle {
            color: theme.bg
            RowLayout {
                id: chdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 14; spacing: 10
                Text { text: "←"; color: theme.textDim; font.pixelSize: 20
                       MouseArea { anchors.fill: parent; onClicked: win.overlay = "" } }
                Text { text: "Новый конфиг"; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
            }
            Flickable {
                anchors.top: chdr.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 18; anchors.rightMargin: 18; contentHeight: fcol.height; clip: true
                Column {
                    id: fcol; width: parent.width; spacing: 10
                    Repeater {
                        model: ["Имя", "Хост сервера", "Адрес(а) · host:port", "Логин", "Пароль",
                                "Протокол · HTTP/2 ▾", "DNS-серверы", "Custom SNI",
                                "Routing profile ▾", "Client random (hex)"]
                        Column { required property string modelData; width: parent.width; spacing: 4
                            Text { text: parent.modelData; color: theme.textDim; font.pixelSize: 13 }
                            Rectangle { width: parent.width; height: 34; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1 }
                        }
                    }
                    Row { width: parent.width; height: 30; spacing: 8
                        Text { text: "Allow IPv6 connections"; color: theme.text; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Item { width: parent.width - 240; height: 1 }
                        Toggle { checked: true; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Column { width: parent.width; spacing: 4
                        Row { width: parent.width
                            Text { text: "Сертификат (PEM)"; color: theme.textDim; font.pixelSize: 13 }
                            Item { width: parent.width - 230; height: 1 }
                            Text { text: "из файла"; color: theme.accent; font.pixelSize: 12; rightPadding: 12 }
                            Text { text: "вставить"; color: theme.accent; font.pixelSize: 12 }
                        }
                        Rectangle { width: parent.width; height: 60; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1 }
                    }
                    Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 10; topPadding: 4; bottomPadding: 16
                        Rectangle { width: 110; height: 36; radius: 8; color: theme.accent
                            Text { anchors.centerIn: parent; text: "Сохранить"; color: "white"; font.pixelSize: 14 } }
                        Rectangle { width: 90; height: 36; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1
                            Text { anchors.centerIn: parent; text: "Отмена"; color: theme.text; font.pixelSize: 14 } }
                    }
                }
            }
        }
    }

    // ===================== Adapters (sub-screen) =====================
    Component {
        id: adaptersPage
        Rectangle {
            color: theme.bg
            RowLayout {
                id: ahdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 14; spacing: 10
                Text { text: "←"; color: theme.textDim; font.pixelSize: 20
                       MouseArea { anchors.fill: parent; onClicked: win.overlay = "" } }
                Text { text: "Сетевые адаптеры"; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                Item { Layout.fillWidth: true }
                Text { text: "⟳ Сканировать"; color: theme.text; font.pixelSize: 13 }
            }
            ColumnLayout {
                anchors.top: ahdr.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 18; anchors.rightMargin: 18; anchors.topMargin: 4; spacing: 0
                Text { text: "Сторонние VPN-адаптеры могут конфликтовать с FreeTunnel."; color: theme.textFaint; font.pixelSize: 12; Layout.bottomMargin: 8 }
                Repeater {
                    model: [ { n: "FreeTunnel WinTUN", s: "Wintun Userspace Tunnel", ours: true, on: true },
                             { n: "Radmin VPN Network Adapter", s: "может конфликтовать · включён", ours: false, on: true },
                             { n: "TAP-Windows Adapter V9", s: "может конфликтовать · включён", ours: false, on: true },
                             { n: "OpenVPN Wintun", s: "отключён", ours: false, on: false } ]
                    ColumnLayout { required property var modelData; Layout.fillWidth: true
                        RowLayout { Layout.fillWidth: true; Layout.topMargin: 9; Layout.bottomMargin: 9; spacing: 12
                            Image { visible: parent.parent.modelData.ours; source: "qrc:/assets/logo.png"; width: 20; height: 20; sourceSize: Qt.size(40,40) }
                            Text { visible: !parent.parent.modelData.ours; text: parent.parent.modelData.on ? "⚠" : "○"; color: parent.parent.modelData.on ? theme.warn : theme.textFaint; font.pixelSize: 16 }
                            ColumnLayout { spacing: 1
                                Text { text: parent.parent.parent.modelData.n; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                                Text { text: parent.parent.parent.modelData.s; color: parent.parent.parent.modelData.ours ? theme.textDim : (parent.parent.parent.modelData.on ? theme.warn : theme.textFaint); font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle { visible: parent.parent.modelData.ours; radius: 10; color: theme.infoBg; implicitWidth: ow.width+16; implicitHeight: 20
                                Text { id: ow; anchors.centerIn: parent; text: "наш"; color: theme.accent; font.pixelSize: 11; font.weight: Font.Medium } }
                            Toggle { visible: !parent.parent.modelData.ours; checked: parent.parent.modelData.on }
                        }
                        Sep {}
                    }
                }
            }
        }
    }
}
