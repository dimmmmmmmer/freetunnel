import QtQuick
import QtQuick.Layouts
import Qt.labs.platform as Platform

// FreeTunnel main window: centered top nav + pages, plus back-arrow sub-screens.
// Consumes a `backend` context object (mock in preview; real app injects VPN).
Window {
    id: win
    visible: true
    // Default to the smallest comfortable size.
    width: 360
    height: 520
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
        // Bright mark when connected, dimmed when off.
        icon.source: backend.connected ? "qrc:/assets/logo.svg" : "qrc:/assets/logo-dim.svg"
        tooltip: backend.connected ? qsTr("FreeTunnel — %1").arg(backend.activeConfig)
                                    : "FreeTunnel"
        // Click opens the menu (below). Double-click brings the window forward.
        onActivated: function(reason) {
            if (reason === Platform.SystemTrayIcon.DoubleClick) {
                win.show(); win.raise(); win.requestActivate()
            }
        }
        menu: Platform.Menu {
            // Plain connect/disconnect action button.
            Platform.MenuItem {
                text: backend.connecting ? qsTr("Connecting…")
                      : backend.connected ? qsTr("Disconnect") : qsTr("Connect")
                enabled: backend.configs.length > 0
                onTriggered: backend.toggle()
            }
            // Active config + session time on one line (only while connected).
            Platform.MenuItem {
                enabled: false; visible: backend.connected
                text: backend.activeConfig + "  ·  " + backend.sessionTime
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
                onObjectAdded: (i, obj) => tray.menu.insertItem(i + 3, obj)
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
        // Pure neutral gray ramp — true monochrome (R = G = B), no tint.
        readonly property color bg: dark ? "#181818" : "#ececec"
        readonly property color surface: dark ? "#262626" : "#e2e2e2"
        readonly property color tile: dark ? "#202020" : "#d8d8d8"
        readonly property color inputBg: dark ? "#101010" : "#d6d6d6" // darker than the card
        // A clearly visible outline for input fields (the plain border is too
        // faint against the light background).
        readonly property color inputBorder: dark ? "#3a3a3a" : "#c2c2c2"
        readonly property color text: dark ? "#eaeaea" : "#1b1b1b"
        readonly property color textDim: dark ? "#9a9a9a" : "#6b6b6b"
        readonly property color textFaint: dark ? "#6a6a6a" : "#9a9a9a"
        readonly property color accent: dark ? "#b0b0b0" : "#4f4f4f"
        readonly property color border: dark ? "#2e2e2e" : "#e5e5e5"
        // Off-state track for switches: clearly darker than the (light) accent
        // in dark mode so on/off don't blur together.
        readonly property color toggleOff: dark ? "#3a3a3a" : "#c4c4c4"
        readonly property color success: dark ? "#3fbf93" : "#1d9e75"
        readonly property color warn: dark ? "#d99634" : "#ba7517"
        readonly property color danger: dark ? "#e06a6a" : "#a32d2d"
        readonly property color infoBg: dark ? Qt.rgba(0.69, 0.69, 0.69, 0.16)
                                             : Qt.rgba(0.31, 0.31, 0.31, 0.12)
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

    // Truncate a long string with an ellipsis (used in confirm messages).
    function elide(s, n) { return s.length > n ? s.substring(0, n - 1) + "…" : s }

    // Render a portable shortcut ("Ctrl+Alt+T") with OS-native modifier glyphs.
    function keyGlyphs(seq) {
        if (!seq) return ""
        if (Qt.platform.os === "osx")
            return seq.replace(/Ctrl/g, "⌘").replace(/Meta/g, "⌃")
                      .replace(/Alt/g, "⌥").replace(/Shift/g, "⇧").replace(/\+/g, "")
        return seq
    }

    // Neutral focus target: clicking empty space moves focus here, blurring any
    // text field that was being edited.
    Item { id: focusSink }
    // Backmost catcher (declared first → behind everything): a press on empty
    // background clears text-field focus.
    MouseArea {
        anchors.fill: parent
        onPressed: function(m) { focusSink.forceActiveFocus(); m.accepted = false }
    }

    // ---------- title-bar drag region ----------
    // Sits above the back-catcher but below the nav buttons; catches presses on
    // the empty top band and starts a native window move.
    MouseArea {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: Qt.platform.os === "osx" ? 70 : 52
        onPressed: backend.startWindowDrag(win)
    }

    // ---------- main content (nav + page) ----------
    // Stays visible behind the create popup (dimmed by its backdrop).
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            // Leave room for the macOS traffic-light buttons (the title bar is
            // transparent and the content flows under it).
            Layout.topMargin: Qt.platform.os === "osx" ? 26 : 12
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
                    color: active ? theme.infoBg : (nma.containsMouse ? theme.surface : theme.bg)
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
        radius: 9; color: theme.surface; border.color: theme.border; border.width: 1
        opacity: 0; visible: opacity > 0
        Text {
            id: tmsg; anchors.centerIn: parent; width: toast.width - 24
            text: toast.message; color: theme.text; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
        }
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Timer { id: toastTimer; interval: 3200; onTriggered: toast.opacity = 0 }
        MouseArea { anchors.fill: parent; onClicked: toast.opacity = 0 }
    }

    // ---------- window-level select popup (used by Dropdown) ----------
    TextMetrics { id: spMetrics; font.pixelSize: 14 }
    function showSelect(anchorItem, model, value, cb) {
        selectPopup.model = model
        selectPopup.value = value
        selectPopup.cb = cb
        // Size to the widest option (+ room for the left pad and check mark),
        // clamped to the window so it never spills off the edge.
        var w = 140
        for (var i = 0; i < model.length; i++) {
            spMetrics.text = model[i].t
            w = Math.max(w, spMetrics.advanceWidth + 56)
        }
        selectPopup.width = Math.min(w, overlayLayer.width - 16)
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
        Shortcut { sequence: "Escape"; enabled: selectPopup.open; onActivated: selectPopup.open = false }
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
            height: spCol.implicitHeight + 12
            radius: 10; color: theme.bg; border.color: theme.border; border.width: 1
            Column {
                id: spCol; width: parent.width - 10; x: 5; y: 6
                Repeater {
                    model: selectPopup.model
                    Rectangle {
                        required property var modelData
                        width: parent.width; height: 36; radius: 6
                        color: spMa.containsMouse ? theme.surface : theme.bg
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

    // ---------- window-level confirm dialog (covers the whole window) --------
    property var confirmCb: null
    function showConfirm(message, confirmLabel, cb) {
        winConfirm.text = message
        winConfirm.confirmText = confirmLabel
        win.confirmCb = cb
        winConfirm.open()
    }
    ConfirmDialog { id: winConfirm; z: 2500
        onConfirmed: if (win.confirmCb) win.confirmCb() }

    // shared bits ------------------------------------------------------------
    component SectionLabel: Text {
        color: theme.textFaint; font.pixelSize: 12
        leftPadding: 0; bottomPadding: 4
    }
    component Sep: Rectangle { Layout.preferredHeight: 1; color: theme.border; Layout.fillWidth: true }

    // Reusable chip delete "✕": tightly centred, red on hover, generous hit area.
    component ChipX: Item {
        id: cx
        property bool onAccent: false
        signal clicked()
        implicitWidth: 15; implicitHeight: 15
        Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 14
               color: cxMa.containsMouse ? theme.danger : (cx.onAccent ? "white" : theme.textDim) }
        MouseArea { id: cxMa; anchors.fill: parent; anchors.margins: -6
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: cx.clicked() }
    }

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
            // Plain value + arrow (no box); brightens on hover.
            Item {
                id: ddVal; Layout.preferredWidth: ddRow.width; Layout.fillHeight: true
                Row { id: ddRow; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Text { anchors.verticalCenter: parent.verticalCenter
                           text: dd.labelFor(dd.value)
                           color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 14 }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "▾"
                           color: ddMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 16 }
                }
                MouseArea { id: ddMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
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
            // Fit the text (names are elided by callers) — no wider than needed.
            width: Math.min(parent.width - 56, Math.max(196, cdText.implicitWidth + 36))
            height: cdCol.implicitHeight + 24
            radius: 12; color: theme.bg; border.color: theme.border; border.width: 1
            Column {
                id: cdCol; width: parent.width - 28; anchors.centerIn: parent; spacing: 14
                Text { id: cdText; width: parent.width; wrapMode: Text.WordWrap; text: cd.text
                       color: theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
                Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 8
                    Rectangle { width: Math.max(76, c1t.implicitWidth + 26); height: 32; radius: 8
                        color: c1.containsMouse ? theme.border : theme.surface
                        Text { id: c1t; anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                        MouseArea { id: c1; anchors.fill: parent; hoverEnabled: true; onClicked: cd.visible = false } }
                    Rectangle { width: Math.max(76, c2t.implicitWidth + 26); height: 32; radius: 8
                        color: c2.containsMouse ? Qt.darker(theme.danger, 1.15) : theme.danger
                        Text { id: c2t; anchors.centerIn: parent; text: cd.confirmText; color: "white"; font.pixelSize: 14 }
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
        }
        Keys.onPressed: function(e) {
            if (!hk.capturing) return
            e.accepted = true
            if (e.key === Qt.Key_Escape) { hk.capturing = false; return }
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
                // The logo IS the connect button. Centered when off; on connect
                // it eases upward and the session timer fades in below it.
                Item {
                    id: hero
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 200; Layout.preferredHeight: 200
                    Image {
                        id: heroLogo
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: "qrc:/assets/logo.svg"; width: 132; height: 132
                        sourceSize: Qt.size(264, 264)
                        opacity: backend.connected ? 1.0 : (backend.connecting ? 0.7 : 0.5)
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                        y: (backend.connected || backend.connecting) ? hero.height / 2 - height / 2 - 24
                                                                      : hero.height / 2 - height / 2
                        Behavior on y { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                        // Subtle breathing while connecting — driven by scale, not
                        // opacity, so it never fights the opacity binding (which
                        // caused a flash on connect).
                        scale: (heroMa.pressed ? 0.96 : 1.0) * (backend.connecting ? pulse.value : 1.0)
                        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        QtObject {
                            id: pulse; property real value: 1.0
                            // (animated below)
                        }
                        SequentialAnimation {
                            running: backend.connecting; loops: Animation.Infinite
                            NumberAnimation { target: pulse; property: "value"; to: 1.05; duration: 750; easing.type: Easing.InOutSine }
                            NumberAnimation { target: pulse; property: "value"; to: 0.97; duration: 750; easing.type: Easing.InOutSine }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: heroLogo.bottom; anchors.topMargin: 8
                        opacity: (backend.connected || backend.connecting) ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 220 } }
                        text: backend.connecting ? qsTr("Connecting…")
                              : (backend.connected ? backend.sessionTime : "")
                        color: theme.textDim
                        font.pixelSize: backend.connecting ? 14 : 18; font.weight: Font.Medium
                    }
                    MouseArea { id: heroMa; anchors.fill: parent; onClicked: backend.toggle() }
                }
                Item {
                    id: cfgSel
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: cfgRow.width; implicitHeight: cfgRow.height
                    Row { id: cfgRow; anchors.centerIn: parent; spacing: 6
                        Text { anchors.verticalCenter: parent.verticalCenter
                               text: backend.configs.length > 0 ? backend.activeConfig : qsTr("Add a config")
                               width: Math.min(implicitWidth, 260); elide: Text.ElideRight
                               color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                        Text { anchors.verticalCenter: parent.verticalCenter
                               visible: backend.configs.length > 0; text: "▾"; color: theme.textDim; font.pixelSize: 15 }
                    }
                    MouseArea { anchors.fill: parent
                        onClicked: {
                            if (backend.configs.length === 0) { win.currentPage = 1; return }
                            if (!cfgPopup.open) {
                                // Position under the selector at click time (a mapToItem
                                // binding wouldn't track the centered layout).
                                var p = cfgSel.mapToItem(homeRoot, 0, 0)
                                cfgPopup.x = p.x + cfgSel.width / 2 - cfgPopup.width / 2
                                cfgPopup.y = p.y + cfgSel.height + 6
                            }
                            cfgPopup.open = !cfgPopup.open
                        }
                    }
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
                height: picker.height + addRow.height + 22 // rows + separator + margins
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
                    Item { width: parent.width; height: 11
                        Rectangle { anchors.centerIn: parent; width: parent.width - 16; height: 1; color: theme.border } }
                    Rectangle {
                        id: addRow; width: parent.width; height: 40; radius: 6
                        color: ama.containsMouse ? theme.surface : theme.bg
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
          id: splitRoot
          // Clicking empty page area clears focus from a text field.
          TapHandler { onTapped: splitRoot.forceActiveFocus() }
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
                    model: [{v:"general",t:qsTr("Bypass VPN")},{v:"selective",t:qsTr("Through VPN")}]
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
                            radius: 13; height: 28
                            implicitWidth: plabel.width + (chip.isDefault ? 22 : 42)
                            color: isActive ? theme.accent : (chipMa.containsMouse ? theme.border : theme.surface)
                            Behavior on color { ColorAnimation { duration: 120 } }
                            MouseArea { id: chipMa; anchors.fill: parent; hoverEnabled: true
                                        onClicked: backend.selectProfile(chip.modelData) }
                            Text { id: plabel; anchors.left: parent.left; anchors.leftMargin: 11
                                   anchors.verticalCenter: parent.verticalCenter; text: chip.modelData
                                   width: Math.min(implicitWidth, 130); elide: Text.ElideRight
                                   color: chip.isActive ? "white" : theme.text; font.pixelSize: 13 }
                            ChipX { visible: !chip.isDefault; onAccent: chip.isActive
                                    anchors.left: plabel.right; anchors.leftMargin: 5
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: win.showConfirm(qsTr("Delete profile “%1”?").arg(win.elide(chip.modelData, 24)),
                                        qsTr("Delete"), function(){ backend.removeProfile(chip.modelData) }) }
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
                        maximumLength: 16
                        onAccepted: { backend.addProfile(text); text = ""; npRow.visible = false }
                        Keys.onEscapePressed: { text = ""; npRow.visible = false }
                        // Collapse the field when it loses focus (e.g. clicking elsewhere).
                        onActiveFocusChanged: if (!activeFocus) { text = ""; npRow.visible = false }
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: qsTr("profile name, then Enter"); color: theme.textFaint; font.pixelSize: 13
                           visible: npInput.text.length === 0 }
                }
                Item { Layout.preferredHeight: 14 }
                RowLayout { Layout.fillWidth: true; spacing: 10
                    SectionLabel { Layout.fillWidth: true; elide: Text.ElideRight
                        text: backend.vpnMode === "selective" ? qsTr("Rules — via VPN") : qsTr("Rules — bypass VPN") }
                    Text { text: qsTr("Recommended for Russia"); font.pixelSize: 12
                           color: recMa.containsMouse ? theme.text : theme.accent; font.underline: recMa.containsMouse
                        MouseArea { id: recMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: backend.addRecommendedRussia() } }
                    Text { text: qsTr("Clear all"); font.pixelSize: 12; visible: backend.domains.length > 0
                           color: clrDomMa.containsMouse ? Qt.lighter(theme.danger, 1.25) : theme.danger
                           font.underline: clrDomMa.containsMouse
                        MouseArea { id: clrDomMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: win.showConfirm(qsTr("Clear all domains?"),
                            qsTr("Clear"), function(){ backend.clearDomains() }) } }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    visible: backend.domains.length > 0
                    Layout.topMargin: visible ? 6 : 0
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    Repeater {
                        model: backend.domains
                        Rectangle {
                            id: domChip
                            required property string modelData
                            required property int index
                            radius: 13; color: theme.surface
                            implicitWidth: dlabel.width + 37; implicitHeight: 28
                            Text { id: dlabel; anchors.left: parent.left; anchors.leftMargin: 11
                                   anchors.verticalCenter: parent.verticalCenter; text: domChip.modelData
                                   width: Math.min(implicitWidth, 190); elide: Text.ElideRight
                                   color: theme.text; font.pixelSize: 13 }
                            ChipX { anchors.left: dlabel.right; anchors.leftMargin: 5
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: backend.removeDomain(domChip.index) }
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
                        onAccepted: { if (backend.addDomain(text)) text = "" }
                        Keys.onEscapePressed: focus = false
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           anchors.right: parent.right; anchors.rightMargin: 12; elide: Text.ElideRight
                           text: qsTr("domain or domains (comma/space separated), then Enter"); color: theme.textFaint; font.pixelSize: 13
                           visible: domInput.text.length === 0 && !domInput.activeFocus }
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.IBeamCursor }
                }
                Item { Layout.preferredHeight: 16 }
            }
          }
        }
    }

    // ===================== Configs =====================
    Component {
        id: configsPage
        Item {
            // Cmd/Ctrl+V tries to import a config from the clipboard.
            Shortcut { sequences: [StandardKey.Paste]; onActivated: backend.importFromClipboard() }
            // Header: Add (+) opens the import/create menu, Ping (speedometer).
            RowLayout {
                id: cfgHdr
                anchors.top: parent.top; anchors.topMargin: 8
                anchors.horizontalCenter: parent.horizontalCenter; spacing: 16
                Rectangle {
                    width: 40; height: 32; radius: 8
                    color: addMa.containsMouse ? theme.surface : theme.bg
                    Behavior on color { ColorAnimation { duration: 120 } }
                    // Drawn plus (same size/weight as the speedometer icon, perfectly centred).
                    Item { anchors.centerIn: parent; width: 22; height: 22
                        Rectangle { anchors.centerIn: parent; width: 16; height: 2.2; radius: 1.1; color: theme.accent }
                        Rectangle { anchors.centerIn: parent; width: 2.2; height: 16; radius: 1.1; color: theme.accent }
                    }
                    MouseArea { id: addMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: importMenu.open = !importMenu.open }
                }
                Rectangle {
                    visible: backend.configs.length > 0
                    width: 40; height: 32; radius: 8
                    color: pingMa.containsMouse ? theme.surface : theme.bg
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Icon { anchors.centerIn: parent; width: 22; height: 22; svg: "qrc:/icons/speedometer.svg"; color: theme.accent }
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
                delegate: Item {
                    required property int index
                    required property string modelData
                    width: cfgList.width; height: 56
                    Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: 4; radius: 8
                        // Stay highlighted as a single block even when the cursor is
                        // over the ⋯/✕ buttons (their hover would otherwise drop it).
                        color: (rowMa.containsMouse || dotsMa.containsMouse || delMa.containsMouse)
                               ? theme.surface : theme.bg
                        Behavior on color { ColorAnimation { duration: 120 } } }
                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: backend.selectConfig(index) }
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
                            Text { anchors.centerIn: parent; text: "⋯"; font.pixelSize: 20
                                   color: dotsMa.containsMouse ? theme.text : theme.textDim }
                            MouseArea { id: dotsMa; anchors.fill: parent; hoverEnabled: true
                                        onClicked: { win.editIndex = index; win.overlay = "create" } } }
                        Item { Layout.preferredWidth: 30; Layout.fillHeight: true
                            Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 17
                                   color: delMa.containsMouse ? theme.danger : theme.textDim }
                            MouseArea { id: delMa; anchors.fill: parent; hoverEnabled: true
                                        onClicked: win.showConfirm(qsTr("Delete config “%1”?").arg(modelData),
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
                        onTriggered: { importMenu.open = false; win.editIndex = -1; win.overlay = "create" } }
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
        Item {
          id: settingsRoot
          TapHandler { onTapped: settingsRoot.forceActiveFocus() }
          Flickable {
            anchors.fill: parent
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

                // ----- Excluded routes (subnets that bypass the tunnel) -----
                RowLayout { Layout.fillWidth: true; spacing: 10
                    SectionLabel { Layout.fillWidth: true; elide: Text.ElideRight; text: qsTr("Excluded routes") }
                    Text { text: qsTr("Restore defaults"); font.pixelSize: 12
                           color: rdMa.containsMouse ? theme.text : theme.accent; font.underline: rdMa.containsMouse
                        MouseArea { id: rdMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: backend.restoreDefaultExcludedRoutes() } }
                    Text { text: qsTr("Clear all"); font.pixelSize: 12
                        visible: backend.excludedRoutes.length > 0
                        color: clrRtMa.containsMouse ? Qt.lighter(theme.danger, 1.25) : theme.danger
                        font.underline: clrRtMa.containsMouse
                        MouseArea { id: clrRtMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: win.showConfirm(qsTr("Clear all excluded routes?"),
                            qsTr("Clear"), function(){ backend.clearExcludedRoutes() }) } }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    visible: backend.excludedRoutes.length > 0
                    Layout.topMargin: visible ? 6 : 0
                    Repeater {
                        model: backend.excludedRoutes
                        Rectangle {
                            id: rtChip
                            required property string modelData
                            required property int index
                            radius: 13; color: theme.surface
                            implicitWidth: rlabel.width + 37; implicitHeight: 28
                            Text { id: rlabel; anchors.left: parent.left; anchors.leftMargin: 11
                                   anchors.verticalCenter: parent.verticalCenter; text: rtChip.modelData
                                   width: Math.min(implicitWidth, 190); elide: Text.ElideRight
                                   color: theme.text; font.pixelSize: 13 }
                            ChipX { anchors.left: rlabel.right; anchors.leftMargin: 5
                                    anchors.verticalCenter: parent.verticalCenter
                                    onClicked: backend.removeExcludedRoute(rtChip.index) }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 36; radius: 8; Layout.topMargin: 6
                    color: theme.inputBg; border.color: rtInput.activeFocus ? theme.accent : theme.inputBorder; border.width: 1
                    TextInput {
                        id: rtInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter; clip: true
                        font.pixelSize: 13; color: theme.text
                        onAccepted: { if (backend.addExcludedRoute(text)) text = "" }
                        Keys.onEscapePressed: focus = false
                    }
                    Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                           text: qsTr("IP or subnet (e.g. 10.0.0.0/8), then Enter"); color: theme.textFaint; font.pixelSize: 13
                           visible: rtInput.text.length === 0 && !rtInput.activeFocus }
                }
                Item { Layout.preferredHeight: 16 }

                SectionLabel { text: qsTr("Hotkeys") }
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { text: qsTr("Enable"); color: theme.text; font.pixelSize: 14 }
                    Item { Layout.fillWidth: true }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.hotkeysEnabled
                             onToggled: function(v){ backend.hotkeysEnabled = v } } }
                // The shortcut fields dim out while the feature is off.
                Item { Layout.fillWidth: true; implicitHeight: hkCol.implicitHeight
                    opacity: backend.hotkeysEnabled ? 1.0 : 0.4
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                    enabled: backend.hotkeysEnabled
                    ColumnLayout { id: hkCol; anchors.left: parent.left; anchors.right: parent.right; spacing: 0
                        HotkeyField { label: qsTr("Toggle VPN"); value: backend.hotkeyToggle
                            onCaptured: function(s){ backend.hotkeyToggle = s } }
                        Sep {}
                        HotkeyField { label: qsTr("Connect"); value: backend.hotkeyConnect
                            onCaptured: function(s){ backend.hotkeyConnect = s } }
                        Sep {}
                        HotkeyField { label: qsTr("Disconnect"); value: backend.hotkeyDisconnect
                            onCaptured: function(s){ backend.hotkeyDisconnect = s } }
                    }
                }
                Item { Layout.preferredHeight: 16 }
                SectionLabel { text: qsTr("Maintenance") }
                Item { Layout.fillWidth: true; Layout.preferredHeight: 42
                    Text { id: updTxt; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                           text: qsTr("Check for updates"); font.pixelSize: 14
                           color: updMa.containsMouse ? theme.accent : theme.text
                           font.underline: updMa.containsMouse }
                    Text { id: updStatus; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                           text: backend.updateState === "checking" ? "…"
                                 : backend.updateState === "available" ? qsTr("Download ›") : backend.appVersion
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint
                           font.pixelSize: 13 }
                    Text { anchors.left: updTxt.right; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                           anchors.right: updStatus.left; anchors.rightMargin: 8; elide: Text.ElideRight
                           visible: backend.updateMessage.length > 0; text: backend.updateMessage
                           horizontalAlignment: Text.AlignRight
                           color: backend.updateState === "available" ? theme.accent : theme.textFaint; font.pixelSize: 12 }
                    MouseArea { id: updMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: backend.updateState === "available" ? backend.openLatestRelease()
                                                                       : backend.checkForUpdates() } }
                Item { Layout.preferredHeight: 14 }
                // Footer: project names link to their repos.
                Row { Layout.alignment: Qt.AlignHCenter; spacing: 0
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
    }

    // ===================== Logs =====================
    Component {
        id: logsPage
        Item {
        ColumnLayout {
            anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18
            anchors.topMargin: 6; anchors.bottomMargin: 12; spacing: 8
            Item { Layout.fillWidth: true; Layout.preferredHeight: 24
                Text { id: clrTxt; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                       text: qsTr("Clear"); font.pixelSize: 13
                       color: clrMa.containsMouse ? theme.text : theme.accent
                       font.underline: clrMa.containsMouse }
                MouseArea { id: clrMa; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            width: clrTxt.implicitWidth + 8; height: 22; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: backend.clearLogs() }
                Text { id: cpyTxt; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                       text: qsTr("Copy"); font.pixelSize: 13
                       color: cpyMa.containsMouse ? theme.text : theme.accent
                       font.underline: cpyMa.containsMouse }
                MouseArea { id: cpyMa; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            width: cpyTxt.implicitWidth + 8; height: 22; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { backend.copyToClipboard(backend.logText()); toast.show(qsTr("Log copied")) } }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: theme.surface
                clip: true
                Text {
                    anchors.centerIn: parent; visible: backend.logEntries.length === 0
                    text: qsTr("Logs will appear after connecting"); color: theme.textFaint; font.pixelSize: 13
                }
                Flickable {
                    id: logFlick
                    anchors.fill: parent; anchors.margins: 12; clip: true
                    contentWidth: width; contentHeight: logView.implicitHeight
                    property bool autoScroll: true
                    function toBottom() { if (autoScroll) contentY = Math.max(0, contentHeight - height) }
                    // Colour-coded, selectable log. RichText keeps per-level colour;
                    // selectByMouse lets the user copy snippets by hand.
                    TextEdit {
                        id: logView
                        width: logFlick.width
                        readOnly: true; selectByMouse: true; persistentSelection: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.RichText
                        font.family: "Menlo"; font.pixelSize: 11
                        color: theme.text; selectionColor: theme.accent
                        text: {
                            function esc(s){ return (""+s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;") }
                            var out = ""
                            var es = backend.logEntries
                            for (var i = 0; i < es.length; i++) {
                                var e = es[i]
                                var lc = e.level === "ERROR" ? theme.danger
                                       : e.level === "WARN" ? theme.warn : theme.textDim
                                out += "<font color='" + theme.textFaint + "'>" + e.time + "</font> "
                                     + "<font color='" + lc + "'>" + e.level + "</font> "
                                     + "<font color='" + theme.text + "'>" + esc(e.msg) + "</font><br>"
                            }
                            return out
                        }
                        onTextChanged: Qt.callLater(logFlick.toBottom)
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
                Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: logFlick.autoScroll; implicitWidth: 34; implicitHeight: 20
                         onToggled: function(v){ logFlick.autoScroll = v } }
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
            height: Math.min(parent.height - 36, fcol.implicitHeight + chdr.height + 10)
            radius: 14; color: theme.bg; border.color: theme.border; border.width: 1
            // Clicking empty card space clears focus from any text field.
            TapHandler { onTapped: cform.forceActiveFocus() }
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
                        Text { text: qsTr("Allow IPv6"); color: theme.textDim; font.pixelSize: 13
                               anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                        Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: cform.ipv6
                                 anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                 onToggled: function(v){ cform.ipv6 = v } }
                    }
                    Column { width: parent.width; spacing: 4
                        Item { width: parent.width; height: certLoad.implicitHeight
                            Text { id: certLoad; anchors.right: parent.right; text: qsTr("Load from file…"); font.pixelSize: 12
                                   color: certLoadMa.containsMouse ? theme.text : theme.accent; font.underline: certLoadMa.containsMouse
                                MouseArea { id: certLoadMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor; onClicked: certFileDlg.open() } }
                            Text { id: certLbl; anchors.left: parent.left; anchors.right: certLoad.left; anchors.rightMargin: 10
                                   elide: Text.ElideRight; text: qsTr("Certificate (PEM) · optional"); color: theme.textDim; font.pixelSize: 13 }
                        }
                        Rectangle { width: parent.width; height: 70; radius: 8; color: theme.inputBg; border.color: fCert.activeFocus ? theme.accent : theme.inputBorder; border.width: 1
                            Flickable { anchors.fill: parent; anchors.margins: 8; contentHeight: fCert.height; clip: true
                                TextEdit { id: fCert; width: parent.width; font.pixelSize: 12; font.family: "Menlo"; color: theme.text; wrapMode: TextEdit.WrapAnywhere } }
                            MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.IBeamCursor } }
                    }
                    Platform.FileDialog {
                        id: certFileDlg; title: qsTr("Select a certificate")
                        nameFilters: ["PEM (*.pem *.crt *.cer)", qsTr("All files (*)")]
                        onAccepted: fCert.text = backend.readTextFile(certFileDlg.file.toString())
                    }
                    Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 8; topPadding: 4; bottomPadding: 4
                        Rectangle { width: 88; height: 32; radius: 8
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
                        Rectangle { width: 88; height: 32; radius: 8
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
