import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Qt.labs.platform as Platform
import "components"

Item {
    id: createRoot
    required property var shell
    required property var backend
    required property var theme

    anchors.fill: parent
    // Dimmed backdrop — the main UI shows through; click to close.
    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; onClicked: cform.tryClose() } }
    Shortcut { sequences: ["Escape"]; enabled: !discardConfirm.visible; onActivated: cform.tryClose() }

    Rectangle {
        id: cform
        anchors.centerIn: parent
        width: Math.min(parent.width - 28, 380)
        height: Math.min(parent.height - 36, fcol.implicitHeight + chdr.height + 10)
        radius: 14; color: theme.bg; border.color: theme.border; border.width: 1
        // Clicking empty card space clears focus from any text field.
        TapHandler { onTapped: cform.forceActiveFocus() }
        property string protocol: "http2"
        property bool ipv6: true
        property string splitProfile: "Default"
        property bool socks5: false
        property string snap: ""
        readonly property bool editing: shell.editIndex >= 0
        function snapshot() {
            return [fName.text, fHost.text, fAddr.text, fUser.text, fPass.text,
                    protocol, fDns.text, fSni.text, fRandom.text, fCert.text, ipv6,
                    splitProfile, socks5, fListen.text, fSUser.text, fSPass.text].join("")
        }
        function tryClose() { if (snapshot() !== snap) discardConfirm.open(); else close() }
        function close() { shell.editIndex = -1; shell.overlay = "" }
        // Prefill from the selected config when editing; snapshot for dirty-check.
        Component.onCompleted: {
            if (editing) {
                var f = backend.configFields(shell.editIndex)
                fName.text = f.name || ""; fHost.text = f.hostname || ""
                fAddr.text = f.addresses || ""; fUser.text = f.username || ""
                fPass.text = f.password || ""; fDns.text = f.dns || ""
                fSni.text = f.customSni || ""; fRandom.text = f.clientRandom || ""
                fCert.text = f.certificate || ""
                cform.protocol = f.protocol === "http3" ? "http3" : "http2"
                cform.ipv6 = f.allowIpv6 === undefined ? true : f.allowIpv6
                cform.splitProfile = f.splitProfile || "Default"
                cform.socks5 = f.socks5 === true
                fListen.text = f.socksListen || ""
                fSUser.text = f.socksUser || ""
                fSPass.text = f.socksPass || ""
            }
            snap = snapshot()
        }
        Item {
            id: chdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
            height: 48
            Text { id: cBack; anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                   text: "←"; color: cBackMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 20
                   MouseArea { id: cBackMa; anchors.fill: parent; hoverEnabled: true; onClicked: cform.tryClose() } }
            Text { anchors.left: cBack.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                   text: cform.editing ? qsTr("Edit config") : qsTr("New config"); color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
        }
        Flickable {
            anchors.top: chdr.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 18; anchors.rightMargin: 18; contentWidth: width; contentHeight: fcol.height; clip: true
            Column {
                id: fcol; width: parent.width; spacing: 10
                Field { id: fName; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Name"); placeholder: qsTr("Germany · Frankfurt") }
                Field { id: fHost; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Server host"); placeholder: "frankfurt.example.com" }
                Field { id: fAddr; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Address(es) · host:port (comma-separated)"); placeholder: "1.2.3.4:443" }
                Row { width: parent.width; spacing: 10
                    Field { id: fUser; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Username"); width: (parent.width - 10) / 2 }
                    Field { id: fPass; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Password"); password: true; width: (parent.width - 10) / 2 }
                }
                Row { width: parent.width; spacing: 10
                    Column { width: (parent.width - 10) / 2; spacing: 4
                        Text { text: qsTr("Protocol"); color: theme.textDim; font.pixelSize: 13 }
                        Rectangle { id: protoBox; width: parent.width; height: 34; radius: 8; color: theme.inputBg
                            border.color: protoMa.containsMouse ? theme.accent : theme.inputBorder; border.width: 1
                            Behavior on border.color { ColorAnimation { duration: 120 } }
                            Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                   text: cform.protocol === "http3" ? "HTTP/3" : "HTTP/2"; color: theme.text; font.pixelSize: 14 }
                            Text { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                   text: "▾"; color: theme.textDim; font.pixelSize: 16 }
                            MouseArea { id: protoMa; anchors.fill: parent; hoverEnabled: true
                                onClicked: shell.showSelect(protoBox,
                                    [{v:"http2",t:"HTTP/2"},{v:"http3",t:"HTTP/3"}],
                                    cform.protocol, function(v){ cform.protocol = v }) } }
                    }
                    Field { id: fDns; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("DNS servers"); placeholder: "1.1.1.1, 8.8.8.8"; width: (parent.width - 10) / 2 }
                }
                Row { width: parent.width; spacing: 10
                    Field { id: fSni; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: "Custom SNI"; width: (parent.width - 10) / 2 }
                    Field { id: fRandom; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: "Client random (hex)"; width: (parent.width - 10) / 2 }
                }
                Column { width: parent.width; spacing: 4
                    Text { text: qsTr("Split profile"); color: theme.textDim; font.pixelSize: 13 }
                    Rectangle { id: profBox; width: parent.width; height: 34; radius: 8; color: theme.inputBg
                        border.color: profMa.containsMouse ? theme.accent : theme.inputBorder; border.width: 1
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                        Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.right: profArrow.left; anchors.rightMargin: 6
                               anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                               text: cform.splitProfile; color: theme.text; font.pixelSize: 14 }
                        Text { id: profArrow; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                               text: "▾"; color: theme.textDim; font.pixelSize: 16 }
                        MouseArea { id: profMa; anchors.fill: parent; hoverEnabled: true
                            onClicked: shell.showSelect(profBox,
                                backend.profiles.map(function(p){ return {v:p, t:p} }),
                                cform.splitProfile, function(v){ cform.splitProfile = v }) } }
                }
                Item { width: parent.width; height: 32
                    Text { text: qsTr("Allow IPv6"); color: theme.textDim; font.pixelSize: 13
                           anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: cform.ipv6
                             anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                             onToggled: function(v){ cform.ipv6 = v } }
                }
                Column { width: parent.width; spacing: 4
                    Text { text: qsTr("Mode"); color: theme.textDim; font.pixelSize: 13 }
                    Rectangle { id: modeBox; width: parent.width; height: 34; radius: 8; color: theme.inputBg
                        border.color: modeMa.containsMouse ? theme.accent : theme.inputBorder; border.width: 1
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                        Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.right: modeArrow.left; anchors.rightMargin: 6
                               anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                               text: cform.socks5 ? qsTr("SOCKS5 proxy · local, no admin")
                                                  : qsTr("VPN tunnel · system-wide")
                               color: theme.text; font.pixelSize: 14 }
                        Text { id: modeArrow; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                               text: "▾"; color: theme.textDim; font.pixelSize: 16 }
                        MouseArea { id: modeMa; anchors.fill: parent; hoverEnabled: true
                            onClicked: shell.showSelect(modeBox,
                                [{v:"tun",t:qsTr("VPN tunnel · system-wide")},
                                 {v:"socks",t:qsTr("SOCKS5 proxy · local, no admin")}],
                                cform.socks5 ? "socks" : "tun",
                                function(v){ cform.socks5 = (v === "socks") }) } }
                }
                Column { width: parent.width; spacing: 10; visible: cform.socks5
                    Field { id: fListen; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("SOCKS listen · host:port"); placeholder: "127.0.0.1:1080" }
                    Row { width: parent.width; spacing: 10
                        Field { id: fSUser; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("SOCKS user · optional"); width: (parent.width - 10) / 2 }
                        Field { id: fSPass; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("SOCKS password · optional"); password: true; width: (parent.width - 10) / 2 }
                    }
                }
                Column { width: parent.width; spacing: 4
                    Item { width: parent.width; height: certLoad.implicitHeight
                        Text { id: certLoad; anchors.right: parent.right; text: qsTr("Load from file…"); font.pixelSize: 12
                               color: certLoadMa.containsMouse ? theme.text : theme.accent; font.underline: certLoadMa.containsMouse
                            MouseArea { id: certLoadMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor; onClicked: certFileDlg.open() } }
                        Text { id: certLbl; anchors.left: parent.left; anchors.right: certLoad.left; anchors.rightMargin: 10
                               elide: Text.ElideRight; text: qsTr("Certificate (PEM) · optional"); color: theme.textDim; font.pixelSize: 13 }
                    }
                    Rectangle { width: parent.width; height: 70; radius: 8; color: theme.inputBg; border.color: fCert.activeFocus ? theme.accent : theme.inputBorder; border.width: 1
                        Flickable { anchors.fill: parent; anchors.margins: 8; contentHeight: fCert.height; clip: true
                            TextEdit { id: fCert; width: parent.width; font.pixelSize: 12; font.family: "Menlo"; color: theme.text; wrapMode: TextEdit.WrapAnywhere } }
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.IBeamCursor } }
                }
                Platform.FileDialog {
                    id: certFileDlg; title: qsTr("Select a certificate")
                    nameFilters: ["PEM (*.pem *.crt *.cer)", qsTr("All files (*)")]
                    onAccepted: fCert.text = backend.readTextFile(certFileDlg.file.toString())
                }
                Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 8; topPadding: 4; bottomPadding: 4
                    Rectangle { width: 88; height: 32; radius: 8
                        color: saveMa.containsMouse ? Qt.darker(theme.accent, 1.12) : theme.accent
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Text { anchors.centerIn: parent; text: qsTr("Save"); color: "white"; font.pixelSize: 14 }
                        MouseArea { id: saveMa; anchors.fill: parent; hoverEnabled: true; onClicked: {
                            var ok = backend.createConfig({
                                name: fName.text, hostname: fHost.text, addresses: fAddr.text,
                                username: fUser.text, password: fPass.text, protocol: cform.protocol,
                                dns: fDns.text, customSni: fSni.text, clientRandom: fRandom.text,
                                allowIpv6: cform.ipv6, certificate: fCert.text,
                                splitProfile: cform.splitProfile, socks5: cform.socks5,
                                socksListen: fListen.text, socksUser: fSUser.text, socksPass: fSPass.text,
                                editIndex: shell.editIndex });
                            if (ok) cform.close()
                        } } }
                    Rectangle { width: 88; height: 32; radius: 8
                        color: cancelMa.containsMouse ? theme.surface : theme.bg; border.color: theme.border; border.width: 1
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                        MouseArea { id: cancelMa; anchors.fill: parent; hoverEnabled: true; onClicked: cform.tryClose() } }
                }
            }
        }
    }
    ConfirmDialog {
        id: discardConfirm
        theme: createRoot.theme
        text: qsTr("Discard unsaved changes?")
        confirmText: qsTr("Discard")
        onConfirmed: cform.close()
    }
}
