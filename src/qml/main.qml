// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0
import org.deepin.dtk 1.0 as D
import org.deepin.dtk.style 1.0 as DS
import "pages"
import "components"

// Layout follows the DTK app pattern used by deepin-music: frameless window
// with a translucent, blurred sidebar column over the window background and a
// TitleBar with a centered search field.
ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    title: qsTr("Dodo Store")
    // Explicit window buttons: without these hints the DTK title bar shows no
    // minimize/maximize/close controls on dxcb.
    flags: Qt.Window | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    DWindow.enabled: true
    DWindow.enableBlurWindow: true
    DWindow.alphaBufferSize: 8
    color: "transparent"

    property int pageIndex: 0

    // The page instances hosted by the right-hand StackView, one per tab.
    property var pageStack: [null, null, null, null, null]

    // Push a populated AppDetailPage onto the stack for the given AppItem.
    function openDetail(item) {
        var detail = mainStack.push("pages/AppDetailPage.qml", { appItem: item })
        detail.backClicked.connect(function() { mainStack.pop() })
    }

    // Jump to the Browse tab and apply the search text typed in the title bar.
    function applyGlobalSearch(text) {
        while (mainStack.depth > 1)
            mainStack.pop()
        root.pageIndex = 1
        browsePage.searchText = text
    }

    function showPage(index) {
        // Popping any open detail pages when switching tabs.
        while (mainStack.depth > 1)
            mainStack.pop()
        root.pageIndex = index
    }

    // Show a transient toast on this window.
    function showToast(text) {
        DTK.sendMessage(root, text)
    }

    Connections {
        target: flatpakBackend
        function onOperationFinished(ref, success, error) {
            if (success)
                root.showToast(qsTr("Operation completed: %1").arg(ref))
            else
                root.showToast(qsTr("Operation failed: %1 — %2").arg(ref).arg(error))
        }
        function onRemoteOperationFinished(success, action, name, error) {
            if (!success) {
                root.showToast(qsTr("Remote operation failed: %1").arg(error))
            } else if (action === "add") {
                root.showToast(qsTr("Remote \"%1\" added").arg(name))
            } else if (action === "remove") {
                root.showToast(qsTr("Remote \"%1\" removed").arg(name))
            } else if (action === "edit") {
                root.showToast(qsTr("Remote \"%1\" updated").arg(name))
            }
        }
    }

    header: TitleBar {
        id: titleBar

        content: RowLayout {
            width: parent.width - 20
            anchors.left: parent.left
            anchors.leftMargin: 10
            spacing: 10

            IconButton {
                icon.name: "go-previous"
                visible: mainStack.depth > 1
                onClicked: mainStack.pop()
            }

            Item { Layout.fillWidth: true }

            SearchEdit {
                Layout.preferredWidth: 320
                Layout.alignment: Qt.AlignCenter
                placeholder: qsTr("Search applications")
                onTextChanged: root.applyGlobalSearch(text)
            }

            Item { Layout.fillWidth: true }
        }

        aboutDialog: AboutDialog {
            productName: qsTr("Dodo Store")
            productIcon: "application-x-executable"
            description: qsTr("A Flatpak graphical application store built with Qt6, CMake, QML and DTK.")
            websiteName: ""
            websiteLink: ""
            license: qsTr("Released under LGPL-3.0-or-later.")
        }
    }

    // Window background: blurred translucent sidebar column on the left
    // ( StyledBehindWindowBlur falls back to a plain tinted fill when the
    // compositor does not provide blur), flat fill for the content area.
    background: Rectangle {
        anchors.fill: parent
        color: "transparent"

        Row {
            anchors.fill: parent

            D.StyledBehindWindowBlur {
                id: leftBgArea
                width: 220
                height: parent.height
                control: root
                blendColor: {
                    if (valid)
                        return DS.Style.control.selectColor(control ? control.palette.window : undefined,
                            Qt.rgba(0.97, 0.97, 0.97, 0.73),
                            Qt.rgba(0.15, 0.15, 0.15, 0.87))
                    return DS.Style.control.selectColor(undefined,
                        DS.Style.behindWindowBlur.lightNoBlurColor,
                        DS.Style.behindWindowBlur.darkNoBlurColor)
                }

                Rectangle {
                    width: 1 / Screen.devicePixelRatio
                    height: parent.height
                    anchors.right: parent.right
                    color: DTK.themeType === ApplicationHelper.LightType
                           ? Qt.rgba(0, 0, 0, 0.08)
                           : Qt.rgba(0, 0, 0, 0.50)
                }
            }

            Rectangle {
                width: parent.width - leftBgArea.width
                height: parent.height
                color: DTK.themeType === ApplicationHelper.LightType ? "#f7f7f7" : "#252525"
            }
        }
    }

    Item {
        anchors.fill: parent

        // Sidebar navigation, grouped like deepin-music's "Library"/"Playlists".
        ColumnLayout {
            width: 220
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 20
            spacing: 4

            Label {
                text: qsTr("Store")
                font: DTK.fontManager.t5
                color: palette.windowText
                opacity: 0.7
                Layout.leftMargin: 10
                Layout.bottomMargin: 4
            }

            Repeater {
                model: [
                    { label: qsTr("Home"), icon: "go-home" },
                    { label: qsTr("Browse"), icon: "view-grid" }
                ]
                delegate: ItemDelegate {
                    Layout.fillWidth: true
                    icon.name: modelData.icon
                    checked: root.pageIndex === index
                    text: modelData.label
                    onClicked: root.showPage(index)
                }
            }

            Item { Layout.preferredHeight: 20 }

            Label {
                text: qsTr("Library")
                font: DTK.fontManager.t5
                color: palette.windowText
                opacity: 0.7
                Layout.leftMargin: 10
                Layout.bottomMargin: 4
            }

            Repeater {
                model: [
                    { label: qsTr("Installed"), icon: "package-x-generic", page: 2 },
                    { label: qsTr("Updates"), icon: "software-update-available", page: 3 }
                ]
                delegate: ItemDelegate {
                    Layout.fillWidth: true
                    icon.name: modelData.icon
                    checked: root.pageIndex === modelData.page
                    text: modelData.label
                    onClicked: root.showPage(modelData.page)
                }
            }

            Item { Layout.preferredHeight: 20 }

            Label {
                text: qsTr("Settings")
                font: DTK.fontManager.t5
                color: palette.windowText
                opacity: 0.7
                Layout.leftMargin: 10
                Layout.bottomMargin: 4
            }

            Repeater {
                model: [
                    { label: qsTr("Remotes"), icon: "network-server", page: 4 }
                ]
                delegate: ItemDelegate {
                    Layout.fillWidth: true
                    icon.name: modelData.icon
                    checked: root.pageIndex === modelData.page
                    text: modelData.label
                    onClicked: root.showPage(modelData.page)
                }
            }

            Item { Layout.fillHeight: true }
        }

        StackView {
            id: mainStack
            x: 220
            width: parent.width - 220
            height: parent.height

            initialItem: StackLayout {
                id: tabLayout
                currentIndex: root.pageIndex

                HomePage {
                    openDetail: function(item) { root.openDetail(item) }
                }
                BrowsePage {
                    id: browsePage
                    openDetail: function(item) { root.openDetail(item) }
                }
                InstalledPage {
                    openDetail: function(item) { root.openDetail(item) }
                }
                UpdatesPage {
                    openDetail: function(item) { root.openDetail(item) }
                }
                RemotesPage {
                }
            }
        }
    }
}
