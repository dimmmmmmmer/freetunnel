import QtQuick

// Three dots that hop one after another: waiting on an answer, with nothing yet
// to show for it. The update row shows them while it checks; the turning arrow is
// for a download under way.
Row {
    id: dots
    required property var theme
    property color color: theme.text
    spacing: 3
    Repeater {
        model: 3
        Rectangle {
            required property int index
            width: 4; height: 4; radius: 2
            color: dots.color
            // One hop each, in turn, on a shared beat: every dot's round takes
            // the same time, so they stay in step however long it runs.
            SequentialAnimation on y {
                running: dots.visible; loops: Animation.Infinite; alwaysRunToEnd: true
                PauseAnimation { duration: index * 140 }
                NumberAnimation { from: 0; to: -4; duration: 220; easing.type: Easing.OutQuad }
                NumberAnimation { to: 0; duration: 220; easing.type: Easing.InQuad }
                PauseAnimation { duration: (2 - index) * 140 + 200 }
            }
        }
    }
}
