import QtQuick

Image {
    id: ic
    required property var theme
    property color color: theme.text
    property url svg
    fillMode: Image.PreserveAspectFit; smooth: true
    sourceSize: Qt.size(width * 2, height * 2)
    function reload() {
        if (svg == "") return
        // qrc-restricted read via the backend — avoids enabling QML XHR local
        // file access (QML_XHR_ALLOW_FILE_READ) for the whole engine.
        var text = backend.readBundledText(svg)
        if (!text) return
        var s = text.replace(/currentColor/g, "" + ic.color)
        ic.source = "data:image/svg+xml;utf8," + encodeURIComponent(s)
    }
    onColorChanged: reload()
    onSvgChanged: reload()
    Component.onCompleted: reload()
}
