import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import "../components"
import ".."

Item {
    id: splitRoot
    required property var shell
    required property var backend
    required property var theme

    // Clicking empty page area clears focus from a text field.
    TapHandler { onTapped: splitRoot.forceActiveFocus() }
    Flickable {
        anchors.fill: parent
        contentWidth: width // pin content to the viewport so margins stay symmetric
        contentHeight: scol.height; clip: true
        interactive: contentHeight > height // don't steal clicks from inputs when it fits
        ColumnLayout {
            id: scol
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 18; anchors.rightMargin: 18
            spacing: 0
            Item { Layout.preferredHeight: 10 }
            RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 42
                Text { text: qsTr("Split Tunneling"); color: theme.text; font.pixelSize: 14 }
                Item { Layout.fillWidth: true }
                Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: backend.splitEnabled; onToggled: function(v){ backend.splitEnabled = v } }
            }
            Dropdown { label: qsTr("Mode"); value: backend.vpnMode; shell: shell; theme: theme
                model: [{v:"general",t:qsTr("Bypass VPN")},{v:"selective",t:qsTr("Through VPN")}]
                onPicked: function(v){ backend.vpnMode = v } }
            Item { Layout.preferredHeight: 6 }
            SectionLabel { text: qsTr("Profile"); theme: theme }
            Flow {
                Layout.fillWidth: true; Layout.topMargin: 2; spacing: 6
                Repeater {
                    model: backend.profiles
                    Rectangle {
                        id: chip
                        required property string modelData
                        property bool isActive: modelData === backend.activeProfile
                        property bool isDefault: modelData === "Default"
                        radius: 13; height: 28
                        implicitWidth: plabel.width + (chip.isDefault ? 22 : 39)
                        color: isActive ? theme.accent : (chipMa.containsMouse ? theme.border : theme.surface)
                        Behavior on color { ColorAnimation { duration: 120 } }
                        MouseArea { id: chipMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: backend.selectProfile(chip.modelData) }
                        Text { id: plabel; anchors.left: parent.left; anchors.leftMargin: 11
                               anchors.verticalCenter: parent.verticalCenter; text: chip.modelData
                               width: Math.min(implicitWidth, 130); elide: Text.ElideRight
                               color: chip.isActive ? "white" : theme.text; font.pixelSize: 13 }
                        ChipX { visible: !chip.isDefault; onAccent: chip.isActive; theme: theme
                                anchors.left: plabel.right; anchors.leftMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: shell.showConfirm(qsTr("Delete profile “%1”?").arg(shell.elideMiddle(chip.modelData, 36)),
                                    qsTr("Delete"), function(){ backend.removeProfile(chip.modelData) }) }
                    }
                }
                Rectangle {
                    radius: 13; height: 28; implicitWidth: 34
                    color: addChipMa.containsMouse ? theme.border : theme.surface
                    Behavior on color { ColorAnimation { duration: 120 } }
                    border.color: theme.border; border.width: 1
                    Text { anchors.centerIn: parent; text: "+"; color: theme.accent; font.pixelSize: 17 }
                    MouseArea { id: addChipMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: { npRow.visible = true; npInput.forceActiveFocus() } }
                }
            }
            Rectangle {
                id: npRow; visible: false
                Layout.fillWidth: true; Layout.topMargin: 6; Layout.preferredHeight: 36; radius: 8
                color: theme.inputBg; border.color: theme.accent; border.width: 1
                TextInput {
                    id: npInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter; clip: true; font.pixelSize: 13; color: theme.text
                    maximumLength: 16
                    onAccepted: { backend.addProfile(text); text = ""; npRow.visible = false }
                    Keys.onEscapePressed: { text = ""; npRow.visible = false }
                    // Collapse the field when it loses focus (e.g. clicking elsewhere).
                    onActiveFocusChanged: if (!activeFocus) { text = ""; npRow.visible = false }
                }
                Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                       anchors.right: parent.right; anchors.rightMargin: 12; elide: Text.ElideRight
                       text: qsTr("profile name, then Enter"); color: theme.textFaint; font.pixelSize: 13
                       visible: npInput.text.length === 0 }
            }
            Item { Layout.preferredHeight: 14 }
            RowLayout { Layout.fillWidth: true; spacing: 10
                SectionLabel { Layout.fillWidth: true; elide: Text.ElideRight; theme: theme
                    text: backend.vpnMode === "selective" ? qsTr("Rules — via VPN") : qsTr("Rules — bypass VPN") }
                Text { text: qsTr("Recommended for Russia"); font.pixelSize: 12
                       color: recMa.containsMouse ? theme.text : theme.accent; font.underline: recMa.containsMouse
                    MouseArea { id: recMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: backend.addRecommendedRussia() } }
                Text { text: qsTr("Clear all"); font.pixelSize: 12; visible: backend.domains.length > 0
                       color: clrDomMa.containsMouse ? Qt.lighter(theme.danger, 1.25) : theme.danger
                       font.underline: clrDomMa.containsMouse
                    MouseArea { id: clrDomMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor; onClicked: shell.showConfirm(qsTr("Clear all domains?"),
                        qsTr("Clear"), function(){ backend.clearDomains() }) } }
            }
            Flow {
                Layout.fillWidth: true; spacing: 6
                visible: backend.domains.length > 0
                Layout.topMargin: visible ? 6 : 0
                Layout.preferredHeight: visible ? implicitHeight : 0
                Repeater {
                    model: backend.domains
                    Rectangle {
                        id: domChip
                        required property string modelData
                        required property int index
                        radius: 13; color: theme.surface
                        implicitWidth: dlabel.width + 39; implicitHeight: 28
                        Text { id: dlabel; anchors.left: parent.left; anchors.leftMargin: 11
                               anchors.verticalCenter: parent.verticalCenter; text: domChip.modelData
                               width: Math.min(implicitWidth, 190); elide: Text.ElideRight
                               color: theme.text; font.pixelSize: 13 }
                        ChipX { theme: theme; anchors.left: dlabel.right; anchors.leftMargin: 5
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: backend.removeDomain(domChip.index) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 36; radius: 8
                Layout.topMargin: 6
                color: theme.inputBg; border.color: domInput.activeFocus ? theme.accent : theme.inputBorder; border.width: 1
                TextInput {
                    id: domInput
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter; clip: true
                    font.pixelSize: 13; color: theme.text
                    onAccepted: { if (backend.addDomain(text)) text = "" }
                    Keys.onEscapePressed: focus = false
                }
                Text { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                       anchors.right: parent.right; anchors.rightMargin: 12; elide: Text.ElideRight
                       text: qsTr("domain or domains (comma/space separated), then Enter"); color: theme.textFaint; font.pixelSize: 13
                       visible: domInput.text.length === 0 && !domInput.activeFocus }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.IBeamCursor }
            }
            Item { Layout.preferredHeight: 16 }
        }
    }
}
