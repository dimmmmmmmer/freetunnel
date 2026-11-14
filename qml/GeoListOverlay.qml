import QtQuick
import QtQuick.Layouts
import "components"

Item {
    id: geoForm
    objectName: "geoListDialog"
    required property var shell
    required property var backend
    required property var theme
    anchors.fill: parent
    property string kind: "sites"
    property string direction: "vpn"
    readonly property bool canAdd: nameField.text.trim().length > 0 && sourceField.text.trim().length > 0

    function submit() {
        if (canAdd && backend.addGeoList(nameField.text.trim(), sourceField.text.trim(),
                                        kind === "ips", direction === "bypass"))
            shell.overlay = ""
    }
    Rectangle {
        anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: geoForm.shell.overlay = "" }
    }
    Shortcut {
        sequence: "Escape"; enabled: !geoForm.shell.windowPopupOpen
        onActivated: geoForm.shell.overlay = ""
    }
    component DialogButton: Rectangle {
        property alias text: buttonText.text
        property bool primary: false
        signal clicked()
        implicitWidth: Math.max(80, buttonText.implicitWidth + 24)
        implicitHeight: 34; radius: 8
        color: primary ? geoForm.theme.accent
                       : pointer.containsMouse ? geoForm.theme.surfaceHover : geoForm.theme.surface
        border.color: activeFocus ? geoForm.theme.text : geoForm.theme.border
        opacity: enabled ? 1 : 0.45
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onSpacePressed: clicked()
        Text {
            id: buttonText; anchors.centerIn: parent; font.pixelSize: 14
            color: parent.primary ? geoForm.theme.accentText : geoForm.theme.text
        }
        MouseArea {
            id: pointer; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }
    Rectangle {
        id: card
        objectName: "geoDialogCard"
        width: Math.min(parent.width - 28, 372)
        height: Math.min(parent.height - geoForm.shell.titlebarSafeTop - 12, form.implicitHeight + 118)
        x: (parent.width - width) / 2
        y: Math.max(geoForm.shell.titlebarSafeTop, (parent.height - height) / 2)
        radius: 14; color: theme.bg; border.color: theme.border
        PopupShadow { radius: card.radius; strength: theme.dark ? 0.5 : 0.2 }
        // Keep clicks on empty card space from reaching the backdrop.
        MouseArea { anchors.fill: parent; onClicked: card.forceActiveFocus() }
        Text {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 16; height: 24; elide: Text.ElideRight
            text: qsTr("Add your own list"); font.pixelSize: 16; font.weight: Font.Medium; color: theme.text
        }
        Flickable {
            id: formFlick
            objectName: "geoDialogForm"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: buttons.top
            anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.topMargin: 54; anchors.bottomMargin: 14
            contentWidth: width; contentHeight: form.implicitHeight; clip: true
            ColumnLayout {
                id: form; width: parent.width; spacing: 12
                Field {
                    id: nameField; objectName: "geoNameField"; Layout.fillWidth: true
                    label: qsTr("Name"); labelColor: theme.textDim; textColor: theme.text
                    fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent
                    placeholderColor: theme.textFaint
                    Component.onCompleted: input.forceActiveFocus()
                }
                Field {
                    id: sourceField; objectName: "geoSourceField"; Layout.fillWidth: true
                    label: qsTr("HTTPS address"); placeholder: "https://…/geosite.dat#category"
                    labelColor: theme.textDim; textColor: theme.text
                    fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent
                    placeholderColor: theme.textFaint
                }
                Dropdown {
                    objectName: "geoKindDropdown"; shell: geoForm.shell; theme: geoForm.theme
                    label: qsTr("List type"); value: geoForm.kind
                    model: [{v:"sites", t:qsTr("Sites")}, {v:"ips", t:qsTr("IP addresses")}]
                    onPicked: function(v) { geoForm.kind = v }
                }
                Dropdown {
                    objectName: "geoDirectionDropdown"; shell: geoForm.shell; theme: geoForm.theme
                    label: qsTr("Route"); value: geoForm.direction; maximumValueWidth: 180
                    model: [{v:"vpn", t:qsTr("Through VPN")}, {v:"bypass", t:qsTr("Bypass VPN")}]
                    onPicked: function(v) { geoForm.direction = v }
                }
            }
        }
        Connections { target: nameField.input; function onAccepted() { sourceField.input.forceActiveFocus() } }
        Connections { target: sourceField.input; function onAccepted() { geoForm.submit() } }
        RowLayout {
            id: buttons
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: 16; spacing: 10
            Item { Layout.fillWidth: true }
            DialogButton { objectName: "geoCancelButton"; text: qsTr("Cancel"); onClicked: geoForm.shell.overlay = "" }
            DialogButton { objectName: "geoAddButton"; text: qsTr("Add"); primary: true
                enabled: geoForm.canAdd; onClicked: geoForm.submit() }
        }
    }
}
