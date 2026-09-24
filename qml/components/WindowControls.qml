import QtQuick
import QtQuick.Window

// The window's own minimise / maximise / close, drawn the way the system the app
// is running on draws them.
//
// The window is frameless on Windows and Linux, so these replace the system's —
// and a replacement that looks like some other platform's reads as a foreign
// window. So there is one look per family rather than one for all of them:
//   "windows"  Windows 11 caption buttons: full-bleed 46x32 cells flush in the
//              corner, Segoe Fluent Icons glyphs, the system red behind close.
//   "pop"      Pop!_OS: 20 px circles, close filled orange, the others only on hover.
//   "adwaita"  libadwaita / GNOME: every button a faint circle, no red anywhere.
// The Linux sizes and spacings are Pop's GTK theme and libadwaita's stylesheet,
// checked against GTK's own rendering of a header bar; the glyphs are the Pop and
// Adwaita icon themes' shapes, 2 px strokes on a 16 px grid.
// Which buttons exist and in what order is the desktop's choice too (see
// DesktopChrome): Pop has no maximise button by default, stock GNOME only close.
Item {
    id: controls

    property var buttons: ["minimize", "maximize", "close"]
    property string controlStyle: "adwaita"
    property var window: null
    property var theme: null

    signal minimizeRequested()
    signal maximizeToggleRequested()
    signal closeRequested()

    readonly property bool windows: controlStyle === "windows"
    readonly property bool pop: controlStyle === "pop"
    readonly property bool maximized: window !== null && window.visibility === Window.Maximized
    readonly property bool windowActive: window === null || window.active
    readonly property color glyphColor: theme ? theme.text : "#808080"
    // Segoe Fluent Icons ships with Windows 11; Windows 10 has the older MDL2 set,
    // which uses the same code points for these four glyphs.
    readonly property string captionFont: Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0
                                          ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"

    readonly property int spacing: windows ? 0 : pop ? 12 : 3
    readonly property int cellWidth: windows ? 46 : pop ? 28 : 34
    readonly property int cellHeight: windows ? 32 : 26
    // Where the group sits in from the window's corner, for the window to place it
    // by. Windows' caption buttons are flush with the edges; on Linux the circles
    // are centred 23 px down and 11-13 px in from the side, as in a header bar.
    readonly property int sideInset: windows ? 0 : pop ? 9 : 6
    readonly property int topInset: windows ? 0 : 10
    readonly property int count: buttons.length
    width: count > 0 ? count * cellWidth + (count - 1) * spacing : 0
    height: cellHeight

    // Three explicit buttons, each placed by where its name sits in `buttons`,
    // rather than a Repeater over the list. Repeater delegates are not in the
    // window's object tree, so findChild() cannot reach them — and the Windows
    // window agent finds these by name to tell Windows where maximise is. Through a
    // Repeater, Snap Layouts would silently never appear.
    CaptionButton { modelData: "minimize" }
    CaptionButton { modelData: "maximize" }
    CaptionButton { modelData: "close" }

    component CaptionButton: Item {
            id: cell
            property string modelData
            readonly property int slot: controls.buttons.indexOf(modelData)
            readonly property bool isClose: modelData === "close"
            visible: slot >= 0
            x: Math.max(slot, 0) * (controls.cellWidth + controls.spacing)
            // Named so the window agent on Windows can mark them as the system's
            // own buttons — which is what makes hovering maximise open Snap Layouts.
            // Only while shown: the window has a set of these on each side, and
            // findChild() stops at the first name it meets, so a hidden one keeping
            // its name could be found in place of the one on screen.
            objectName: slot < 0 ? ""
                      : modelData === "minimize" ? "windowMinButton"
                      : modelData === "maximize" ? "windowMaxButton" : "windowCloseRect"
            width: controls.cellWidth
            height: controls.cellHeight

            // ---- Windows 11 ----------------------------------------------------
            Rectangle {
                visible: controls.windows
                anchors.fill: parent
                color: !area.containsMouse ? "transparent"
                     : cell.isClose ? (area.pressed ? Qt.rgba(0.77, 0.17, 0.12, 0.9) : "#C42C1E")
                     : Qt.rgba(controls.glyphColor.r, controls.glyphColor.g, controls.glyphColor.b,
                               area.pressed ? 0.05 : 0.08)
            }
            Text {
                visible: controls.windows
                anchors.centerIn: parent
                font.family: controls.captionFont
                font.pixelSize: 10
                text: cell.modelData === "minimize" ? "\uE921"
                    : cell.modelData === "maximize" ? (controls.maximized ? "\uE923" : "\uE922")
                    : "\uE8BB"
                color: cell.isClose && area.containsMouse ? "white" : controls.glyphColor
                // An inactive window's caption glyphs are dimmed, except the one
                // under the pointer.
                opacity: controls.windowActive || area.containsMouse ? 1.0 : 0.45
            }

            // ---- Linux: Pop and libadwaita -------------------------------------
            Rectangle {
                id: circle
                visible: !controls.windows
                anchors.centerIn: parent
                readonly property bool pop: controls.pop
                width: pop ? 20 : 24
                height: width
                radius: width / 2
                // Pop fills close with its orange while the window is active and
                // drops it when it is not; the others show only under the pointer.
                // libadwaita shows every button as a faint disc, a little stronger
                // under the pointer and stronger still while pressed.
                color: {
                    const g = controls.glyphColor
                    if (pop) {
                        if (area.containsMouse)
                            return Qt.rgba(g.r, g.g, g.b, area.pressed ? 0.10 : (controls.windowActive ? 0.20 : 0.10))
                        return cell.isClose && controls.windowActive ? "#f28c2a" : "transparent"
                    }
                    return Qt.rgba(g.r, g.g, g.b, area.pressed ? 0.30 : area.containsMouse ? 0.15 : 0.10)
                }
            }
            Item {
                id: glyph
                visible: !controls.windows
                anchors.centerIn: parent
                width: 16; height: 16
                // Pop's close sits on its orange disc with a light glyph whatever the
                // window's own theme; everything else takes the text colour, because
                // this window can be light, which Pop's title bar never is.
                readonly property color ink: circle.pop && cell.isClose && controls.windowActive
                                             && !area.containsMouse ? "#F7F7F7" : controls.glyphColor
                opacity: controls.controlStyle === "adwaita" && !controls.windowActive ? 0.5 : 1.0

                readonly property bool restore: cell.modelData === "maximize" && controls.maximized
                // minimise: a bar low in the cell
                Rectangle {
                    visible: cell.modelData === "minimize"
                    x: 4; y: 10; width: 8; height: 2; color: glyph.ink
                }
                // maximise: Pop draws a plus, Adwaita a square
                Rectangle {
                    visible: cell.modelData === "maximize" && !glyph.restore && circle.pop
                    x: 7; y: 4; width: 2; height: 8; color: glyph.ink
                }
                Rectangle {
                    visible: cell.modelData === "maximize" && !glyph.restore && circle.pop
                    x: 4; y: 7; width: 8; height: 2; color: glyph.ink
                }
                Rectangle {
                    visible: cell.modelData === "maximize" && !glyph.restore && !circle.pop
                    x: 4; y: 4; width: 8; height: 8
                    color: "transparent"; border.width: 2; border.color: glyph.ink
                }
                // restore: a smaller square, on both (Pop has no icon of its own and
                // falls back to Adwaita's)
                Rectangle {
                    visible: glyph.restore
                    x: 5; y: 5; width: 6; height: 6
                    color: "transparent"; border.width: 2; border.color: glyph.ink
                }
                // close: a cross with round ends
                Rectangle {
                    visible: cell.isClose
                    anchors.centerIn: parent; width: 10.5; height: 2; radius: 1
                    rotation: 45; color: glyph.ink
                }
                Rectangle {
                    visible: cell.isClose
                    anchors.centerIn: parent; width: 10.5; height: 2; radius: 1
                    rotation: -45; color: glyph.ink
                }
            }

            MouseArea {
                id: area
                // The UI tests press this one by name.
                objectName: cell.isClose && cell.slot >= 0 ? "windowCloseButton" : ""
                anchors.fill: parent
                hoverEnabled: true
                // No pointing hand: a window's own buttons use the arrow on every
                // desktop this draws for. The hand was a web idiom.
                onClicked: {
                    if (cell.modelData === "minimize")
                        controls.minimizeRequested()
                    else if (cell.modelData === "maximize")
                        controls.maximizeToggleRequested()
                    else
                        controls.closeRequested()
                }
            }
        }
}
