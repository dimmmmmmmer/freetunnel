pragma ComponentBehavior: Bound
import QtQuick

// A soft drop shadow under a popup, made of plain rounded rectangles.
//
// Qt's optional software renderer cannot run shaders: a MultiEffect, or any ShaderEffect,
// draws nothing there, and a popup that used one as its layer.effect vanished
// with it. Each ring here is a little larger and fainter than the one inside it,
// so together they fade out from the popup's edge the way a blur does.
//
// Put it inside the popup: it fills its parent and stacks itself beneath the
// parent's own background (z < 0), so it moves, fades and hides with the popup.
Item {
    id: shadow
    // The popup's corner radius, so the shadow follows its outline.
    property real radius: 0
    property color color: "#000000"
    // How dark it is, 0..1: what MultiEffect called shadowOpacity.
    property real strength: 0.2
    // How far past the popup's edge the shadow reaches, in pixels.
    property real spread: 16
    property real verticalOffset: 5
    // Rings to fade it out with. More is smoother and costs a rectangle each.
    property int steps: 8
    // Alpha per ring, chosen so that all of them stacked come to `strength`
    // halved: a blurred shadow is half as dark at the edge it is blurred across.
    readonly property real stepAlpha: 1 - Math.pow(1 - Math.max(0, Math.min(1, strength)) / 2, 1 / steps)

    anchors.fill: parent
    z: -1

    Repeater {
        model: shadow.steps
        Rectangle {
            required property int index
            // Rings bunch up near the edge, so it is darkest there and thins
            // out further away, as a blurred shadow does, instead of in even bands.
            readonly property real grow: shadow.spread * Math.pow((index + 1) / shadow.steps, 1.7)
            x: -grow
            y: -grow + shadow.verticalOffset
            width: shadow.width + 2 * grow
            height: shadow.height + 2 * grow
            radius: shadow.radius + grow
            color: Qt.rgba(shadow.color.r, shadow.color.g, shadow.color.b, shadow.stepAlpha)
            antialiasing: true
        }
    }
}
