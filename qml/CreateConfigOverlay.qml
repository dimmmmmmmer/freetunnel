import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects
// See AppPickerOverlay.qml: the labs FileDialog opens nothing where the desktop
// has no native helper and the application does not link Qt Widgets.
import QtQuick.Dialogs as Dialogs
import "components"

Item {
    id: createRoot
    objectName: "createOverlay"
    // The built-in profile is stored as "Default" and shown in the UI's language.
    function profileLabel(name) { return name === "Default" ? qsTr("Default") : name }
    required property var shell
    required property var backend
    required property var theme

    // Keep the card below native traffic lights / frameless window controls. The
    // window works that out from where the buttons really are; see Main.qml.
    readonly property int safeTop: shell.titlebarSafeTop
    readonly property int cardWidth: Math.min(width - 28, 372)

    anchors.fill: parent
    // Dimmed backdrop — the main UI shows through; click to close. It takes hover
    // too: the page under the dim is out of reach, and lit up as if it were not.
    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.45
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: cform.tryClose() } }
    // Escape closes the innermost thing first: stand down while the discard
    // dialog or a window-level popup (protocol / split-profile dropdown, confirm
    // dialog) already handles it — two enabled shortcuts on one key are
    // ambiguous to Qt and then *neither* fires.
    // Nor while the certificate file dialog is up. Where Qt draws that dialog
    // itself, this window shortcut also sees its Escape, and closing the editor
    // then destroyed the dialog in the middle of handling the key: a crash.
    Shortcut { sequences: ["Escape"]
               enabled: !discardConfirm.visible && !shell.windowPopupOpen && !certFileDlg.visible
               onActivated: cform.tryClose() }

    Rectangle {
        id: cform
        // Centre in the window, but never let the top rise above the title-bar
        // safe area — so a tall window centres the card vertically, while a short
        // one keeps it pinned just below the traffic lights.
        x: (parent.width - width) / 2
        y: Math.max(createRoot.safeTop, (parent.height - height) / 2)
        width: createRoot.cardWidth
        height: Math.min(parent.height - createRoot.safeTop - 12,
                        fcol.implicitHeight + chdr.height + 14)
        radius: 14; color: theme.bg; border.color: theme.border; border.width: 1
        // Clicking empty card space clears focus from any text field.
        TapHandler { onTapped: cform.forceActiveFocus() }
        property string protocol: "http2"
        property bool ipv6: true
        property bool skipVerification: false
        property bool antiDpi: false
        property string splitProfile: "Default"
        property string snap: ""
        // The config being edited, by file rather than by row. The row can move
        // under an open editor — an imported config is prepended to the list —
        // and saving against the old number then writes over whatever is at that
        // position now. See Backend::createConfig().
        property string editPath: ""
        readonly property bool editing: shell.editIndex >= 0
        // Join on a separator no field can contain (U+001F): a plain join() lets
        // a boundary-shifting edit (name "ab" + host "c" → name "a" + host "bc")
        // rebuild the same string, so tryClose() would drop real edits silently.
        function snapshot() {
            return [fName.text, fHost.text, fAddr.text, fUser.text, fPass.text,
                    protocol, fDns.text, fSni.text, fRandom.text, fCert.text, ipv6,
                    skipVerification, antiDpi, splitProfile].join("\u001f")
        }
        function tryClose() { if (snapshot() !== snap) discardConfirm.open(); else close() }
        function close() { shell.editIndex = -1; shell.overlay = "" }
        // Prefill from the selected config when editing; snapshot for dirty-check.
        Component.onCompleted: {
            if (editing) {
                var f = backend.configFields(shell.editIndex)
                cform.editPath = f.path || ""
                fName.text = f.name || ""; fHost.text = f.hostname || ""
                fAddr.text = f.addresses || ""; fUser.text = f.username || ""
                fPass.text = f.password || ""; fDns.text = f.dns || ""
                fSni.text = f.customSni || ""; fRandom.text = f.clientRandom || ""
                fCert.text = f.certificate || ""
                cform.protocol = f.protocol === "http3" ? "http3" : "http2"
                cform.ipv6 = f.allowIpv6 === undefined ? true : f.allowIpv6
                cform.skipVerification = f.skipVerification === true
                cform.antiDpi = f.antiDpi === true
                cform.splitProfile = f.splitProfile || "Default"
            }
            snap = snapshot()
        }
        Item {
            id: chdr; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
            height: 48
            Text { id: cBack; anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                   text: "←"; color: cBackMa.containsMouse ? theme.text : theme.textDim; font.pixelSize: 20
                   // As far around the arrow as the app picker's: 3 px to its left
                   // it no longer responded.
                   MouseArea { id: cBackMa; objectName: "editorBack"; anchors.fill: parent; anchors.margins: -6
                               hoverEnabled: true; onClicked: cform.tryClose() } }
            Text { anchors.left: cBack.right; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                   text: cform.editing ? qsTr("Edit config") : qsTr("New config"); color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
        }
        Flickable {
            id: formFlick
            objectName: "editorForm"
            anchors.top: chdr.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.leftMargin: 18; anchors.rightMargin: 18; contentWidth: width; contentHeight: fcol.height; clip: true
            // Keep the field that has the keyboard in view. Tab reaches fields below
            // the fold, and typing went into one nobody could see.
            // Room is left above it for the field's label.
            function reveal(item) {
                const top = item.mapToItem(fcol, 0, 0).y
                if (top - 28 < contentY)
                    contentY = Math.max(0, top - 28)
                else if (top + item.height > contentY + height)
                    contentY = Math.max(0, Math.min(contentHeight - height, top + item.height - height + 8))
            }
            Connections {
                target: createRoot.Window.window
                function onActiveFocusItemChanged() {
                    let item = createRoot.Window.activeFocusItem
                    for (let up = item; up; up = up.parent) {
                        if (up === fcol) {
                            formFlick.reveal(item)
                            return
                        }
                    }
                }
            }
            Column {
                id: fcol; width: parent.width; spacing: 10
                Field { id: fName; objectName: "nameField"; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Name"); placeholder: qsTr("Germany · Frankfurt") }
                Field { id: fHost; objectName: "hostField"; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Server host"); placeholder: "frankfurt.example.com" }
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
                    Field { id: fSni; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Custom SNI"); width: (parent.width - 10) / 2 }
                    Field { id: fRandom; labelColor: theme.textDim; fieldBg: theme.inputBg; fieldBorder: theme.inputBorder; fieldFocus: theme.accent; textColor: theme.text; placeholderColor: theme.textFaint; label: qsTr("Client random (hex)"); width: (parent.width - 10) / 2 }
                }
                Column { width: parent.width; spacing: 4
                    Text { text: qsTr("Split profile"); color: theme.textDim; font.pixelSize: 13 }
                    Rectangle { id: profBox; width: parent.width; height: 34; radius: 8; color: theme.inputBg
                        border.color: profMa.containsMouse ? theme.accent : theme.inputBorder; border.width: 1
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                        Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.right: profArrow.left; anchors.rightMargin: 6
                               anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                               text: createRoot.profileLabel(cform.splitProfile); color: theme.text; font.pixelSize: 14 }
                        Text { id: profArrow; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                               text: "▾"; color: theme.textDim; font.pixelSize: 16 }
                        MouseArea { id: profMa; anchors.fill: parent; hoverEnabled: true
                            onClicked: shell.showSelect(profBox,
                                backend.profiles.map(function(p){ return {v:p, t:createRoot.profileLabel(p)} }),
                                cform.splitProfile, function(v){ cform.splitProfile = v }) } }
                }
                Item { width: parent.width; height: 32
                    Text { text: qsTr("Allow IPv6"); color: theme.textDim; font.pixelSize: 13
                           anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: cform.ipv6
                             anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                             onToggled: function(v){ cform.ipv6 = v } }
                }
                Item { width: parent.width; height: 32
                    Text { text: qsTr("Skip certificate check"); color: theme.textDim; font.pixelSize: 13
                           anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: cform.skipVerification
                             anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                             onToggled: function(v){ cform.skipVerification = v } }
                }
                Item { width: parent.width; height: 32
                    Text { text: qsTr("Anti-DPI"); color: theme.textDim; font.pixelSize: 13
                           anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                    Toggle { accent: theme.accent; offColor: theme.toggleOff; checked: cform.antiDpi
                             anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                             onToggled: function(v){ cform.antiDpi = v } }
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
                            TextEdit { id: fCert; objectName: "certificateField"
                                width: parent.width; font.pixelSize: 12; font.family: shell.monoFont; color: theme.text; wrapMode: TextEdit.WrapAnywhere
                                // In the tab chain like the other fields; a certificate
                                // has no use for a typed tab, so Tab leaves the field.
                                activeFocusOnTab: true
                                Keys.onTabPressed: function(e) { nextItemInFocusChain(true).forceActiveFocus(Qt.TabFocusReason); e.accepted = true }
                                Keys.onBacktabPressed: function(e) { nextItemInFocusChain(false).forceActiveFocus(Qt.BacktabFocusReason); e.accepted = true } } }
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.IBeamCursor } }
                }
                Dialogs.FileDialog {
                    id: certFileDlg; objectName: "certificateDialog"; title: qsTr("Select a certificate")
                    nameFilters: ["PEM (*.pem *.crt *.cer)", qsTr("All files (*)")]
                    onAccepted: fCert.text = backend.readTextFile(certFileDlg.selectedFile.toString())
                }
                Row { width: parent.width; layoutDirection: Qt.RightToLeft; spacing: 8
                        topPadding: 6; bottomPadding: 12
                    // At least 88 px, and wider for a longer label: «Сохранить» nearly
                    // touched the edges of a fixed 88 px button.
                    Rectangle { objectName: "saveButton"; width: Math.max(88, saveText.implicitWidth + 26); height: 32; radius: 8
                        color: saveMa.containsMouse ? Qt.darker(theme.accent, 1.12) : theme.accent
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Text { id: saveText; objectName: "saveLabel"; anchors.centerIn: parent; text: qsTr("Save"); color: theme.accentText; font.pixelSize: 14 }
                        MouseArea { id: saveMa; anchors.fill: parent; hoverEnabled: true; onClicked: {
                            var ok = backend.createConfig({
                                name: fName.text, hostname: fHost.text, addresses: fAddr.text,
                                username: fUser.text, password: fPass.text, protocol: cform.protocol,
                                dns: fDns.text, customSni: fSni.text, clientRandom: fRandom.text,
                                allowIpv6: cform.ipv6, skipVerification: cform.skipVerification,
                                antiDpi: cform.antiDpi, certificate: fCert.text,
                                splitProfile: cform.splitProfile, editIndex: shell.editIndex,
                                editPath: cform.editPath });
                            if (ok) cform.close()
                        } } }
                    Rectangle { width: Math.max(88, cancelText.implicitWidth + 26); height: 32; radius: 8
                        color: cancelMa.containsMouse ? theme.surface : theme.bg; border.color: theme.border; border.width: 1
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Text { id: cancelText; anchors.centerIn: parent; text: qsTr("Cancel"); color: theme.text; font.pixelSize: 14 }
                        MouseArea { id: cancelMa; anchors.fill: parent; hoverEnabled: true; onClicked: cform.tryClose() } }
                }
            }
        }
    }
    ConfirmDialog {
        id: discardConfirm
        objectName: "discardConfirm"
        theme: createRoot.theme
        // The window's own confirm dialog is drawn above this one; while it is up,
        // Return and Escape are its.
        escapeOwner: !shell.windowPopupOpen
        text: qsTr("Discard unsaved changes?")
        confirmText: qsTr("Discard")
        onConfirmed: cform.close()
    }
}
