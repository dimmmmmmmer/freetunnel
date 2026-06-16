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
        tooltip: backend.connected ? qsTr("FreeTunnel — %1").arg(backend.activeConfig)
                                    : "FreeTunnel"
        // Click opens the menu (below). Double-click brings the window forward.
        onActivated: function(reason) {
            if (reason === Platform.SystemTrayIcon.DoubleClick) {
                win.show(); win.raise(); win.requestActivate()
            }
        }
        menu: Platform.Menu {
            // Status line + session time (read-only).
            Platform.MenuItem {
                enabled: false
                text: backend.connected ? qsTr("Connected · %1").arg(backend.activeConfig)
                                         : qsTr("Disconnected")
            }
            Platform.MenuItem {
                enabled: false; visible: backend.connected
                text: "   " + backend.sessionTime
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: backend.connected ? qsTr("Disconnect") : qsTr("Connect")
                onTriggered: backend.toggle()
            }
            Platform.MenuSeparator {}
            // Configs listed inline; the active one carries a checkmark.
            Instantiator {
                model: backend.configs
                delegate: Platform.MenuItem {
                    required property int index
                    required property string modelData
                    text: modelData
                    checkable: true
                    checked: index === backend.activeIndex
                    onTriggered: backend.selectConfig(index)
                }
                onObjectAdded: (i, obj) => tray.menu.insertItem(i + 5, obj)
                onObjectRemoved: (i, obj) => tray.menu.removeItem(obj)
            }
            Platform.MenuSeparator { visible: backend.configs.length > 0 }
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
        readonly property color bg: dark ? "#16181d" : "#eceef2"
        readonly property color surface: dark ? "#23262d" : "#e2e5ea"
        readonly property color tile: dark ? "#20232a" : "#d7dbe2"
        readonly property color inputBg: dark ? "#0e1014" : "#d6dae1" // darker than the card
        // A clearly visible outline for input fields (the plain border is too
        // faint against the light background).
        readonly property color inputBorder: dark ? "#363b45" : "#c2c7d0"
        readonly property color text: dark ? "#e8eaed" : "#1b1d21"
        readonly property color textDim: dark ? "#9aa1ab" : "#6b7280"
        readonly property color textFaint: dark ? "#6b7280" : "#9aa1ab"
        readonly property color accent: dark ? "#aeb6c2" : "#4b5563"
        readonly property color border: dark ? "#2d313a" : "#e7e9ee"
        // Off-state track for switches: clearly darker than the (light) accent
        // in dark mode so on/off don't blur together.
        readonly property color toggleOff: dark ? "#3a3f4a" : "#c4c8d0"
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

    // Render a portable shortcut ("Ctrl+Alt+T") with OS-native modifier glyphs.
    function keyGlyphs(seq) {
        if (!seq) return ""
        if (Qt.platform.os === "osx")
            return seq.replace(/Ctrl/g, "⌘").replace(/Meta/g, "⌃")
                      .replace(/Alt/g, "⌥").replace(/Shift/g, "⇧").replace(/\+/g, "")
        return seq
    }

    // ---------- main content (nav + page) ----------
    // Stays visible behind the create popup (dimmed by its backdrop).
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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

    // ---------- window-level select popup (used by Dropdown) ----------
    function showSelect(anchorItem, model, value, cb) {
        selectPopup.model = model
        selectPopup.value = value
        selectPopup.cb = cb
        // Anchor under the right edge of the value (so it opens where the choice is).
        var p = anchorItem.mapToItem(overlayLayer, anchorItem.width, anchorItem.height + 4)
        selectPopup.x = Math.max(8, Math.min(p.x - selectPopup.width, overlayLayer.width - selectPopup.width - 8))
        selectPopup.y = p.y
        selectPopup.open = true
    }
    Item {
        id: overlayLayer; anchors.fill: parent; z: 1500
        visible: selectPopup.open
        MouseArea { anchors.fill: parent; onClicked: selectPopup.open = false }
        Rectangle {
            id: selectPopup
            property bool open: false
            property var model: []
            property string value: ""
            property var cb: null
            visible: opacity > 0
            opacity: open ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 130 } }
            transform: Translate { y: selectPopup.open ? 0 : -8
                                   Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } } }
            width: 200
            height: spCol.implicitHeight + 10
            radius: 10; color: theme.bg; border.color: theme.border; border.width: 1
            Column {
                id: spCol; width: parent.width; y: 5
                Repeater {
                    model: selectPopup.model
                    Rectangle {
                        required property var modelData
                        width: parent.width; height: 38; radius: 6
                        color: spMa.containsMouse ? theme.surface : "transparent"
                        Text { anchors.verticalCenter: parent.verticalCenter; x: 12
                               text: parent.modelData.t
                               color: parent.modelData.v === selectPopup.value ? theme.accent : theme.text
                               font.pixelSize: 14 }
                        Text { visible: parent.modelData.v === selectPopup.value; text: "✓"; color: theme.accent
                               anchors.right: parent.right; rightPadding: 12; anchors.verticalCenter: parent.verticalCenter }
                        MouseArea { id: spMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { selectPopup.open = false; if (selectPopup.cb) selectPopup.cb(parent.modelData.v) } }
                    }
                }
            }
        }
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

    // Dropdown row that opens a floating select popup (like the import menu).
    // Only the value (right side) is the clickable/hover target.
    component Dropdown: Item {
        id: dd
        property string label: ""
        property var model: []     // [{ v: "...", t: "..." }]
        property string value: ""
        signal picked(string v)
        Layout.fillWidth: true
        Layout.preferredHeight: 42
        implicitHeight: 42
        function labelFor(v) {
            for (var i = 0; i < model.length; i++) if (model[i].v === v) return model[i].t
            return ""
        }
        RowLayout {
            anchors.fill: parent
            Text { text: dd.label; color: theme.text; font.pixelSize: 14 }
            Item { Layout.fillWidth: true }
            Row {
                id: ddVal; spacing: 4; height: parent.height
                Text { anchors.verticalCenter: parent.verticalCenter
                       text: dd.labelFor(dd.value)
                       color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 14 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "▾"
                       color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 16 }
                MouseArea { id: ddMa; anchors.fill: parent; hoverEnabled: true
                            onClicked: win.showSelect(ddVal, dd.model, dd.value, function(v){ dd.picked(v) }) }
            }
        }
    }

    // Reusable confirmation dialog (centered card + dimmed backdrop). Call
    // open(); emits confirmed() on the destructive action.
    component ConfirmDialog: Item {
        id: cd
        property string text: ""
        property string confirmText: qsTr("Delete")
        signal confirmed()
        anchors.fill: parent
        visible: false
        z: 2000
        function open() { visible = true }
        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
            MouseArea { anchors.fill: parent; onClicked: cd.visible = false } }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 56, 300)
            height: cdCol.implicitHeight + 36
            radius: 12; color: theme.bg; border.color: theme.border; border.width: 1
            Column {
                id: cdCol; width: parent.width - 36; anchors.centerIn: parent; spacing: 18
                Text { width: parent.width; wrapMode: Text.WordWrap; text: cd.text
                       color: theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
                Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 10
                    Rectangle { width: 110; height: 36; radius: 8
                        color: c1.containsMouse ? theme.border : theme.surface
                        Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                        MouseArea { id: c1; anchors.fill: parent; hoverEnabled: true; onClicked: cd.visible = false } }
                    Rectangle { width: 110; height: 36; radius: 8
                        color: c2.containsMouse ? Qt.darker(theme.danger, 1.15) : theme.danger
                        Text { anchors.centerIn: parent; text: cd.confirmText; color: "white"; font.pixelSize: 14 }
                        MouseArea { id: c2; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { cd.visible = false; cd.confirmed() } } }
                }
            }
        }
        Shortcut { sequences: ["Escape"]; enabled: cd.visible; onActivated: cd.visible = false }
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
                color: hk.capturing ? theme.infoBg : (hkMa.containsMouse ? theme.border : theme.surface)
                Behavior on color { ColorAnimation { duration: 120 } }
                border.width: hk.capturing ? 1 : 0; border.color: theme.accent
                Text {
                    id: lbl; anchors.centerIn: parent
                    text: hk.capturing ? qsTr("Press…") : (hk.value ? win.keyGlyphs(hk.value) : "—")
                    color: (hk.value || hk.capturing) ? theme.text : theme.textFaint
                    font.pixelSize: 13
                }
                MouseArea { id: hkMa; anchors.fill: parent; hoverEnabled: true
                            onClicked: { hk.capturing = true; hk.forceActiveFocus() } }
            }
            // Clear (disable) the binding.
            Text {
                visible: hk.value !== "" && !hk.capturing
                text: "✕"; color: clrMa.containsMouse ? theme.danger : theme.textDim
                font.pixelSize: 14; leftPadding: 8
                MouseArea { id: clrMa; anchors.fill: parent; hoverEnabled: true; onClicked: hk.captured("") }
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
                        property color ringColor: backend.connected ? theme.accent : theme.textFaint
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
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                        }
                        // Reserve the row height always so the logo doesn't jump
                        // when the timer appears/disappears; fade the text instead.
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            opacity: backend.connected ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                            text: backend.connected ? backend.sessionTime : "00:00:00"
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
                        model: [ { v: backend.downSpeed, c: theme.success, a: "↓" },
                                 { v: backend.upSpeed, c: theme.textDim, a: "↑" } ]
                        Rectangle {
                            required property var modelData
                            width: 116; height: 44; radius: 8; color: theme.tile
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
            }
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
        Item {
          Flickable {
            anchors.fill: parent
            contentHeight: scol.height; clip: true
            interactive: contentHeight > height // don't steal clicks from inputs when it fits
            ColumnLayout {
                id: scol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 18; anchors.rightMargin: 18
                spacing: 0
                Item { Layout.preferredHeight: 10 }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: qsTr("Split Tunneling"); color: theme.text; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.splitEnabled; onToggled: function(v){ backend.splitEnabled = v } }
                }
                Dropdown { label: qsTr("Mode"); value: backend.vpnMode
                    model: [{v:"general",t:qsTr("Bypass listed")},{v:"selective",t:qsTr("Only listed via VPN")}]
                    onPicked: function(v){ backend.vpnMode = v } }
                Item { Layout.preferredHeight: 6 }
                SectionLabel { text: qsTr("Profile") }
                Flow {
                    Layout.fillWidth: true; Layout.topMargin: 2; spacing: 6
                    Repeater {
                        model: backend.profiles
                        Rectangle {
                            id: chip
                            required property string modelData
                            property bool isActive: modelData === backend.activeProfile
                            property bool isDefault: modelData === "Default"
                            radius: 13; height: 28; implicitWidth: prow.width + 22
                            color: isActive ? theme.accent : (chipMa.containsMouse ? theme.border : theme.surface)
                            Behavior on color { ColorAnimation { duration: 120 } }
                            MouseArea { id: chipMa; anchors.fill: parent; hoverEnabled: true
                                        onClicked: backend.selectProfile(chip.modelData) }
                            Row { id: prow; anchors.centerIn: parent; spacing: 6
                                Text { text: chip.modelData
                                       color: chip.isActive ? "white" : theme.text; font.pixelSize: 13 }
                                Text { visible: !chip.isDefault; text: "×"
                                       color: chip.isActive ? "white" : theme.textDim; font.pixelSize: 14
                                       MouseArea { anchors.fill: parent
                                           onClicked: { delProfile.target = chip.modelData
                                               delProfile.text = qsTr("Delete profile “%1”?").arg(chip.modelData)
                                               delProfile.open() } } }
                            }
                        }
                    }
                    Rectangle {
                        radius: 13; height: 28; implicitWidth: 34
                        color: addChipMa.containsMouse ? theme.border : theme.surface
                        Behavior on color { ColorAnimation { duration: 120 } }
                        border.color: theme.border; border.width: 1
                        Text { anchors.centerIn: parent; text: "+"; color: theme.accent; font.pixelSize: 17 }
                        MouseArea { id: addChipMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: { npRow.visible = true; npInput.forceActiveFocus() } }
                    }
                }
                Rectangle {
                    id: npRow; visible: false
                    Layout.fillWidth: true; Layout.topMargin: 6; Layout.preferredHeight: 36; radius: 8
                    color: theme.inputBg; border.color: theme.accent; border.width: 1
                    TextInput {
                        id: npInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter; clip: true; font.pixelSize: 13; color: theme.text
                        onAccepted: { backend.addProfile(text); text = ""; npRow.visible = false }
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: qsTr("profile name, then Enter"); color: theme.textFaint; font.pixelSize: 13
                           visible: npInput.text.length === 0 }
                }
                Item { Layout.preferredHeight: 14 }
                RowLayout { Layout.fillWidth: true
                    SectionLabel { text: backend.vpnMode === "selective" ? qsTr("Rules — via VPN") : qsTr("Rules — bypass VPN") }
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("Clear all"); color: theme.danger; font.pixelSize: 12; visible: backend.domains.length > 0
                        MouseArea { anchors.fill: parent; onClicked: clearDomains.open() } }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    visible: backend.domains.length > 0
                    Layout.topMargin: visible ? 6 : 0
                    Layout.preferredHeight: visible ? implicitHeight : 0
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
                    Layout.topMargin: 6
                    color: theme.inputBg; border.color: domInput.activeFocus ? theme.accent : theme.inputBorder; border.width: 1
                    TextInput {
                        id: domInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter; clip: true
                        font.pixelSize: 13; color: theme.text
                        onAccepted: { backend.addDomain(text); text = "" }
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: qsTr("domain, IP or subnet, then Enter"); color: theme.textFaint; font.pixelSize: 13
                           visible: domInput.text.length === 0 && !domInput.activeFocus }
                }
                Item { Layout.preferredHeight: 16 }
            }
          }
          ConfirmDialog { id: delProfile; property string target: ""
              confirmText: qsTr("Delete"); onConfirmed: backend.removeProfile(target) }
          ConfirmDialog { id: clearDomains; text: qsTr("Clear all domains?")
              confirmText: qsTr("Clear"); onConfirmed: backend.clearDomains() }
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
                    Icon { anchors.centerIn: parent; width: 22; height: 22; svg: "qrc:/icons/speedometer.svg"; color: theme.accent }
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
                        Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                            Text { anchors.centerIn: parent; text: "⋯"; font.pixelSize: 20
                                   color: dotsMa.containsMouse ? theme.text : theme.textDim }
                            MouseArea { id: dotsMa; anchors.fill: parent; hoverEnabled: true
                                        onClicked: { win.editIndex = index; win.overlay = "create" } } }
                        Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                            Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 17
                                   color: delMa.containsMouse ? theme.danger : theme.textDim }
                            MouseArea { id: delMa; anchors.fill: parent; hoverEnabled: true
                                        onClicked: { delCfg.target = index
                                            delCfg.text = qsTr("Delete config “%1”?").arg(modelData)
                                            delCfg.open() } } }
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
            ConfirmDialog { id: delCfg; property int target: -1
                confirmText: qsTr("Delete"); onConfirmed: if (target >= 0) backend.removeConfig(target) }
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
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Text { text: qsTr("Launch at system startup"); color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true } Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.autoStart; onToggled: function(v){ backend.autoStart = v } } }
                Sep {}
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42; Text { text: qsTr("Connect on startup"); color: theme.text; font.pixelSize: 14 } Item { Layout.fillWidth: true }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.autoConnect; onToggled: function(v){ backend.autoConnect = v } } }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: qsTr("Security") }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: "Kill switch"; color: theme.text; font.pixelSize: 14 }
                    Text { text: qsTr("block traffic outside the VPN"); color: theme.textFaint; font.pixelSize: 12; leftPadding: 6 }
                    Item { Layout.fillWidth: true }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.killSwitch; onToggled: function(v){ backend.killSwitch = v } } }
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
                    Rectangle { anchors.fill: parent; anchors.topMargin: 2; anchors.bottomMargin: 2; radius: 6
                        color: updMa.containsMouse ? theme.surface : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } } }
                    Text { text: qsTr("Check for updates")
                           color: updMa.containsMouse ? theme.accent : theme.text; font.pixelSize: 14 }
                    Text { visible: backend.updateMessage.length > 0; text: backend.updateMessage
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 12; leftPadding: 8 }
                    Item { Layout.fillWidth: true }
                    Text { text: backend.updateState === "checking" ? "…"
                                 : backend.updateState === "available" ? qsTr("Download ›") : backend.appVersion
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 13 }
                    MouseArea { id: updMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: backend.updateState === "available" ? backend.openLatestRelease()
                                                                       : backend.checkForUpdates() } }
                Item { Layout.preferredHeight: 14 }
                // Footer: project names link to their repos.
                Row { Layout.fillWidth: true; Layout.alignment: Qt.AlignHCenter; spacing: 0
                    Text { text: "FreeTunnel " + backend.appVersion; font.pixelSize: 12
                           color: ftMa.containsMouse ? theme.accent : theme.textFaint
                           MouseArea { id: ftMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                               onClicked: backend.openUrl("https://github.com/enrvate/freetunnel") } }
                    Text { text: "  ·  "; color: theme.textFaint; font.pixelSize: 12 }
                    Text { text: qsTr("TrustTunnel core ") + backend.coreVersion; font.pixelSize: 12
                           color: ttMa.containsMouse ? theme.accent : theme.textFaint
                           MouseArea { id: ttMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                               onClicked: backend.openUrl("https://github.com/TrustTunnel/TrustTunnelClient") } }
                }
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
                Rectangle { Layout.preferredHeight: 28; Layout.preferredWidth: clrTxt.implicitWidth + 24; radius: 7
                    color: clrMa.containsMouse ? theme.surface : "transparent"
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Text { id: clrTxt; anchors.centerIn: parent; text: qsTr("Clear")
                           color: clrMa.containsMouse ? theme.text : theme.accent; font.pixelSize: 13 }
                    MouseArea { id: clrMa; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor; onClicked: backend.clearLogs() } }
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
                Text { Layout.fillWidth: true; text: backend.logPath; font.pixelSize: 11
                       color: logPathMa.containsMouse ? theme.accent : theme.textDim
                       font.underline: logPathMa.containsMouse; elide: Text.ElideMiddle
                       MouseArea { id: logPathMa; anchors.fill: parent; hoverEnabled: true
                                   cursorShape: Qt.PointingHandCursor; onClicked: backend.openLogFolder() } }
                Text { text: qsTr("Auto-scroll"); color: theme.textDim; font.pixelSize: 12 }
                Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: logList.autoScroll; implicitWidth: 34; implicitHeight: 20
                         onToggled: function(v){ logList.autoScroll = v } }
            }
        }
        }
    }

    // ===================== Create config (sub-screen) =====================
    Component {
        id: createConfig
        Item {
            anchors.fill: parent
            // Dimmed backdrop — the main UI shows through; click to close.
            Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
                MouseArea { anchors.fill: parent; onClicked: cform.tryClose() } }
            Shortcut { sequences: ["Escape"]; enabled: !discardConfirm.visible; onActivated: cform.tryClose() }

            Rectangle {
            id: cform
            anchors.centerIn: parent
            width: Math.min(parent.width - 28, 380)
            height: Math.min(parent.height - 36, fcol.implicitHeight + chdr.height + 24)
            radius: 14; color: theme.bg; border.color: theme.border; border.width: 1
            property string protocol: "http2"
            property bool ipv6: true
            property string snap: ""
            readonly property bool editing: win.editIndex >= 0
            function snapshot() {
                return [fName.text, fHost.text, fAddr.text, fUser.text, fPass.text,
                        protocol, fDns.text, fSni.text, fRandom.text, fCert.text, ipv6].join("")
            }
            function tryClose() { if (snapshot() !== snap) discardConfirm.open(); else close() }
            function close() { win.editIndex = -1; win.overlay = "" }
            // Prefill from the selected config when editing; snapshot for dirty-check.
            Component.onCompleted: {
                if (editing) {
                    var f = backend.configFields(win.editIndex)
                    fName.text = f.name || ""; fHost.text = f.hostname || ""
                    fAddr.text = f.addresses || ""; fUser.text = f.username || ""
                    fPass.text = f.password || ""; fDns.text = f.dns || ""
                    fSni.text = f.customSni || ""; fRandom.text = f.clientRandom || ""
                    fCert.text = f.certificate || ""
                    cform.protocol = f.protocol === "http3" ? "http3" : "http2"
                    cform.ipv6 = f.allowIpv6 === undefined ? true : f.allowIpv6
                }
                snap = snapshot()
            }
            Item {
                id: chdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: 48
                Text { id: cBack; anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                       text: "←"; color: cBackMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 20
                       MouseArea { id: cBackMa; anchors.fill: parent; hoverEnabled: true; onClicked: cform.tryClose() } }
                Text { anchors.left: cBack.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                       text: cform.editing ? qsTr("Edit config") : qsTr("New config"); color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
            }
            Flickable {
                anchors.top: chdr.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.leftMargin: 18; anchors.rightMargin: 18; contentHeight: fcol.height; clip: true
                Column {
                    id: fcol; width: parent.width; spacing: 10
                    Field { id: fName; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Name"); placeholder: qsTr("Germany · Frankfurt") }
                    Field { id: fHost; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Server host"); placeholder: "frankfurt.example.com" }
                    Field { id: fAddr; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Address(es) · host:port (comma-separated)"); placeholder: "1.2.3.4:443" }
                    Row { width: parent.width; spacing: 10
                        Field { id: fUser; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Username"); width: (parent.width - 10) / 2 }
                        Field { id: fPass; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Password"); password: true; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; spacing: 10
                        Column { width: (parent.width - 10) / 2; spacing: 4
                            Text { text: qsTr("Protocol"); color: theme.textDim; font.pixelSize: 13 }
                            Rectangle { id: protoBox; width: parent.width; height: 34; radius: 8; color: theme.inputBg
                                border.color: protoMa.containsMouse ? theme.accent : theme.inputBorder; border.width: 1
                                Behavior on border.color { ColorAnimation { duration: 120 } }
                                Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                       text: cform.protocol === "http3" ? "HTTP/3" : "HTTP/2"; color: theme.text; font.pixelSize: 14 }
                                Text { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                       text: "▾"; color: theme.textDim; font.pixelSize: 16 }
                                MouseArea { id: protoMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: win.showSelect(protoBox,
                                        [{v:"http2",t:"HTTP/2"},{v:"http3",t:"HTTP/3"}],
                                        cform.protocol, function(v){ cform.protocol = v }) } }
                        }
                        Field { id: fDns; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("DNS servers"); placeholder: "1.1.1.1, 8.8.8.8"; width: (parent.width - 10) / 2 }
                    }
                    Row { width: parent.width; spacing: 10
                        Field { id: fSni; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: "Custom SNI"; width: (parent.width - 10) / 2 }
                        Field { id: fRandom; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: "Client random (hex)"; width: (parent.width - 10) / 2 }
                    }
                    Item { width: parent.width; height: 32
                        Text { text: qsTr("Allow IPv6"); color: theme.text; font.pixelSize: 14
                               anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                        Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: cform.ipv6
                                 anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                 onToggled: function(v){ cform.ipv6 = v } }
                    }
                    Column { width: parent.width; spacing: 4
                        Text { text: qsTr("Certificate (PEM) · optional"); color: theme.textDim; font.pixelSize: 13 }
                        Rectangle { width: parent.width; height: 70; radius: 8; color: theme.inputBg; border.color: fCert.activeFocus ? theme.accent : theme.inputBorder; border.width: 1
                            Flickable { anchors.fill: parent; anchors.margins: 8; contentHeight: fCert.height; clip: true
                                TextEdit { id: fCert; width: parent.width; font.pixelSize: 12; font.family: "Menlo"; color: theme.text; wrapMode: TextEdit.WrapAnywhere } } }
                    }
                    Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 10; topPadding: 4; bottomPadding: 16
                        Rectangle { width: 110; height: 36; radius: 8
                            color: saveMa.containsMouse ? Qt.darker(theme.accent, 1.12) : theme.accent
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Text { anchors.centerIn: parent; text: qsTr("Save"); color: "white"; font.pixelSize: 14 }
                            MouseArea { id: saveMa; anchors.fill: parent; hoverEnabled: true; onClicked: {
                                var ok = backend.createConfig({
                                    name: fName.text, hostname: fHost.text, addresses: fAddr.text,
                                    username: fUser.text, password: fPass.text, protocol: cform.protocol,
                                    dns: fDns.text, customSni: fSni.text, clientRandom: fRandom.text,
                                    allowIpv6: cform.ipv6, certificate: fCert.text, editIndex: win.editIndex });
                                if (ok) cform.close()
                            } } }
                        Rectangle { width: 90; height: 36; radius: 8
                            color: cancelMa.containsMouse ? theme.surface : theme.bg; border.color: theme.border; border.width: 1
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                            MouseArea { id: cancelMa; anchors.fill: parent; hoverEnabled: true; onClicked: cform.tryClose() } }
                    }
                }
            }
            }
            ConfirmDialog {
                id: discardConfirm
                text: qsTr("Discard unsaved changes?")
                confirmText: qsTr("Discard")
                onConfirmed: cform.close()
            }
        }
    }

}
