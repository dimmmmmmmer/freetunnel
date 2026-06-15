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

    // Active palette: light/dark, or follow the OS when themeMode === "system".
    readonly property bool systemDark: Application.styleHints.colorScheme === Qt.Dark
    QtObject {
        id: theme
        readonly property bool dark: backend.themeMode === "dark"
                                     || (backend.themeMode === "system" && win.systemDark)
        readonly property color bg: dark ? "#16181d" : "#ffffff"
        readonly property color surface: dark ? "#23262d" : "#f3f4f6"
        readonly property color text: dark ? "#e8eaed" : "#1b1d21"
        readonly property color textDim: dark ? "#9aa1ab" : "#6b7280"
        readonly property color textFaint: dark ? "#6b7280" : "#9aa1ab"
        readonly property color accent: dark ? "#4a9eea" : "#185fa5"
        readonly property color border: dark ? "#2d313a" : "#e7e9ee"
        readonly property color success: dark ? "#3fbf93" : "#1d9e75"
        readonly property color warn: dark ? "#d99634" : "#ba7517"
        readonly property color danger: dark ? "#e06a6a" : "#a32d2d"
        readonly property color infoBg: dark ? Qt.rgba(0.29, 0.62, 0.92, 0.18)
                                             : Qt.rgba(0.094, 0.373, 0.647, 0.12)
    }

    property int currentPage: 0
    property string overlay: "" // "", "create"
    property int editIndex: -1  // config being edited in the create overlay (-1 = new)
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
                        // Home shows our logo; Configs uses the "servers" glyph.
                        source: parent.modelData === "connection" ? "qrc:/assets/logo.png"
                              : parent.modelData === "configs" ? "qrc:/icons/connection.svg"
                              : "qrc:/icons/" + parent.modelData + ".svg"
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
        sourceComponent: win.overlay === "create" ? createConfig : null
    }

    // ---------- toast (errors/notices) ----------
    Connections {
        target: backend
        function onErrorOccurred(msg) { toast.show(msg) }
    }
    Rectangle {
        id: toast
        z: 1000
        property string message: ""
        function show(m) { message = m; opacity = 0.97; toastTimer.restart() }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        width: Math.min(parent.width - 36, tmsg.implicitWidth + 32)
        height: Math.max(40, tmsg.implicitHeight + 18)
        radius: 9; color: "#2a2d33"; opacity: 0; visible: opacity > 0
        Text {
            id: tmsg; anchors.centerIn: parent; width: toast.width - 24
            text: toast.message; color: "white"; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
        }
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Timer { id: toastTimer; interval: 3200; onTriggered: toast.opacity = 0 }
        MouseArea { anchors.fill: parent; onClicked: toast.opacity = 0 }
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
                    text: hk.capturing ? qsTr("Press…") : (hk.value || "—")
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
                        property color ringColor: backend.connected ? theme.accent : theme.border
                        onRingColorChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d"); ctx.reset();
                            ctx.lineWidth = 9; ctx.lineCap = "round";
                            ctx.strokeStyle = ringColor;
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
                            text: backend.connected ? backend.sessionTime : qsTr("Off")
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
                    Layout.alignment: Qt.AlignHCenter; spacing: 12
                    Repeater {
                        model: [ { l: qsTr("Download"), v: backend.downSpeed, c: theme.success, a: "↓" },
                                 { l: qsTr("Upload"), v: backend.upSpeed, c: "#378add", a: "↑" } ]
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
                                       text: parent.parent.modelData.v + qsTr(" MB/s"); color: theme.text; font.pixelSize: 19; font.weight: Font.Medium }
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
                       text: qsTr("Split tunneling: the domains below go directly, bypassing the VPN.")
                       color: theme.textFaint; font.pixelSize: 12 }
                Item { Layout.preferredHeight: 4 }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: qsTr("Enable"); color: theme.text; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    Toggle { checked: backend.splitEnabled; onToggled: function(v){ backend.splitEnabled = v } }
                }
                Item { Layout.preferredHeight: 10 }
                SectionLabel { text: qsTr("Profile") }
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
                           text: qsTr("profile name, then Enter"); color: theme.textFaint; font.pixelSize: 13
                           visible: npInput.text.length === 0 }
                }
                RowLayout { Layout.fillWidth: true; Layout.topMargin: 6; visible: backend.activeProfile !== "Default"
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("Delete profile “%1”").arg(backend.activeProfile); color: theme.danger; font.pixelSize: 12
                        MouseArea { anchors.fill: parent; onClicked: backend.removeProfile(backend.activeProfile) } }
                }
                Item { Layout.preferredHeight: 12 }
                RowLayout { Layout.fillWidth: true
                    SectionLabel { text: qsTr("Domains — bypass VPN") }
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("Clear all"); color: theme.danger; font.pixelSize: 12; visible: backend.domains.length > 0
                        MouseArea { anchors.fill: parent; onClicked: backend.clearDomains() } }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    Layout.topMargin: backend.domains.length > 0 ? 6 : 0
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
                    Layout.fillWidth: true; Layout.preferredHeight: 36; radius: 8
                    Layout.topMargin: backend.domains.length > 0 ? 8 : 2
                    color: theme.bg; border.color: domInput.activeFocus ? theme.accent : theme.border; border.width: 1
                    TextInput {
                        id: domInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter; clip: true
                        font.pixelSize: 13; color: theme.text
                        onAccepted: { backend.addDomain(text); text = "" }
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: qsTr("add a domain, then Enter"); color: theme.textFaint; font.pixelSize: 13
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
                    Text { text: qsTr("Import ▾"); color: theme.accent; font.pixelSize: 15; font.weight: Font.Medium
                        MouseArea { anchors.fill: parent; onClicked: importMenu.visible = !importMenu.visible } }
                    Text { text: qsTr("Create"); color: theme.text; font.pixelSize: 15
                        MouseArea { anchors.fill: parent; onClicked: { win.editIndex = -1; win.overlay = "create" } } }
                    Text { text: qsTr("Ping"); color: theme.text; font.pixelSize: 15; visible: backend.configs.length > 0
                        MouseArea { anchors.fill: parent; onClicked: backend.pingConfigs() } }
                }
                Text {
                    visible: backend.configs.length === 0
                    Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 30
                    text: qsTr("No configs — import or create one"); color: theme.textFaint; font.pixelSize: 14
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
                            Rectangle { visible: index === backend.activeIndex && backend.connected
                                radius: 10; color: Qt.rgba(0.11,0.62,0.46,0.16)
                                implicitWidth: ab.width+16; implicitHeight: 20
                                Text { id: ab; anchors.centerIn: parent; text: qsTr("connected"); color: theme.success; font.pixelSize: 11; font.weight: Font.Medium } }
                            Text { text: qsTr("Edit"); color: theme.accent; font.pixelSize: 12; leftPadding: 10
                                   MouseArea { anchors.fill: parent; onClicked: { win.editIndex = index; win.overlay = "create" } } }
                            Text { text: "✕"; color: theme.danger; font.pixelSize: 15; leftPadding: 10
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
                           text: qsTr("Paste from clipboard"); color: theme.text; font.pixelSize: 14
                           MouseArea { anchors.fill: parent; onClicked: { importMenu.visible = false; backend.importFromClipboard() } } }
                    Text { width: parent.width; height: 40; leftPadding: 14; verticalAlignment: Text.AlignVCenter
                           text: qsTr("From file…"); color: theme.text; font.pixelSize: 14
                           MouseArea { anchors.fill: parent; onClicked: { importMenu.visible = false; fileDlg.open() } } }
                }
            }
            Platform.FileDialog {
                id: fileDlg; title: qsTr("Select a config")
                nameFilters: ["TOML (*.toml)", qsTr("All files (*)")]
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
                SectionLabel { text: qsTr("General") }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: qsTr("Language"); color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Text { text: (backend.language === "ru" ? "Русский" : "English") + " ▾"; color: theme.textDim; font.pixelSize: 14
                        MouseArea { anchors.fill: parent; onClicked: backend.language = (backend.language === "ru" ? "en" : "ru") } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: qsTr("Theme"); color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Text { text: ({system:qsTr("System"),light:qsTr("Light"),dark:qsTr("Dark")}[backend.themeMode] || qsTr("System")) + " ▾"; color: theme.textDim; font.pixelSize: 14
                        MouseArea { anchors.fill: parent; onClicked: backend.themeMode = (backend.themeMode === "system" ? "light" : backend.themeMode === "light" ? "dark" : "system") } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Text { text: qsTr("Launch at system startup"); color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Toggle { checked: backend.autoStart; onToggled: function(v){ backend.autoStart = v } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Text { text: qsTr("Connect automatically"); color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Toggle { checked: backend.autoConnect; onToggled: function(v){ backend.autoConnect = v } } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: qsTr("Security") }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Kill switch"; color: theme.text; font.pixelSize: 14 }
                    Text { text: qsTr("block traffic outside the VPN"); color: theme.textFaint; font.pixelSize: 12; leftPadding: 6 }
                    Item { Layout.fillWidth: true }
                    Toggle { checked: backend.killSwitch; onToggled: function(v){ backend.killSwitch = v } } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: qsTr("Hotkeys") }
                HotkeyField { label: qsTr("Toggle VPN"); value: backend.hotkeyToggle
                    onCaptured: function(s){ backend.hotkeyToggle = s } }
                Sep {}
                HotkeyField { label: qsTr("Connect"); value: backend.hotkeyConnect
                    onCaptured: function(s){ backend.hotkeyConnect = s } }
                Sep {}
                HotkeyField { label: qsTr("Disconnect"); value: backend.hotkeyDisconnect
                    onCaptured: function(s){ backend.hotkeyDisconnect = s } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: qsTr("Maintenance") }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: qsTr("Check for updates"); color: theme.text; font.pixelSize: 14 }
                    Text { visible: backend.updateMessage.length > 0; text: backend.updateMessage
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 12; leftPadding: 8 }
                    Item { Layout.fillWidth: true }
                    Text { text: backend.updateState === "checking" ? "…"
                                 : backend.updateState === "available" ? qsTr("Download ›") : backend.appVersion
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent
                        onClicked: backend.updateState === "available" ? backend.openLatestRelease()
                                                                       : backend.checkForUpdates() } }
                Item { Layout.preferredHeight: 14 }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                       text: qsTr("FreeTunnel %1 · TrustTunnel core %2").arg(backend.appVersion).arg(backend.coreVersion); color: theme.textFaint; font.pixelSize: 12 }
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
                Text { text: qsTr("Clear"); color: theme.accent; font.pixelSize: 13
                    MouseArea { anchors.fill: parent; onClicked: backend.clearLogs() } }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: theme.surface
                clip: true
                Text {
                    anchors.centerIn: parent; visible: backend.logEntries.length === 0
                    text: qsTr("Logs will appear after connecting"); color: theme.textFaint; font.pixelSize: 13
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
                Text { text: qsTr("Auto-scroll"); color: theme.textDim; font.pixelSize: 12 }
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
            readonly property bool editing: win.editIndex >= 0
            // Prefill from the selected config when editing.
            Component.onCompleted: {
                if (!editing) return
                var f = backend.configFields(win.editIndex)
                fName.text = f.name || ""; fHost.text = f.hostname || ""
                fAddr.text = f.addresses || ""; fUser.text = f.username || ""
                fPass.text = f.password || ""; fDns.text = f.dns || ""
                fSni.text = f.customSni || ""; fRandom.text = f.clientRandom || ""
                fCert.text = f.certificate || ""
                cform.protocol = f.protocol === "http3" ? "http3" : "http2"
                cform.ipv6 = f.allowIpv6 === undefined ? true : f.allowIpv6
            }
            Item {
                id: chdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: 48
                Text { id: cBack; anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                       text: "←"; color: theme.textDim; font.pixelSize: 20
                       MouseArea { anchors.fill: parent; onClicked: { win.editIndex = -1; win.overlay = "" } } }
                Text { anchors.left: cBack.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                       text: cform.editing ? qsTr("Edit config") : qsTr("New config"); color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
            }
            Flickable {
                anchors.top: chdr.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 18; anchors.rightMargin: 18; contentHeight: fcol.height; clip: true
                Column {
                    id: fcol; width: parent.width; spacing: 10
                    Field { id: fName; label: qsTr("Name"); placeholder: qsTr("Germany · Frankfurt") }
                    Field { id: fHost; label: qsTr("Server host"); placeholder: "frankfurt.example.com" }
                    Field { id: fAddr; label: qsTr("Address(es) · host:port (comma-separated)"); placeholder: "1.2.3.4:443" }
                    Row { width: parent.width; spacing: 10
                        Field { id: fUser; label: qsTr("Username"); width: (parent.width - 10) / 2 }
                        Field { id: fPass; label: qsTr("Password"); password: true; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; spacing: 10
                        Column { width: (parent.width - 10) / 2; spacing: 4
                            Text { text: qsTr("Protocol"); color: theme.textDim; font.pixelSize: 13 }
                            Rectangle { width: parent.width; height: 34; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1
                                Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                       text: cform.protocol === "http3" ? "HTTP/3" : "HTTP/2"; color: theme.text; font.pixelSize: 14 }
                                Text { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                       text: "▾"; color: theme.textDim; font.pixelSize: 14 }
                                MouseArea { anchors.fill: parent; onClicked: cform.protocol = (cform.protocol === "http2" ? "http3" : "http2") } }
                        }
                        Field { id: fDns; label: qsTr("DNS servers"); placeholder: "1.1.1.1, 8.8.8.8"; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; spacing: 10
                        Field { id: fSni; label: "Custom SNI"; width: (parent.width - 10) / 2 }
                        Field { id: fRandom; label: "Client random (hex)"; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; height: 32; spacing: 8
                        Text { text: qsTr("Allow IPv6"); color: theme.text; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                        Item { width: parent.width - 220; height: 1 }
                        Toggle { checked: cform.ipv6; anchors.verticalCenter: parent.verticalCenter; onToggled: function(v){ cform.ipv6 = v } }
                    }
                    Column { width: parent.width; spacing: 4
                        Text { text: qsTr("Certificate (PEM) · optional"); color: theme.textDim; font.pixelSize: 13 }
                        Rectangle { width: parent.width; height: 70; radius: 8; color: theme.bg; border.color: fCert.activeFocus ? theme.accent : theme.border; border.width: 1
                            Flickable { anchors.fill: parent; anchors.margins: 8; contentHeight: fCert.height; clip: true
                                TextEdit { id: fCert; width: parent.width; font.pixelSize: 12; font.family: "Menlo"; color: theme.text; wrapMode: TextEdit.WrapAnywhere } } }
                    }
                    Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 10; topPadding: 4; bottomPadding: 16
                        Rectangle { width: 110; height: 36; radius: 8; color: theme.accent
                            Text { anchors.centerIn: parent; text: qsTr("Save"); color: "white"; font.pixelSize: 14 }
                            MouseArea { anchors.fill: parent; onClicked: {
                                var ok = backend.createConfig({
                                    name: fName.text, hostname: fHost.text, addresses: fAddr.text,
                                    username: fUser.text, password: fPass.text, protocol: cform.protocol,
                                    dns: fDns.text, customSni: fSni.text, clientRandom: fRandom.text,
                                    allowIpv6: cform.ipv6, certificate: fCert.text, editIndex: win.editIndex });
                                if (ok) { win.editIndex = -1; win.overlay = "" }
                            } } }
                        Rectangle { width: 90; height: 36; radius: 8; color: theme.bg; border.color: theme.border; border.width: 1
                            Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                            MouseArea { anchors.fill: parent; onClicked: { win.editIndex = -1; win.overlay = "" } } }
                    }
                }
            }
        }
    }

}
