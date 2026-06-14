import QtQuick
import QtQuick.Layouts
import Qt.labs.platform as Platform

// FreeTunnel main window: centered top nav + pages, plus back-arrow sub-screens.
// Consumes a `backend` context object (mock in preview; real app injects VPN).
Window {
    id: win
    visible: true
    width: 420
    height: 620
    color: theme.bg
    title: "FreeTunnel"

    // Closing the window hides to tray instead of quitting (quitOnLastWindowClosed
    // is off in main.cpp). Quit explicitly from the tray menu.
    onClosing: function(close) { close.accepted = false; win.hide() }

    // ---------- system tray ----------
    Platform.SystemTrayIcon {
        id: tray
        visible: true
        icon.source: "qrc:/assets/logo.png"
        tooltip: backend.connected ? "FreeTunnel — connected" : "FreeTunnel"
        onActivated: function(reason) {
            if (reason === Platform.SystemTrayIcon.Trigger
                    || reason === Platform.SystemTrayIcon.DoubleClick) {
                if (win.visible) {
                    win.hide()
                } else {
                    win.show(); win.raise(); win.requestActivate()
                }
            }
        }
        menu: Platform.Menu {
            Platform.MenuItem {
                text: backend.connected ? qsTr("Disconnect") : qsTr("Connect")
                onTriggered: backend.toggle()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: qsTr("Show FreeTunnel")
                onTriggered: { win.show(); win.raise(); win.requestActivate() }
            }
            Platform.MenuItem {
                text: qsTr("Quit")
                onTriggered: Qt.quit()
            }
        }
    }

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

    // Map a Qt key code to a portable QKeySequence name (used by HotkeyField).
    function keyName(key, text) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z) return String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9) return String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F12) return "F" + (key - Qt.Key_F1 + 1)
        var m = {}
        m[Qt.Key_Space] = "Space"; m[Qt.Key_Tab] = "Tab"; m[Qt.Key_Return] = "Return"
        m[Qt.Key_Enter] = "Enter"; m[Qt.Key_Home] = "Home"; m[Qt.Key_End] = "End"
        m[Qt.Key_Insert] = "Ins"; m[Qt.Key_PageUp] = "PgUp"; m[Qt.Key_PageDown] = "PgDown"
        m[Qt.Key_Up] = "Up"; m[Qt.Key_Down] = "Down"; m[Qt.Key_Left] = "Left"; m[Qt.Key_Right] = "Right"
        if (m[key] !== undefined) return m[key]
        if (text && text.length === 1 && text.charCodeAt(0) >= 33) return text.toUpperCase()
        return ""
    }

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
        leftPadding: 0; bottomPadding: 4
    }
    component Sep: Rectangle { Layout.preferredHeight: 1; color: theme.border; Layout.fillWidth: true }

    // Click to record a global hotkey chord; emits a portable sequence string
    // ("Ctrl+Alt+T"). Backspace clears, Esc cancels.
    component HotkeyField: Item {
        id: hk
        property string label: ""
        property string value: ""
        signal captured(string seq)
        Layout.fillWidth: true
        Layout.preferredHeight: 42
        property bool capturing: false
        RowLayout {
            anchors.fill: parent
            Text { text: hk.label; color: theme.text; font.pixelSize: 14 }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredHeight: 28
                Layout.preferredWidth: Math.max(96, lbl.implicitWidth + 24)
                radius: 6
                color: hk.capturing ? theme.infoBg : theme.surface
                border.width: hk.capturing ? 1 : 0; border.color: theme.accent
                Text {
                    id: lbl; anchors.centerIn: parent
                    text: hk.capturing ? "Нажмите…" : (hk.value || "—")
                    color: (hk.value || hk.capturing) ? theme.text : theme.textFaint
                    font.pixelSize: 13
                }
                MouseArea { anchors.fill: parent; onClicked: { hk.capturing = true; hk.forceActiveFocus() } }
            }
        }
        Keys.onPressed: function(e) {
            if (!hk.capturing) return
            e.accepted = true
            if (e.key === Qt.Key_Escape) { hk.capturing = false; return }
            if (e.key === Qt.Key_Backspace || e.key === Qt.Key_Delete) {
                hk.capturing = false; hk.captured(""); return
            }
            if (e.key === Qt.Key_Control || e.key === Qt.Key_Shift
                    || e.key === Qt.Key_Alt || e.key === Qt.Key_Meta) return
            var parts = []
            if (e.modifiers & Qt.ControlModifier) parts.push("Ctrl")
            if (e.modifiers & Qt.AltModifier) parts.push("Alt")
            if (e.modifiers & Qt.ShiftModifier) parts.push("Shift")
            if (e.modifiers & Qt.MetaModifier) parts.push("Meta")
            var kn = win.keyName(e.key, e.text)
            if (kn === "") return
            parts.push(kn)
            hk.capturing = false
            hk.captured(parts.join("+"))
        }
    }

    // ===================== Home (Подключение) =====================
    Component {
        id: homePage
        Item {
            ColumnLayout {
                anchors.top: parent.top; anchors.topMargin: 40
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                spacing: 18
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
                    MouseArea { anchors.fill: parent; onClicked: win.currentPage = 2 } // open Configs
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
                                anchors.centerIn: parent; spacing: 2
                                Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 5
                                    Text { text: parent.parent.parent.modelData.a; color: parent.parent.parent.modelData.c; font.pixelSize: 13 }
                                    Text { text: parent.parent.parent.modelData.l; color: theme.textDim; font.pixelSize: 12 }
                                }
                                Text { anchors.horizontalCenter: parent.horizontalCenter
                                       text: parent.parent.modelData.v + " МБ/с"; color: theme.text; font.pixelSize: 19; font.weight: Font.Medium }
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
                Item { Layout.preferredHeight: 10 }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap
                       text: "Раздельное туннелирование: домены из списка ниже идут напрямую, мимо VPN."
                       color: theme.textFaint; font.pixelSize: 12 }
                Item { Layout.preferredHeight: 4 }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Включить"; color: theme.text; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    Toggle { checked: backend.splitEnabled; onToggled: function(v){ backend.splitEnabled = v } }
                }
                Item { Layout.preferredHeight: 10 }
                SectionLabel { text: "Профиль" }
                Flow {
                    Layout.fillWidth: true; Layout.topMargin: 2; spacing: 6
                    Repeater {
                        model: backend.profiles
                        Rectangle {
                            required property string modelData
                            radius: 13; height: 28; implicitWidth: prn.width + 24
                            color: modelData === backend.activeProfile ? theme.accent : theme.surface
                            Text { id: prn; anchors.centerIn: parent; text: parent.modelData
                                   color: parent.modelData === backend.activeProfile ? "white" : theme.text; font.pixelSize: 13 }
                            MouseArea { anchors.fill: parent; onClicked: backend.selectProfile(parent.modelData) }
                        }
                    }
                    Rectangle {
                        radius: 13; height: 28; implicitWidth: 34; color: theme.surface
                        border.color: theme.border; border.width: 1
                        Text { anchors.centerIn: parent; text: "+"; color: theme.accent; font.pixelSize: 17 }
                        MouseArea { anchors.fill: parent; onClicked: { npRow.visible = true; npInput.forceActiveFocus() } }
                    }
                }
                Rectangle {
                    id: npRow; visible: false
                    Layout.fillWidth: true; Layout.topMargin: 6; Layout.preferredHeight: 36; radius: 8
                    color: theme.bg; border.color: theme.accent; border.width: 1
                    TextInput {
                        id: npInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter; clip: true; font.pixelSize: 13; color: theme.text
                        onAccepted: { backend.addProfile(text); text = ""; npRow.visible = false }
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: "имя профиля и Enter"; color: theme.textFaint; font.pixelSize: 13
                           visible: npInput.text.length === 0 }
                }
                RowLayout { Layout.fillWidth: true; Layout.topMargin: 6; visible: backend.activeProfile !== "Default"
                    Item { Layout.fillWidth: true }
                    Text { text: "Удалить профиль «" + backend.activeProfile + "»"; color: theme.danger; font.pixelSize: 12
                        MouseArea { anchors.fill: parent; onClicked: backend.removeProfile(backend.activeProfile) } }
                }
                Item { Layout.preferredHeight: 12 }
                RowLayout { Layout.fillWidth: true
                    SectionLabel { text: "Домены — мимо VPN" }
                    Item { Layout.fillWidth: true }
                    Text { text: "Очистить все"; color: theme.danger; font.pixelSize: 12; visible: backend.domains.length > 0
                        MouseArea { anchors.fill: parent; onClicked: backend.clearDomains() } }
                }
                Flow {
                    Layout.fillWidth: true; Layout.topMargin: 4; spacing: 6
                    Repeater {
                        model: backend.domains
                        Rectangle {
                            required property string modelData
                            required property int index
                            radius: 12; color: theme.surface
                            implicitWidth: chipRow.width + 20; implicitHeight: 26
                            Row { id: chipRow; anchors.centerIn: parent; spacing: 5
                                Text { text: parent.parent.modelData; color: theme.text; font.pixelSize: 13 }
                                Text { text: "×"; color: theme.textDim; font.pixelSize: 14
                                    MouseArea { anchors.fill: parent; onClicked: backend.removeDomain(parent.parent.parent.index) } }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 8; Layout.preferredHeight: 36; radius: 8
                    color: theme.bg; border.color: domInput.activeFocus ? theme.accent : theme.border; border.width: 1
                    TextInput {
                        id: domInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter; clip: true
                        font.pixelSize: 13; color: theme.text
                        onAccepted: { backend.addDomain(text); text = "" }
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: "добавить домен и Enter"; color: theme.textFaint; font.pixelSize: 13
                           visible: domInput.text.length === 0 && !domInput.activeFocus }
                }
                Item { Layout.preferredHeight: 16 }
            }
        }
    }

    // ===================== Configs =====================
    Component {
        id: configsPage
        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 0
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 8; Layout.bottomMargin: 8; spacing: 24
                    Text { text: "Импорт ▾"; color: theme.accent; font.pixelSize: 15; font.weight: Font.Medium
                        MouseArea { anchors.fill: parent; onClicked: importMenu.visible = !importMenu.visible } }
                    Text { text: "Создать"; color: theme.text; font.pixelSize: 15
                        MouseArea { anchors.fill: parent; onClicked: win.overlay = "create" } }
                    Text { text: "Пинг"; color: theme.text; font.pixelSize: 15; visible: backend.configs.length > 0
                        MouseArea { anchors.fill: parent; onClicked: backend.pingConfigs() } }
                }
                Text {
                    visible: backend.configs.length === 0
                    Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 30
                    text: "Нет конфигов — импортируйте или создайте"; color: theme.textFaint; font.pixelSize: 14
                }
                Repeater {
                    model: backend.configs
                    ColumnLayout {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        RowLayout { Layout.fillWidth: true; Layout.topMargin: 8; Layout.bottomMargin: 8; spacing: 12
                            Image { source: "qrc:/assets/logo.png"; Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                    sourceSize: Qt.size(44,44); opacity: index === backend.activeIndex ? 1 : 0.35 }
                            Text { text: modelData; color: theme.text; font.pixelSize: 14
                                   font.weight: index === backend.activeIndex ? Font.Medium : Font.Normal
                                   MouseArea { anchors.fill: parent; onClicked: backend.selectConfig(index) } }
                            Item { Layout.fillWidth: true }
                            Text { visible: index < backend.pings.length; text: index < backend.pings.length ? backend.pings[index] : ""
                                   color: theme.textDim; font.pixelSize: 12 }
                            Rectangle { visible: index === backend.activeIndex; radius: 10; color: Qt.rgba(0.11,0.62,0.46,0.16)
                                implicitWidth: ab.width+16; implicitHeight: 20
                                Text { id: ab; anchors.centerIn: parent; text: "активен"; color: theme.success; font.pixelSize: 11; font.weight: Font.Medium } }
                            Text { text: "✕"; color: theme.danger; font.pixelSize: 15; leftPadding: 6
                                   MouseArea { anchors.fill: parent; onClicked: backend.removeConfig(index) } }
                        }
                        Sep {}
                    }
                }
                Item { Layout.fillHeight: true }
            }
            // Import dropdown (collapsed by default).
            Rectangle {
                id: importMenu; visible: false; z: 10
                anchors.top: parent.top; anchors.topMargin: 40; anchors.horizontalCenter: parent.horizontalCenter
                width: 230; height: menuCol.height + 12; radius: 10; color: theme.bg
                border.color: theme.border; border.width: 1
                Column { id: menuCol; width: parent.width; y: 6
                    Text { width: parent.width; height: 40; leftPadding: 14; verticalAlignment: Text.AlignVCenter
                           text: "Вставить из буфера"; color: theme.text; font.pixelSize: 14
                           MouseArea { anchors.fill: parent; onClicked: { importMenu.visible = false; backend.importFromClipboard() } } }
                    Text { width: parent.width; height: 40; leftPadding: 14; verticalAlignment: Text.AlignVCenter
                           text: "Из файла…"; color: theme.text; font.pixelSize: 14
                           MouseArea { anchors.fill: parent; onClicked: { importMenu.visible = false; fileDlg.open() } } }
                }
            }
            Platform.FileDialog {
                id: fileDlg; title: "Выберите конфиг"
                nameFilters: ["TOML (*.toml)", "Все файлы (*)"]
                onAccepted: backend.importFile(fileDlg.file.toString())
            }
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
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Язык"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Text { text: (backend.language === "ru" ? "Русский" : "English") + " ▾"; color: theme.textDim; font.pixelSize: 14
                        MouseArea { anchors.fill: parent; onClicked: backend.language = (backend.language === "ru" ? "en" : "ru") } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Тема"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Text { text: ({system:"Система",light:"Светлая",dark:"Тёмная"}[backend.themeMode] || "Система") + " ▾"; color: theme.textDim; font.pixelSize: 14
                        MouseArea { anchors.fill: parent; onClicked: backend.themeMode = (backend.themeMode === "system" ? "light" : backend.themeMode === "light" ? "dark" : "system") } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Text { text: "Запускать при входе в систему"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Toggle { checked: backend.autoStart; onToggled: function(v){ backend.autoStart = v } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Text { text: "Подключаться автоматически"; color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Toggle { checked: backend.autoConnect; onToggled: function(v){ backend.autoConnect = v } } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: "Безопасность" }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Kill switch"; color: theme.text; font.pixelSize: 14 }
                    Text { text: "блокировать трафик вне VPN"; color: theme.textFaint; font.pixelSize: 12; leftPadding: 6 }
                    Item { Layout.fillWidth: true }
                    Toggle { checked: backend.killSwitch; onToggled: function(v){ backend.killSwitch = v } } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: "Горячие клавиши" }
                HotkeyField { label: "Переключить VPN"; value: backend.hotkeyToggle
                    onCaptured: function(s){ backend.hotkeyToggle = s } }
                Sep {}
                HotkeyField { label: "Подключить"; value: backend.hotkeyConnect
                    onCaptured: function(s){ backend.hotkeyConnect = s } }
                Sep {}
                HotkeyField { label: "Отключить"; value: backend.hotkeyDisconnect
                    onCaptured: function(s){ backend.hotkeyDisconnect = s } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: "Обслуживание" }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Сетевые адаптеры"; color: theme.text; font.pixelSize: 14 }
                    Text { text: "Windows"; color: theme.textFaint; font.pixelSize: 12; leftPadding: 6 }
                    Item { Layout.fillWidth: true } Text { text: "›"; color: theme.textDim; font.pixelSize: 16 }
                    MouseArea { anchors.fill: parent; onClicked: win.overlay = "adapters" } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Проверить обновления"; color: theme.text; font.pixelSize: 14 }
                    Text { visible: backend.updateMessage.length > 0; text: backend.updateMessage
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 12; leftPadding: 8 }
                    Item { Layout.fillWidth: true }
                    Text { text: backend.updateState === "checking" ? "…"
                                 : backend.updateState === "available" ? "Скачать ›" : backend.appVersion
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent
                        onClicked: backend.updateState === "available" ? backend.openLatestRelease()
                                                                       : backend.checkForUpdates() } }
                Item { Layout.preferredHeight: 14 }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                       text: "FreeTunnel " + backend.appVersion + " · ядро TrustTunnel"; color: theme.textFaint; font.pixelSize: 12 }
                Item { Layout.preferredHeight: 14 }
            }
        }
    }

    // ===================== Logs =====================
    Component {
        id: logsPage
        Item {
        ColumnLayout {
            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
            anchors.topMargin: 6; anchors.bottomMargin: 12; spacing: 8
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Text { text: "Очистить"; color: theme.accent; font.pixelSize: 13
                    MouseArea { anchors.fill: parent; onClicked: backend.clearLogs() } }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: theme.surface
                clip: true
                Text {
                    anchors.centerIn: parent; visible: backend.logEntries.length === 0
                    text: "Логи появятся после подключения"; color: theme.textFaint; font.pixelSize: 13
                }
                ListView {
                    id: logList
                    anchors.fill: parent; anchors.margins: 12; spacing: 3
                    model: backend.logEntries
                    clip: true
                    property bool autoScroll: true
                    onCountChanged: if (autoScroll) positionViewAtEnd()
                    delegate: Row {
                        required property var modelData
                        width: logList.width; spacing: 6
                        Text { text: modelData.time; color: theme.textFaint; font.pixelSize: 11; font.family: "Menlo" }
                        Text { text: modelData.level; font.pixelSize: 11; font.family: "Menlo"
                               color: modelData.level === "ERROR" ? "#a32d2d" : modelData.level === "WARN" ? "#ba7517" : "#185fa5" }
                        Text { text: modelData.msg; color: theme.text; font.pixelSize: 11; font.family: "Menlo"
                               width: parent.width - 116; elide: Text.ElideRight }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Text { Layout.fillWidth: true; text: backend.logPath; color: theme.textDim; font.pixelSize: 11
                       elide: Text.ElideMiddle
                       MouseArea { anchors.fill: parent; onClicked: backend.openLogFolder() } }
                Text { text: "Авто-прокрутка"; color: theme.textDim; font.pixelSize: 12 }
                Toggle { checked: logList.autoScroll; implicitWidth: 34; implicitHeight: 20
                         onToggled: function(v){ logList.autoScroll = v } }
            }
        }
        }
    }

    // ===================== Create config (sub-screen) =====================
    Component {
        id: createConfig
        Rectangle {
            id: cform
            color: theme.bg
            property string protocol: "http2"
            property bool ipv6: true
            Item {
                id: chdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: 48
                Text { id: cBack; anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                       text: "←"; color: theme.textDim; font.pixelSize: 20
                       MouseArea { anchors.fill: parent; onClicked: win.overlay = "" } }
                Text { anchors.left: cBack.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                       text: "Новый конфиг"; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
            }
            Flickable {
                anchors.top: chdr.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 18; anchors.rightMargin: 18; contentHeight: fcol.height; clip: true
                Column {
                    id: fcol; width: parent.width; spacing: 10
                    Field { id: fName; label: "Имя"; placeholder: "Германия · Франкфурт" }
                    Field { id: fHost; label: "Хост сервера"; placeholder: "frankfurt.example.com" }
                    Field { id: fAddr; label: "Адрес(а) · host:port (через запятую)"; placeholder: "1.2.3.4:443" }
                    Row { width: parent.width; spacing: 10
                        Field { id: fUser; label: "Логин"; width: (parent.width - 10) / 2 }
                        Field { id: fPass; label: "Пароль"; password: true; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; spacing: 10
                        Column { width: (parent.width - 10) / 2; spacing: 4
                            Text { text: "Протокол"; color: theme.textDim; font.pixelSize: 13 }
                            Rectangle { width: parent.width; height: 34; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1
                                Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                       text: cform.protocol === "http3" ? "HTTP/3" : "HTTP/2"; color: theme.text; font.pixelSize: 14 }
                                Text { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                       text: "▾"; color: theme.textDim; font.pixelSize: 14 }
                                MouseArea { anchors.fill: parent; onClicked: cform.protocol = (cform.protocol === "http2" ? "http3" : "http2") } }
                        }
                        Field { id: fDns; label: "DNS-серверы"; placeholder: "1.1.1.1, 8.8.8.8"; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; spacing: 10
                        Field { id: fSni; label: "Custom SNI"; width: (parent.width - 10) / 2 }
                        Field { id: fRandom; label: "Client random (hex)"; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; height: 32; spacing: 8
                        Text { text: "Разрешить IPv6"; color: theme.text; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Item { width: parent.width - 220; height: 1 }
                        Toggle { checked: cform.ipv6; anchors.verticalCenter: parent.verticalCenter; onToggled: function(v){ cform.ipv6 = v } }
                    }
                    Column { width: parent.width; spacing: 4
                        Text { text: "Сертификат (PEM) · необязательно"; color: theme.textDim; font.pixelSize: 13 }
                        Rectangle { width: parent.width; height: 70; radius: 8; color: theme.bg; border.color: fCert.activeFocus ? theme.accent : theme.border; border.width: 1
                            Flickable { anchors.fill: parent; anchors.margins: 8; contentHeight: fCert.height; clip: true
                                TextEdit { id: fCert; width: parent.width; font.pixelSize: 12; font.family: "Menlo"; color: theme.text; wrapMode: TextEdit.WrapAnywhere } } }
                    }
                    Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 10; topPadding: 4; bottomPadding: 16
                        Rectangle { width: 110; height: 36; radius: 8; color: theme.accent
                            Text { anchors.centerIn: parent; text: "Сохранить"; color: "white"; font.pixelSize: 14 }
                            MouseArea { anchors.fill: parent; onClicked: {
                                var ok = backend.createConfig({
                                    name: fName.text, hostname: fHost.text, addresses: fAddr.text,
                                    username: fUser.text, password: fPass.text, protocol: cform.protocol,
                                    dns: fDns.text, customSni: fSni.text, clientRandom: fRandom.text,
                                    allowIpv6: cform.ipv6, certificate: fCert.text });
                                if (ok) win.overlay = "";
                            } } }
                        Rectangle { width: 90; height: 36; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1
                            Text { anchors.centerIn: parent; text: "Отмена"; color: theme.text; font.pixelSize: 14 }
                            MouseArea { anchors.fill: parent; onClicked: win.overlay = "" } }
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
            Component.onCompleted: if (backend.adapterScanSupported) backend.scanAdapters()
            RowLayout {
                id: ahdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 14; spacing: 10
                Text { text: "←"; color: theme.textDim; font.pixelSize: 20
                       MouseArea { anchors.fill: parent; onClicked: win.overlay = "" } }
                Text { text: "Сетевые адаптеры"; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                Item { Layout.fillWidth: true }
                Text { visible: backend.adapterScanSupported; text: "Сканировать"; color: theme.accent; font.pixelSize: 13
                       MouseArea { anchors.fill: parent; onClicked: backend.scanAdapters() } }
            }
            // Unsupported-platform notice (scan is Windows-only).
            Text {
                visible: !backend.adapterScanSupported
                anchors.top: ahdr.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 18; wrapMode: Text.WordWrap
                text: "Управление сетевыми адаптерами доступно только в Windows."
                color: theme.textFaint; font.pixelSize: 13
            }
            ColumnLayout {
                visible: backend.adapterScanSupported
                anchors.top: ahdr.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 18; anchors.rightMargin: 18; anchors.topMargin: 4; spacing: 0
                Text { text: "Сторонние VPN-адаптеры могут конфликтовать с FreeTunnel."; color: theme.textFaint; font.pixelSize: 12; Layout.bottomMargin: 8 }
                Text { visible: backend.adapters.length === 0; text: "Адаптеры не найдены. Нажмите «Сканировать»."
                       color: theme.textFaint; font.pixelSize: 13; Layout.topMargin: 8 }
                Repeater {
                    model: backend.adapters
                    ColumnLayout { required property var modelData; Layout.fillWidth: true
                        RowLayout { Layout.fillWidth: true; Layout.topMargin: 9; Layout.bottomMargin: 9; spacing: 12
                            Image { visible: parent.parent.modelData.ours; source: "qrc:/assets/logo.png"; width: 20; height: 20; sourceSize: Qt.size(40,40) }
                            ColumnLayout { spacing: 1
                                Text { text: parent.parent.parent.modelData.name; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                                Text { text: parent.parent.parent.modelData.ours ? parent.parent.parent.modelData.description
                                             : (parent.parent.parent.modelData.conflict
                                                ? ("может конфликтовать" + (parent.parent.parent.modelData.enabled ? " · включён" : " · отключён"))
                                                : (parent.parent.parent.modelData.enabled ? "включён" : "отключён"))
                                       color: parent.parent.parent.modelData.ours ? theme.textDim
                                             : (parent.parent.parent.modelData.conflict ? theme.warn : theme.textFaint); font.pixelSize: 12 }
                            }
                            Item { Layout.fillWidth: true }
                            Toggle { visible: !parent.parent.modelData.ours; checked: parent.parent.modelData.enabled
                                     onToggled: function(v){ backend.setAdapterEnabled(parent.parent.modelData.name, v) } }
                        }
                        Sep {}
                    }
                }
            }
        }
    }
}
