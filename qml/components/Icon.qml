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
