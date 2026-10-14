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
    property string searchText: ""

    // Curated default list; a non-empty search covers the full loaded catalog.
    readonly property var featuredAppIds: [
        "org.mozilla.firefox",
        "com.google.Chrome",
        "com.spotify.Client",
        "com.valvesoftware.Steam",
        "org.videolan.VLC",
        "org.libreoffice.LibreOffice",
        "org.gimp.GIMP",
        "org.telegram.desktop",
        "com.discordapp.Discord",
        "com.visualstudio.code",
        "org.kde.kdenlive",
        "org.audacityteam.Audacity",
        "org.inkscape.Inkscape",
        "org.blender.Blender",
        "com.obsproject.Studio",
        "org.keepassxc.KeePassXC",
        "org.signal.Signal",
        "com.slack.Slack",
        "org.qbittorrent.qBittorrent",
        "com.usebottles.bottles"
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.searchText.trim().length > 0
                      ? qsTr("Search results")
                      : qsTr("Popular applications")
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
                sourceModel: flatpakBackend.browseModel
                filterText: root.searchText.trim()
                featuredOnly: true
                featuredAppIds: root.featuredAppIds
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
                  : root.searchText.trim().length > 0
                    ? qsTr("No applications found.")
                    : qsTr("No featured applications are available in this source.")
            color: palette.windowText
        }
    }
}