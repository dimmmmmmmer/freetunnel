import QtQuick
import QtQuick.Layouts

Rectangle {
    required property var theme
    Layout.preferredHeight: 1; color: theme.border; Layout.fillWidth: true
}
