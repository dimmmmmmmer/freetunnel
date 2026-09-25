import QtQuick
import QtQuick.Shapes

// The update row's arrow, drawn so that it can change shape. Straight it is ↓,
// "download"; bent round clockwise it is ↻, the arrow of the work that follows,
// and while that work runs the ↻ turns. In between, the shaft bends and its head
// sweeps down, left, up and round: the arrow turns into the other one rather
// than being swapped for it.
Item {
    id: arrow
    required property var theme
    // 0 is ↓, 1 is ↻.
    property real bend: 0
    // Turned clockwise by this much on top of any spin, in degrees.
    property real turn: 0
    property bool spinning: false
    property color color: theme.accent
    implicitWidth: 20; implicitHeight: 20

    // Clockwise, continuously, while it spins; when it stops, it comes round to
    // where it started rather than halting at an angle.
    property real spinAngle: 0
    NumberAnimation on spinAngle {
        id: spin
        running: arrow.spinning && arrow.visible
        from: 0; to: 360; duration: 1100; loops: Animation.Infinite
        onRunningChanged: {
            if (running) {
                settle.stop()
            } else if (arrow.spinAngle % 360 !== 0) {
                // Slowing from the speed it turned at: an OutQuad starts at twice
                // the average speed, so twice the time the rest would take.
                settle.duration = Math.max(1, (360 - arrow.spinAngle) / 360 * spin.duration * 2)
                settle.start()
            }
        }
    }
    NumberAnimation {
        id: settle; target: arrow; property: "spinAngle"
        to: 360; easing.type: Easing.OutQuad
        onFinished: arrow.spinAngle = 0
    }

    // The two strokes, shaft and head, for the current bend. The shaft has one
    // curvature along its whole length and its tail always heads straight down,
    // so bending it swings the tip clockwise; it grows as it bends, from the
    // length of ↓ to 290° of a circle, and the whole is centred in the box. The
    // centre of that circle is what a spin turns about: the head sticks out past
    // the circle, and turning about the middle of everything made it wobble.
    readonly property var geometry: {
        const t = Math.max(0, Math.min(1, bend))
        const radius = 6.2, fullSweep = 290 * Math.PI / 180
        const sweep = t * fullSweep
        const length = 13 + t * (radius * fullSweep - 13)
        const k = sweep / length
        const heading0 = Math.PI / 2 // down; y grows downward
        const shaft = []
        const steps = 28
        for (let i = 0; i <= steps; ++i) {
            const s = length * i / steps
            shaft.push(k < 1e-4 ? Qt.point(0, s)
                                : Qt.point((Math.sin(heading0 + k * s) - Math.sin(heading0)) / k,
                                           -(Math.cos(heading0 + k * s) - Math.cos(heading0)) / k))
        }
        const tip = shaft[steps]
        const back = heading0 + sweep + Math.PI
        const barb = 4.5, spread = 0.7
        const head = [Qt.point(tip.x + barb * Math.cos(back - spread), tip.y + barb * Math.sin(back - spread)),
                      tip,
                      Qt.point(tip.x + barb * Math.cos(back + spread), tip.y + barb * Math.sin(back + spread))]
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
        for (const p of shaft.concat(head)) {
            minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x)
            minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y)
        }
        const dx = width / 2 - (minX + maxX) / 2
        const dy = height / 2 - (minY + maxY) / 2
        const moved = p => Qt.point(p.x + dx, p.y + dy)
        return {
            strokes: [shaft.map(moved), head.map(moved)],
            pivot: k < 1e-4 ? Qt.point(width / 2, height / 2) : Qt.point(-1 / k + dx, dy)
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        transform: Rotation {
            origin.x: arrow.geometry.pivot.x; origin.y: arrow.geometry.pivot.y
            angle: arrow.spinAngle + arrow.turn
        }
        ShapePath {
            strokeColor: arrow.color; strokeWidth: 1.7; fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathMultiline { paths: arrow.geometry.strokes }
        }
    }
}
