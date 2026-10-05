// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0

// Remote (repository) management: list the configured Flatpak remotes, enable
// or disable them, remove them, and add new ones by URL or ".flatpakrepo" file.
Item {
    id: root

    function remoteLabel(info) {
        if (info.title && info.title.length > 0)
            return info.title
        return info.name
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: qsTr("Remotes")
                font: DTK.fontManager.t4
                color: palette.windowText
            }

            BusyIndicator {
                running: flatpakBackend.busy
                visible: running
            }

            Button {
                text: qsTr("Add remote")
                onClicked: addDialog.open()
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Flatpak repositories the store browses and installs from. Disable a remote to stop using it without deleting it.")
            font: DTK.fontManager.t8
            color: palette.windowText
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: flatpakBackend.remotesInfo

            delegate: Rectangle {
                width: list.width
                height: 94
                radius: 8
                color: DTK.themeType === ApplicationHelper.LightType
                       ? Qt.rgba(0, 0, 0, 0.03)
                       : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: DTK.themeType === ApplicationHelper.LightType
                              ? Qt.rgba(0, 0, 0, 0.07)
                              : Qt.rgba(1, 1, 1, 0.10)

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            Layout.fillWidth: true
                            text: root.remoteLabel(modelData)
                            font: DTK.fontManager.t6
                            color: palette.windowText
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.url
                            font: DTK.fontManager.t9
                            color: palette.windowText
                            opacity: 0.75
                            elide: Text.ElideMiddle
                        }

                        RowLayout {
                            spacing: 10

                            Text {
                                text: modelData.disabled ? qsTr("Disabled") : qsTr("Enabled")
                                font: DTK.fontManager.t9
                                color: modelData.disabled ? palette.windowText : palette.highlight
                                opacity: modelData.disabled ? 0.5 : 1
                            }

                            Text {
                                text: modelData.gpgVerify
                                      ? qsTr("GPG verification")
                                      : qsTr("No GPG verification")
                                font: DTK.fontManager.t9
                                color: palette.windowText
                                opacity: 0.5
                            }
                        }
                    }

                    // The remote's short name, vertically centred with the
                    // controls on the right.
                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.rightMargin: 4
                        text: modelData.name
                        font: DTK.fontManager.t9
                        color: palette.windowText
                        opacity: 0.5
                    }

                    Switch {
                        checked: !modelData.disabled
                        onClicked: flatpakBackend.setRemoteEnabled(modelData.name, checked)
                    }

                    Button {
                        text: qsTr("Edit")
                        onClicked: editDialog.load(modelData)
                    }

                    Button {
                        text: qsTr("Remove")
                        onClicked: {
                            removeDialog.remoteName = modelData.name
                            removeDialog.remoteTitle = root.remoteLabel(modelData)
                            removeDialog.open()
                        }
                    }
                }
            }
        }

        Text {
            visible: list.count === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            text: qsTr("No remotes configured.")
            color: palette.windowText
            opacity: 0.7
        }
    }

    // Confirmation before deleting a remote.
    Dialog {
        id: removeDialog
        property string remoteName: ""
        property string remoteTitle: ""

        anchors.centerIn: parent
        modal: true
        title: qsTr("Remove remote")
        width: 460
        standardButtons: Dialog.Cancel | Dialog.Ok

        contentItem: Text {
            wrapMode: Text.Wrap
            color: palette.windowText
            text: qsTr("Remove the remote \"%1\"? Applications installed from it are kept, but will no longer receive updates from it.").arg(removeDialog.remoteTitle)
        }

        onAccepted: flatpakBackend.removeRemote(removeDialog.remoteName)
    }

    // Add a new remote.
    Dialog {
        id: addDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Add remote")
        width: 470
        standardButtons: Dialog.Cancel | Dialog.Ok

        function updateOk() {
            var b = standardButton(Dialog.Ok)
            if (b)
                b.enabled = nameField.text.length > 0 && urlField.text.length > 0
        }

        contentItem: ColumnLayout {
            spacing: 6

            Text {
                text: qsTr("Name")
                font: DTK.fontManager.t9
                color: palette.windowText
                opacity: 0.8
            }
            LineEdit {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. flathub")
                onTextChanged: addDialog.updateOk()
            }

            Text {
                Layout.topMargin: 8
                text: qsTr("URL")
                font: DTK.fontManager.t9
                color: palette.windowText
                opacity: 0.8
            }
            LineEdit {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("https://flathub.org/repo/flathub.flatpakrepo")
                onTextChanged: addDialog.updateOk()
            }

            CheckBox {
                id: gpgBox
                Layout.topMargin: 8
                checked: true
                text: qsTr("Require GPG verification")
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("For a .flatpakrepo URL the GPG key is read from the file and this option is ignored.")
                wrapMode: Text.Wrap
                font: DTK.fontManager.t9
                color: palette.windowText
                opacity: 0.6
            }
        }

        onOpened: {
            nameField.text = ""
            urlField.text = ""
            gpgBox.checked = true
            updateOk()
        }

        onAccepted: flatpakBackend.addRemote(nameField.text, urlField.text, gpgBox.checked)
    }

    // Edit an existing remote (URL, title, GPG verification, listing visibility
    // and priority). The remote's name is its key and cannot be changed.
    Dialog {
        id: editDialog
        property string remoteName: ""
        property string displayName: ""

        anchors.centerIn: parent
        modal: true
        title: qsTr("Edit remote")
        width: 480
        standardButtons: Dialog.Cancel | Dialog.Ok

        function updateOk() {
            var b = standardButton(Dialog.Ok)
            if (b)
                b.enabled = editUrlField.text.length > 0
        }

        // Populate the dialog from a remotesInfo entry and show it.
        function load(info) {
            remoteName = info.name
            displayName = root.remoteLabel(info)
            editTitleField.text = info.title ? info.title : ""
            editUrlField.text = info.url ? info.url : ""
            editGpgBox.checked = info.gpgVerify === true
            editNoEnumBox.checked = info.noEnumerate === true
            editPrioBox.value = info.prio !== undefined ? info.prio : 1
            updateOk()
            open()
        }

        contentItem: ColumnLayout {
            spacing: 6

            Text {
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                text: qsTr("Editing \"%1\"").arg(editDialog.displayName)
                font: DTK.fontManager.t8
                color: palette.windowText
                opacity: 0.7
                elide: Text.ElideRight
            }

            Text {
                text: qsTr("Title")
                font: DTK.fontManager.t9
                color: palette.windowText
                opacity: 0.8
            }
            LineEdit {
                id: editTitleField
                Layout.fillWidth: true
                placeholderText: qsTr("Display name")
            }

            Text {
                Layout.topMargin: 8
                text: qsTr("URL")
                font: DTK.fontManager.t9
                color: palette.windowText
                opacity: 0.8
            }
            LineEdit {
                id: editUrlField
                Layout.fillWidth: true
                placeholderText: qsTr("https://dl.flathub.org/repo/")
                onTextChanged: editDialog.updateOk()
            }

            CheckBox {
                id: editGpgBox
                Layout.topMargin: 8
                text: qsTr("Require GPG verification")
            }

            CheckBox {
                id: editNoEnumBox
                text: qsTr("Hide apps from search and listings")
            }

            RowLayout {
                Layout.topMargin: 8
                spacing: 10

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Priority")
                    font: DTK.fontManager.t9
                    color: palette.windowText
                    opacity: 0.8
                }
                SpinBox {
                    id: editPrioBox
                    from: -100
                    to: 100
                    stepSize: 1
                }
                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Higher priority wins when two remotes provide the same app.")
                    wrapMode: Text.Wrap
                    font: DTK.fontManager.t9
                    color: palette.windowText
                    opacity: 0.6
                }
            }
        }

        onAccepted: flatpakBackend.editRemote(editDialog.remoteName, editTitleField.text,
                                              editUrlField.text, editGpgBox.checked,
                                              editNoEnumBox.checked, editPrioBox.value)
    }
}
