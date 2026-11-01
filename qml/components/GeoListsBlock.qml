import QtQuick
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: geo
    required property var backend
    required property var shell
    required property var theme
    property bool customOpen: false
    property string customKind: "sites"
    property string customDirection: "vpn"
    spacing: 8

    function presetName(preset) {
        switch (preset.id) {
        case "ru-blocked-sites": return qsTr("Blocked sites in Russia")
        case "ru-blocked-ips": return qsTr("Blocked IP addresses in Russia")
        case "ru-sites": return qsTr("Russian sites")
        case "ru-ips": return qsTr("Russian IP addresses")
        default: return preset.name
        }
    }
    component Action: Rectangle {
        property alias text: label.text
        signal clicked()
        implicitWidth: label.implicitWidth + 20
        implicitHeight: 32
        radius: 8
        color: activeFocus || pointer.containsMouse ? geo.theme.surfaceHover : geo.theme.surface
        border.color: activeFocus ? geo.theme.accent : geo.theme.border
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onReturnPressed: clicked()
        Keys.onSpacePressed: clicked()
        Text { id: label; anchors.centerIn: parent; color: geo.theme.accent; font.pixelSize: 13 }
        MouseArea { id: pointer; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }
    SectionLabel { theme: geo.theme; text: qsTr("Geo lists") }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textDim; font.pixelSize: 12
        text: qsTr("Ready-made groups of sites and IP addresses. Your manual rules take priority.")
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.text; font.pixelSize: 12
        visible: backend.geoLimitExceeded
        text: qsTr("Too many geo rules. Disable some lists. The full tunnel stays on until the rules fit.")
    }
    Dropdown {
        Layout.fillWidth: true; shell: geo.shell; theme: geo.theme
        label: qsTr("Add a ready-made list"); value: ""
        model: {
            var choices = [{v:"", t:qsTr("Choose…")}]
            var presets = backend.geoPresets
            for (var i = 0; i < presets.length; ++i)
                choices.push({v:presets[i].id, t:geo.presetName(presets[i])})
            return choices
        }
        onPicked: function(id) {
            var presets = backend.geoPresets
            for (var i = 0; i < presets.length; ++i) {
                var p = presets[i]
                if (p.id === id) backend.addGeoList(geo.presetName(p), p.source, p.kind === "ips", p.direction === "bypass")
            }
        }
    }
    Repeater {
        model: backend.geoLists
        delegate: Rectangle {
            id: card
            required property var modelData
            Layout.fillWidth: true; implicitHeight: contents.implicitHeight + 20
            radius: 10; color: theme.surface; border.color: theme.border
            ColumnLayout {
                id: contents
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 10; spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: card.modelData.name; wrapMode: Text.WordWrap; color: theme.text; font.pixelSize: 14 }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: card.modelData.enabled
                        onToggled: function(v) { backend.setGeoListEnabled(card.modelData.id, v) } }
                }
                Dropdown {
                    shell: geo.shell; theme: geo.theme; label: qsTr("Route")
                    model: [{v:"vpn",t:qsTr("Through VPN")},{v:"bypass",t:qsTr("Bypass VPN")}]
                    value: card.modelData.bypass ? "bypass" : "vpn"
                    onPicked: function(v) { backend.setGeoListDirection(card.modelData.id, v === "bypass") }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textDim; font.pixelSize: 12
                    text: card.modelData.busy ? qsTr("Updating…")
                        : card.modelData.entries > 0 ? qsTr("%1 entries · %2").arg(card.modelData.entries).arg(card.modelData.updated)
                        : qsTr("Not downloaded yet")
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.textDim; font.pixelSize: 12
                    visible: card.modelData.skipped > 0; text: qsTr("Unsupported entries skipped: %1").arg(card.modelData.skipped) }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: theme.text; font.pixelSize: 12
                    visible: !!card.modelData.error; text: card.modelData.error || "" }
                RowLayout {
                    Action { text: qsTr("Update"); enabled: !card.modelData.busy; opacity: enabled ? 1 : 0.5
                        onClicked: backend.refreshGeoList(card.modelData.id) }
                    Item { Layout.fillWidth: true }
                    Action { text: qsTr("Remove"); onClicked: backend.removeGeoList(card.modelData.id) }
                }
            }
        }
    }
    Action { text: customOpen ? qsTr("Cancel") : qsTr("Add your own list"); onClicked: customOpen = !customOpen }
    ColumnLayout {
        visible: customOpen; Layout.fillWidth: true; spacing: 8
        Field { id: customName; Layout.fillWidth: true; label: qsTr("Name"); labelColor: theme.textDim; textColor: theme.text
            fieldBg: theme.inputBg; fieldBorder: theme.border; fieldFocus: theme.accent }
        Field { id: customSource; Layout.fillWidth: true; label: qsTr("HTTPS address"); placeholder: "https://…/geosite.dat#category"
            labelColor: theme.textDim; textColor: theme.text; fieldBg: theme.inputBg; fieldBorder: theme.border; fieldFocus: theme.accent }
        Dropdown { shell: geo.shell; theme: geo.theme; label: qsTr("List type"); value: customKind
            model: [{v:"sites",t:qsTr("Sites")},{v:"ips",t:qsTr("IP addresses")}]
            onPicked: function(v) { customKind = v } }
        Dropdown { shell: geo.shell; theme: geo.theme; label: qsTr("Route"); value: customDirection
            model: [{v:"vpn",t:qsTr("Through VPN")},{v:"bypass",t:qsTr("Bypass VPN")}]
            onPicked: function(v) { customDirection = v } }
        Action { text: qsTr("Add"); onClicked: {
            if (backend.addGeoList(customName.text, customSource.text, customKind === "ips", customDirection === "bypass")) {
                customName.text = ""; customSource.text = ""; customOpen = false
            }
        } }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Update lists daily"); color: theme.text; font.pixelSize: 13 }
        Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.geoAutoUpdate
            onToggled: function(v) { backend.geoAutoUpdate = v } }
    }
}
