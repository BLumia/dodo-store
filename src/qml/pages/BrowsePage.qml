// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0
import "../components"

Item {
    id: root
    property var openDetail: null

    // The search field lives in the window title bar (DTK style); the page
    // only receives the text. Search is scoped to the current listing: the
    // selected category, or all applications when the category tiles are shown.
    property string searchText: ""

    // Category tiles are shown first; selecting one switches to appsView and
    // fills the listing with that category (empty id = all applications).
    property bool appsView: false
    property string categoryId: ""
    property string categoryName: ""

    // AppStream category ids (CamelCase), the same ids used by AppStream.
    readonly property var categories: [
        { id: "", name: qsTr("All applications") },
        { id: "AudioVideo", name: qsTr("Audio & Video") },
        { id: "Development", name: qsTr("Development") },
        { id: "Education", name: qsTr("Education") },
        { id: "Game", name: qsTr("Games") },
        { id: "Graphics", name: qsTr("Graphics") },
        { id: "MedicalSoftware", name: qsTr("Health & Fitness") },
        { id: "Network", name: qsTr("Network") },
        { id: "Office", name: qsTr("Office") },
        { id: "Science", name: qsTr("Science") },
        { id: "System", name: qsTr("System") },
        { id: "Utility", name: qsTr("Utilities") }
    ]

    function openCategory(id, name) {
        root.categoryId = id
        root.categoryName = name
        root.appsView = true
        flatpakBackend.showBrowseCategory(id)
        appsGrid.positionViewAtBeginning()
    }

    // Called by the title bar's back button (see main.qml), like the detail
    // page's back navigation.
    function backToCategories() {
        root.appsView = false
        root.categoryId = ""
        root.categoryName = ""
        flatpakBackend.clearBrowseCategory()
    }

    onSearchTextChanged: {
        if (searchText !== "")
            flatpakBackend.searchBrowse(searchText)
        else if (appsView)
            flatpakBackend.showBrowseCategory(categoryId)
        else
            flatpakBackend.clearBrowseCategory()
        appsGrid.positionViewAtBeginning()
    }

    // Load the remote catalog lazily: Browse is hidden at startup, so this
    // keeps the full catalog out of the startup path.
    onVisibleChanged: {
        if (visible)
            flatpakBackend.refreshBrowse()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: {
                    if (root.searchText !== "")
                        return (root.appsView && root.categoryId !== "")
                               ? qsTr("Search results in %1").arg(root.categoryName)
                               : qsTr("Search results")
                    return root.appsView ? root.categoryName : qsTr("Browse flatpak apps")
                }
                font: DTK.fontManager.t8
                color: palette.windowText
                elide: Text.ElideRight
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
                    root.backToCategories()
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

        // Category tiles (initial Browse view).
        GridView {
            id: categoriesGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.appsView && root.searchText === ""
                     && flatpakBackend.remotes.length > 0
            clip: true
            cellWidth: 220
            cellHeight: 96

            model: root.categories

            delegate: Rectangle {
                id: categoryTile
                width: categoriesGrid.cellWidth - 12
                height: categoriesGrid.cellHeight - 12
                radius: 12
                color: tileArea.containsMouse
                       ? Qt.alpha(palette.highlight, 0.18)
                       : Qt.rgba(palette.windowText.r, palette.windowText.g,
                                 palette.windowText.b, 0.04)

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    text: modelData.name
                    font: DTK.fontManager.t6
                    color: palette.windowText
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openCategory(modelData.id, modelData.name)
                }
            }
        }

        // Applications of the selected category, or search results within it.
        // "All applications" grows in chunks: load the next one near the end.
        GridView {
            id: appsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.appsView || root.searchText !== ""
            clip: true
            cellWidth: 200
            cellHeight: 190
            focus: true

            model: flatpakBackend.browseListModel

            onContentYChanged: {
                if (contentHeight > height && contentY + height > contentHeight - 200)
                    flatpakBackend.loadMoreBrowse()
            }

            delegate: AppCard {
                width: appsGrid.cellWidth - 16
                height: appsGrid.cellHeight - 16
                onClicked: {
                    if (root.openDetail)
                        root.openDetail(model.item)
                }
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: text.length > 0
            text: {
                if (flatpakBackend.busy)
                    return ""
                if (root.searchText !== "" || root.appsView)
                    return appsGrid.count === 0 ? qsTr("No applications found.") : ""
                if (flatpakBackend.remotes.length === 0)
                    return qsTr("No Flatpak remotes configured.")
                return ""
            }
            color: palette.windowText
        }
    }
}
