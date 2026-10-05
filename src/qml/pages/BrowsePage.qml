// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0
import dodo.store 1.0
import "../components"

Item {
    id: root
    property var openDetail: null

    // The search field lives in the window title bar (DTK style); the page
    // only receives the text.
    property string searchText: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Browse flatpak apps")
                font: DTK.fontManager.t8
                color: palette.windowText
            }

            Label {
                text: qsTr("Source:")
                color: palette.windowText
                opacity: 0.7
            }

            ComboBox {
                id: remoteBox
                Layout.preferredWidth: 200
                model: flatpakBackend.remotes

                // Keep the selection in sync when the remote list or the current
                // remote changes without breaking the user's own choice.
                function syncIndex() {
                    var i = flatpakBackend.remotes.indexOf(flatpakBackend.currentRemote)
                    if (i >= 0 && i !== currentIndex)
                        currentIndex = i
                }

                Component.onCompleted: syncIndex()

                Connections {
                    target: flatpakBackend
                    function onRemotesChanged() { remoteBox.syncIndex() }
                    function onCurrentRemoteChanged() { remoteBox.syncIndex() }
                }

                onActivated: {
                    flatpakBackend.currentRemote = flatpakBackend.remotes[index]
                    flatpakBackend.refreshBrowse()
                }
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: flatpakBackend.busy
            visible: running
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 200
            cellHeight: 190
            focus: true

            model: AppFilterProxyModel {
                id: filterModel
                sourceModel: flatpakBackend.browseModel
                filterText: root.searchText
            }

            delegate: AppCard {
                width: grid.cellWidth - 16
                height: grid.cellHeight - 16
                onClicked: {
                    if (root.openDetail)
                        root.openDetail(model.item)
                }
            }
        }

        Label {
            visible: grid.count === 0 && !flatpakBackend.busy
            Layout.alignment: Qt.AlignHCenter
            text: flatpakBackend.remotes.length === 0
                      ? qsTr("No Flatpak remotes configured.")
                      : qsTr("No applications found.")
            color: palette.windowText
        }
    }
}