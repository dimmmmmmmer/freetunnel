import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects
import Qt.labs.platform as Platform
import "components"

// FreeTunnel main window: centered top nav + pages, plus back-arrow sub-screens.
// Consumes a `backend` context object injected from C++ (main.cpp).
Window {
    id: win
    visible: true
    // Default size; 400px min keeps frameless nav clear of window controls on Linux/Windows.
    width: 400
    height: 460
    minimumWidth: 400
    minimumHeight: 460
    color: theme.bg
    title: "FreeTunnel"

    // macOS keeps its native (unified) title bar; Linux/Windows go frameless
    // with our own window controls + drag/resize, so the chrome matches macOS.
    readonly property bool isMac: Qt.platform.os === "osx"
    // Custom min/max/close on frameless Linux/Windows (must match nav offset below).
    readonly property int framelessChromeWidth: 108 // 9 + 3×30 + 2×3 + 9
    flags: isMac ? Qt.Window : (Qt.Window | Qt.FramelessWindowHint)

    // macOS red button hides to tray; Linux/Windows custom ✕ calls hide()
    // directly. onClosing then means a real quit (panel Quit, Ctrl+Q, Alt+F4).
    property bool shuttingDown: false
    onClosing: function(close) {
        if (shuttingDown) {
            close.accepted = true
            return
        }
        if (win.isMac) {
            close.accepted = false
            win.hide()
            return
        }
        backend.prepareQuit()
        close.accepted = true
        Qt.quit()
    }

    Connections {
        target: backend
        function onAboutToShutdown() {
            shuttingDown = true
            tray.visible = false
        }
    }

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
                text: backend.disconnecting ? qsTr("Disconnecting…")
                      : backend.connecting ? qsTr("Connecting…")
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
                onTriggered: { backend.prepareQuit(); Qt.quit() }
            }
        }
    }

    // Active palette: light/dark, or follow the OS when themeMode === "system".
    readonly property bool systemDark: Application.styleHints.colorScheme === Qt.Dark
    readonly property QtObject theme: QtObject {
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
    readonly property var pagePaths: ["pages/HomePage.qml", "pages/ConfigsPage.qml",
                                      "pages/SplitPage.qml", "pages/SettingsPage.qml",
                                      "pages/LogsPage.qml"]

    // Pages/overlay get shell+backend+theme as *initial* properties via
    // Loader.setSource so their `required` properties are satisfied at creation.
    // Setting them later in onLoaded runs after the component is built, which
    // breaks the required-property contract and leaves the page blank.
    function pageProps() { return { shell: win, backend: backend, theme: win.theme } }
    onCurrentPageChanged: pageLoader.setSource(pagePaths[currentPage], pageProps())
    onOverlayChanged: overlayLoader.setSource(overlay === "create" ? "CreateConfigOverlay.qml" : "",
                                              overlay === "create" ? pageProps() : {})

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
    // Keep both ends of a long name visible inside quoted confirm text.
    function elideMiddle(s, n) {
        if (s.length <= n) return s
        if (n <= 1) return "…"
        var keep = n - 1
        var head = Math.ceil(keep / 2)
        var tail = Math.floor(keep / 2)
        return s.substring(0, head) + "…" + s.substring(s.length - tail)
    }

    // Render a portable shortcut ("Ctrl+Alt+T") with OS-native modifier glyphs.
    function keyGlyphs(seq) {
        if (!seq) return ""
        if (Qt.platform.os === "osx")
            return seq.replace(/Ctrl/g, "⌘").replace(/Meta/g, "⌃")
                      .replace(/Alt/g, "⌥").replace(/Shift/g, "⇧").replace(/\+/g, "")
        return seq
    }

    function showToast(m) { toast.show(m) }

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

    // ---------- custom window controls (Linux/Windows) ----------
    // macOS keeps its native traffic lights; elsewhere we draw our own so the
    // frameless window can still minimize / maximize / close.
    Row {
        visible: !win.isMac
        z: 60
        anchors.top: parent.top; anchors.right: parent.right
        anchors.topMargin: 9; anchors.rightMargin: 9
        spacing: 3
        // minimize
        Rectangle { width: 30; height: 24; radius: 6
            color: minMa.containsMouse ? theme.surface : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
            Rectangle { anchors.centerIn: parent; width: 11; height: 1.6; radius: 1; color: theme.textDim }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: win.showMinimized() }
        }
        // maximize / restore
        Rectangle { width: 30; height: 24; radius: 6
            color: maxMa.containsMouse ? theme.surface : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
            Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 2
                        color: "transparent"; border.color: theme.textDim; border.width: 1.4 }
            MouseArea { id: maxMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: win.visibility = (win.visibility === Window.Maximized ? Window.Windowed : Window.Maximized) }
        }
        // close (hides to tray, like the macOS red button)
        Rectangle { width: 30; height: 24; radius: 6
            color: closeMa.containsMouse ? theme.danger : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
            Item { anchors.centerIn: parent; width: 12; height: 12
                Rectangle { anchors.centerIn: parent; width: 13; height: 1.6; radius: 1; rotation: 45
                            color: closeMa.containsMouse ? "white" : theme.textDim }
                Rectangle { anchors.centerIn: parent; width: 13; height: 1.6; radius: 1; rotation: -45
                            color: closeMa.containsMouse ? "white" : theme.textDim }
            }
            MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: win.hide() }
        }
    }

    // ---------- resize grips (frameless Linux/Windows) ----------
    Item {
        visible: !win.isMac; anchors.fill: parent; z: 55
        MouseArea { height: 5; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            cursorShape: Qt.SizeVerCursor; onPressed: win.startSystemResize(Qt.TopEdge) }
        MouseArea { height: 5; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            cursorShape: Qt.SizeVerCursor; onPressed: win.startSystemResize(Qt.BottomEdge) }
        MouseArea { width: 5; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left
            cursorShape: Qt.SizeHorCursor; onPressed: win.startSystemResize(Qt.LeftEdge) }
        MouseArea { width: 5; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
            cursorShape: Qt.SizeHorCursor; onPressed: win.startSystemResize(Qt.RightEdge) }
        MouseArea { width: 11; height: 11; anchors.top: parent.top; anchors.left: parent.left
            cursorShape: Qt.SizeFDiagCursor; onPressed: win.startSystemResize(Qt.TopEdge | Qt.LeftEdge) }
        MouseArea { width: 11; height: 11; anchors.top: parent.top; anchors.right: parent.right
            cursorShape: Qt.SizeBDiagCursor; onPressed: win.startSystemResize(Qt.TopEdge | Qt.RightEdge) }
        MouseArea { width: 11; height: 11; anchors.bottom: parent.bottom; anchors.left: parent.left
            cursorShape: Qt.SizeBDiagCursor; onPressed: win.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
        MouseArea { width: 11; height: 11; anchors.bottom: parent.bottom; anchors.right: parent.right
            cursorShape: Qt.SizeFDiagCursor; onPressed: win.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }
    }

    // ---------- main content (nav + page) ----------
    // Stays visible behind the create popup (dimmed by its backdrop).
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Qt.platform.os === "osx" ? 26 : 36
            Layout.bottomMargin: 6
            spacing: 0

            // Mirror the top-right window controls on the left so the nav sits
            // in the true horizontal centre of the title bar (Linux/Windows).
            Item { Layout.preferredWidth: win.isMac ? 0 : win.framelessChromeWidth
                   Layout.maximumWidth: win.isMac ? 0 : win.framelessChromeWidth }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    Repeater {
                        model: win.navIcons
                        Rectangle {
                            id: navItem
                            required property int index
                            required property string modelData
                            property bool active: index === win.currentPage
                            width: 46; height: 38; radius: 8
                            color: theme.bg
                            Rectangle {
                                anchors.fill: parent; radius: parent.radius; color: theme.surface
                                opacity: (nma.containsMouse && !navItem.active) ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                            }
                            Rectangle {
                                anchors.fill: parent; radius: parent.radius; color: theme.infoBg
                                opacity: navItem.active ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                            }
                            scale: nma.containsMouse && !active ? 1.08 : 1.0
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                            Image {
                                visible: navItem.modelData === "connection"
                                anchors.centerIn: parent; width: 22; height: 22
                                source: "qrc:/assets/logo.svg"; sourceSize: Qt.size(44, 44)
                                opacity: navItem.active ? 1.0 : 0.8
                            }
                            Icon {
                                visible: navItem.modelData !== "connection"
                                anchors.centerIn: parent; width: 22; height: 22
                                theme: win.theme
                                svg: navItem.modelData === "configs" ? "qrc:/icons/connection.svg"
                                                                     : "qrc:/icons/" + navItem.modelData + ".svg"
                                color: navItem.active ? theme.accent : theme.textDim
                            }
                            MouseArea { id: nma; anchors.fill: parent; hoverEnabled: true
                                        onClicked: win.currentPage = navItem.index }
                        }
                    }
                }
            }

            Item { Layout.preferredWidth: win.isMac ? 0 : win.framelessChromeWidth
                   Layout.maximumWidth: win.isMac ? 0 : win.framelessChromeWidth }
        }

        Loader {
            id: pageLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            Component.onCompleted: setSource(win.pagePaths[win.currentPage], win.pageProps())
        }
    }

    // ---------- sub-screen overlay ----------
    // Loaded on demand by win.onOverlayChanged via setSource (see above).
    Loader {
        id: overlayLoader
        anchors.fill: parent
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
        TextMetrics { id: toastMetrics; font.pixelSize: 13 }
        function show(m) {
            toastMetrics.text = m
            message = m
            opacity = 0.97
            toastTimer.restart()
        }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 26
        // Size to the message text (TextMetrics), not tmsg.implicitWidth — binding
        // tmsg.width to toast.width made implicitWidth inflate and left empty margins.
        width: Math.min(parent.width - 36, Math.max(80, Math.ceil(toastMetrics.boundingRect.width) + 24))
        height: Math.max(40, tmsg.contentHeight + 18)
        radius: 9; color: theme.surface; border.color: theme.border; border.width: 1
        opacity: 0; visible: opacity > 0
        Text {
            id: tmsg; anchors.centerIn: parent; width: toast.width - 24
            text: toast.message; color: theme.text; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            maximumLineCount: 3; elide: Text.ElideRight
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
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true; shadowColor: "#000000"
                shadowOpacity: theme.dark ? 0.5 : 0.2; shadowBlur: 0.7; shadowVerticalOffset: 5
            }
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
    ConfirmDialog { id: winConfirm; z: 2500; theme: win.theme
        onConfirmed: if (win.confirmCb) win.confirmCb() }

}
