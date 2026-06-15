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
    minimumWidth: 360
    minimumHeight: 480
    color: theme.bg
    title: "FreeTunnel"

    // Closing the window hides to tray instead of quitting (quitOnLastWindowClosed
    // is off in main.cpp). Quit explicitly from the tray menu.
    onClosing: function(close) { close.accepted = false; win.hide() }

    // ---------- system tray ----------
    Platform.SystemTrayIcon {
        id: tray
        visible: true
        icon.source: "qrc:/assets/logo.svg"
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
        readonly property color accent: dark ? "#aeb6c2" : "#4b5563"
        readonly property color border: dark ? "#2d313a" : "#e7e9ee"
        readonly property color success: dark ? "#3fbf93" : "#1d9e75"
        readonly property color warn: dark ? "#d99634" : "#ba7517"
        readonly property color danger: dark ? "#e06a6a" : "#a32d2d"
        readonly property color infoBg: dark ? Qt.rgba(0.68, 0.71, 0.76, 0.16)
                                             : Qt.rgba(0.29, 0.33, 0.39, 0.12)
    }

    property int currentPage: 0
    property string overlay: "" // "", "create"
    property int editIndex: -1  // config being edited in the create overlay (-1 = new)
    // nav order: Home, Configs, Split, Settings, Logs (configs before split)
    readonly property var navIcons: ["connection", "configs", "network", "settings", "log"]

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
                    id: navItem
                    required property int index
                    required property string modelData
                    property bool active: index === win.currentPage
                    width: 46; height: 38; radius: 8
                    color: active ? theme.infoBg : (nma.containsMouse ? theme.surface : "transparent")
                    Behavior on color { ColorAnimation { duration: 120 } }
                    scale: nma.containsMouse && !active ? 1.08 : 1.0
                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    // Home shows our (colourful) logo; other tabs use tinted glyphs.
                    Image {
                        visible: navItem.modelData === "connection"
                        anchors.centerIn: parent; width: 22; height: 22
                        source: "qrc:/assets/logo.svg"; sourceSize: Qt.size(44, 44)
                        opacity: navItem.active ? 1.0 : 0.8
                    }
                    Icon {
                        visible: navItem.modelData !== "connection"
                        anchors.centerIn: parent; width: 22; height: 22
                        svg: navItem.modelData === "configs" ? "qrc:/icons/connection.svg"
                                                             : "qrc:/icons/" + navItem.modelData + ".svg"
                        color: navItem.active ? theme.accent : theme.textDim
                    }
                    MouseArea { id: nma; anchors.fill: parent; hoverEnabled: true
                                onClicked: win.currentPage = navItem.index }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: [homePage, configsPage, splitPage, settingsPage, logsPage][win.currentPage]
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

    // Monochrome SVG icon tinted to `color`. Recolors by substituting
    // "currentColor" in the SVG and feeding a data URI — shader-free, so it
    // works on every render backend (raw Image would render black).
    component Icon: Image {
        id: ic
        property color color: theme.text
        property url svg
        fillMode: Image.PreserveAspectFit; smooth: true
        sourceSize: Qt.size(width * 2, height * 2)
        function reload() {
            if (svg == "") return
            var x = new XMLHttpRequest()
            x.open("GET", svg)
            x.onreadystatechange = function() {
                if (x.readyState === XMLHttpRequest.DONE && x.responseText) {
                    var s = x.responseText.replace(/currentColor/g, "" + ic.color)
                    ic.source = "data:image/svg+xml;utf8," + encodeURIComponent(s)
                }
            }
            x.send()
        }
        onColorChanged: reload()
        onSvgChanged: reload()
        Component.onCompleted: reload()
    }

    // Animated inline dropdown (real options list, not a click-cycle).
    component Dropdown: Item {
        id: dd
        property string label: ""
        property var model: []     // [{ v: "...", t: "..." }]
        property string value: ""
        signal picked(string v)
        property bool open: false
        property real panelHeight: open ? optCol.implicitHeight : 0
        Behavior on panelHeight { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
        Layout.fillWidth: true
        implicitHeight: 42 + panelHeight
        Layout.preferredHeight: implicitHeight
        function labelFor(v) {
            for (var i = 0; i < model.length; i++) if (model[i].v === v) return model[i].t
            return ""
        }
        RowLayout {
            id: ddRow; width: parent.width; height: 42
            Text { text: dd.label; color: theme.text; font.pixelSize: 14 }
            Item { Layout.fillWidth: true }
            Text { text: dd.labelFor(dd.value); color: theme.textDim; font.pixelSize: 14 }
            Text { text: "▾"; color: theme.textDim; font.pixelSize: 13; leftPadding: 6
                   rotation: dd.open ? 180 : 0
                   Behavior on rotation { NumberAnimation { duration: 150 } } }
            MouseArea { anchors.fill: parent; onClicked: dd.open = !dd.open }
        }
        Item {
            anchors.top: ddRow.bottom; width: parent.width; height: dd.panelHeight; clip: true
            Column {
                id: optCol; width: parent.width
                Repeater {
                    model: dd.model
                    Rectangle {
                        required property var modelData
                        width: parent.width; height: 38; radius: 6
                        color: oma.containsMouse ? theme.surface : "transparent"
                        Text { anchors.verticalCenter: parent.verticalCenter; x: 12
                               text: parent.modelData.t
                               color: parent.modelData.v === dd.value ? theme.accent : theme.text; font.pixelSize: 14 }
                        Text { visible: parent.modelData.v === dd.value; text: "✓"; color: theme.accent; font.pixelSize: 14
                               anchors.right: parent.right; rightPadding: 12; anchors.verticalCenter: parent.verticalCenter }
                        MouseArea { id: oma; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { dd.picked(parent.modelData.v); dd.open = false } }
                    }
                }
            }
        }
    }

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
            id: homeRoot
            ColumnLayout {
                anchors.centerIn: parent
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
                            source: "qrc:/assets/logo.svg"; width: 56; height: 56
                            sourceSize: Qt.size(112, 112)
                            opacity: backend.connected ? 1.0 : 0.4
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: backend.connected
                            text: backend.sessionTime
                            color: theme.text
                            font.pixelSize: 17; font.weight: Font.Medium
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: backend.toggle() }
                }
                Row {
                    id: cfgSel
                    Layout.alignment: Qt.AlignHCenter; spacing: 6
                    Text { text: backend.configs.length > 0 ? backend.activeConfig : qsTr("Add a config")
                           color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                    Text { text: "▾"; color: theme.textDim; font.pixelSize: 15 }
                    MouseArea { anchors.fill: parent
                        onClicked: backend.configs.length > 0 ? (cfgPopup.open = !cfgPopup.open)
                                                              : (win.currentPage = 1) }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter; spacing: 12
                    Repeater {
                        model: [ { l: qsTr("Download"), v: backend.downSpeed, c: theme.success, a: "↓" },
                                 { l: qsTr("Upload"), v: backend.upSpeed, c: theme.textDim, a: "↑" } ]
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
            // Config picker dropdown, anchored under the selector.
            Rectangle {
                id: cfgPopup
                property bool open: false
                visible: open; z: 100
                width: 250
                x: cfgSel.mapToItem(homeRoot, 0, 0).x + cfgSel.width / 2 - width / 2
                y: cfgSel.mapToItem(homeRoot, 0, 0).y + cfgSel.height + 6
                height: picker.height + addRow.height + 11
                radius: 10; color: theme.bg; border.color: theme.border; border.width: 1
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
                            color: pma.containsMouse ? theme.surface : "transparent"
                            Text { anchors.verticalCenter: parent.verticalCenter; x: 10
                                   width: parent.width - 20; elide: Text.ElideRight
                                   text: parent.modelData
                                   color: parent.index === backend.activeIndex ? theme.accent : theme.text
                                   font.pixelSize: 14 }
                            MouseArea { id: pma; anchors.fill: parent; hoverEnabled: true
                                        onClicked: { backend.selectConfig(parent.index); cfgPopup.open = false } }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; color: theme.border }
                    Rectangle {
                        id: addRow; width: parent.width; height: 40; radius: 6
                        color: ama.containsMouse ? theme.surface : "transparent"
                        Text { anchors.verticalCenter: parent.verticalCenter; x: 10
                               text: qsTr("Add a config…"); color: theme.accent; font.pixelSize: 14 }
                        MouseArea { id: ama; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { cfgPopup.open = false; win.currentPage = 1 } }
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
            // Header: Add (+) opens the import/create menu, Ping (speedometer).
            RowLayout {
                id: cfgHdr
                anchors.top: parent.top; anchors.topMargin: 8
                anchors.horizontalCenter: parent.horizontalCenter; spacing: 16
                Rectangle {
                    width: 40; height: 32; radius: 8
                    color: addMa.containsMouse ? theme.surface : "transparent"
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Text { anchors.centerIn: parent; text: "+"; color: theme.accent; font.pixelSize: 22 }
                    MouseArea { id: addMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: importMenu.visible = !importMenu.visible }
                }
                Rectangle {
                    visible: backend.configs.length > 0
                    width: 40; height: 32; radius: 8
                    color: pingMa.containsMouse ? theme.surface : "transparent"
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Icon { anchors.centerIn: parent; width: 22; height: 22; svg: "qrc:/icons/speedometer.svg"; color: theme.text }
                    MouseArea { id: pingMa; anchors.fill: parent; hoverEnabled: true; onClicked: backend.pingConfigs() }
                }
            }
            Text {
                visible: backend.configs.length === 0
                anchors.centerIn: parent
                text: qsTr("Add a config"); color: theme.textFaint; font.pixelSize: 15
                MouseArea { anchors.fill: parent; onClicked: importMenu.visible = true }
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
                        color: rowMa.containsMouse ? theme.surface : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } } }
                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: backend.selectConfig(index) }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6; spacing: 10
                        Image { source: "qrc:/assets/logo.svg"; Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                sourceSize: Qt.size(44,44); opacity: index === backend.activeIndex ? 1 : 0.4 }
                        Text { text: modelData; color: theme.text; font.pixelSize: 14
                               font.weight: index === backend.activeIndex ? Font.Medium : Font.Normal }
                        Item { Layout.fillWidth: true }
                        Text { visible: index < backend.pings.length && backend.pings[index] !== ""
                               text: index < backend.pings.length ? backend.pings[index] : ""
                               color: theme.textDim; font.pixelSize: 12 }
                        Rectangle { visible: index === backend.activeIndex && backend.connected
                            radius: 10; color: theme.infoBg; implicitWidth: ab.width+16; implicitHeight: 20
                            Text { id: ab; anchors.centerIn: parent; text: qsTr("connected"); color: theme.success; font.pixelSize: 11; font.weight: Font.Medium } }
                        Text { text: "⋯"; color: dotsMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 18; padding: 6
                               MouseArea { id: dotsMa; anchors.fill: parent; hoverEnabled: true
                                           onClicked: { win.editIndex = index; win.overlay = "create" } } }
                        Text { text: "✕"; color: theme.danger; font.pixelSize: 14; padding: 6
                               MouseArea { anchors.fill: parent; onClicked: backend.removeConfig(index) } }
                    }
                }
            }
            // Import / create dropdown (collapsed by default).
            Rectangle {
                id: importMenu; visible: false; z: 10
                anchors.top: parent.top; anchors.topMargin: 44; anchors.horizontalCenter: parent.horizontalCenter
                width: 240; height: menuCol.height + 12; radius: 10; color: theme.bg
                border.color: theme.border; border.width: 1
                Column { id: menuCol; width: parent.width; y: 6
                    component MenuRow: Text {
                        width: parent.width; height: 40; leftPadding: 14; verticalAlignment: Text.AlignVCenter
                        color: theme.text; font.pixelSize: 14
                    }
                    MenuRow { text: qsTr("Paste from clipboard")
                        Rectangle { anchors.fill: parent; z: -1; color: m1.containsMouse ? theme.surface : "transparent" }
                        MouseArea { id: m1; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { importMenu.visible = false; backend.importFromClipboard() } } }
                    MenuRow { text: qsTr("From file…")
                        Rectangle { anchors.fill: parent; z: -1; color: m2.containsMouse ? theme.surface : "transparent" }
                        MouseArea { id: m2; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { importMenu.visible = false; fileDlg.open() } } }
                    Rectangle { width: parent.width; height: 1; color: theme.border }
                    MenuRow { text: qsTr("Create new…")
                        Rectangle { anchors.fill: parent; z: -1; color: m3.containsMouse ? theme.surface : "transparent" }
                        MouseArea { id: m3; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { importMenu.visible = false; win.editIndex = -1; win.overlay = "create" } } }
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
                Dropdown { label: qsTr("Language"); value: backend.language
                    model: [{v:"en",t:"English"},{v:"ru",t:"Русский"}]
                    onPicked: function(v){ backend.language = v } }
                Sep {}
                Dropdown { label: qsTr("Theme"); value: backend.themeMode
                    model: [{v:"system",t:qsTr("System")},{v:"light",t:qsTr("Light")},{v:"dark",t:qsTr("Dark")}]
                    onPicked: function(v){ backend.themeMode = v } }
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
                               color: modelData.level === "ERROR" ? theme.danger : modelData.level === "WARN" ? theme.warn : theme.textDim }
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
