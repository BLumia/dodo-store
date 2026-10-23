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
import org.deepin.dtk.settings 1.0 as Settings
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
    // Set once the DConfig-backed settings object is ready; guards the change
    // handlers while DConfig populates it during construction.
    property bool storeSettingsReady: false

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
        root.pageIndex = 0
        explorePage.searchText = text
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

    // Push the persisted store API settings to the backend. Called on startup
    // and whenever DConfig reports a change.
    function applyStoreSettings() {
        flatpakBackend.configureStoreApi(storeSettings.apiEnabled,
                                         storeSettings.apiBaseUrl,
                                         storeSettings.apiProxy)
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
        function onLaunchFinished(success, name, error) {
            if (!success)
                root.showToast(qsTr("Failed to open %1: %2").arg(name).arg(error))
        }
        function onHomeRefreshFailed(error) {
            root.showToast(qsTr("Store API refresh failed, showing cached data: %1").arg(error))
        }
        // A "flatpak+https" web link resolved to an app: open its detail page.
        function onOpenAppRequested(appId, branch) {
            root.showPage(0)
            var item = flatpakBackend.findAppItem(appId, branch)
            if (item)
                root.openDetail(item)
            else
                root.showToast(qsTr("Application not found in the selected source: %1").arg(appId))
        }
        function onFlatpakRefFailed(url, error) {
            root.showToast(qsTr("Cannot open link: %1").arg(error))
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
                // Also navigates back from a Browse category, so the page does
                // not need its own back button.
                visible: mainStack.depth > 1
                         || (root.pageIndex === 1 && browsePage.appsView
                             && browsePage.searchText === "")
                onClicked: {
                    if (mainStack.depth > 1)
                        mainStack.pop()
                    else
                        browsePage.backToCategories()
                }
            }

            Item { Layout.fillWidth: true }

            // Do not clear focus when the user clicks inside the search field.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: function(eventPoint, button) {
                    const p = eventPoint.position
                    if (p.x < searchField.x || p.x > searchField.x + searchField.width
                            || p.y < searchField.y || p.y > searchField.y + searchField.height)
                        searchField.focus = false
                }
            }

            SearchEdit {
                id: searchField
                Layout.preferredWidth: 320
                Layout.alignment: Qt.AlignCenter
                placeholder: qsTr("Search applications")
                onTextChanged: root.applyGlobalSearch(text)
            }

            Item { Layout.fillWidth: true }
        }

        menu: Menu {
            MenuItem {
                text: qsTr("Settings")
                onTriggered: root.openSettings()
            }
            MenuSeparator {}
            ThemeMenu {}
            MenuSeparator {}
            HelpAction {}
            AboutAction { aboutDialog: titleBar.aboutDialog }
            QuitAction {}
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

    // Store API settings, persisted through DConfig. The metadata lives in
    // packaging/dsg/configs/dodo-store/dodo-store.json.
    Config {
        id: storeSettings
        name: "dodo-store"

        property bool apiEnabled: true
        property string apiBaseUrl: "https://flathub.org/api/v2"
        property string apiProxy: ""

        // DConfig fills the properties above while the Config is still being
        // constructed; applying them from a change handler at that point would
        // re-enter the half-built object (and crash). Wait for completion.
        Component.onCompleted: {
            root.applyStoreSettings()
            root.storeSettingsReady = true
        }
        onApiEnabledChanged: if (root.storeSettingsReady) root.applyStoreSettings()
        onApiBaseUrlChanged: if (root.storeSettingsReady) root.applyStoreSettings()
        onApiProxyChanged: if (root.storeSettingsReady) root.applyStoreSettings()
    }

    // The dialog is created on demand and destroyed when closed. DConfig fills
    // the Config asynchronously; building the dialog before that leaves its
    // controls showing the defaults (e.g. the API checkbox appears checked even
    // though the persisted value is false). Recreating it on every open also
    // picks up values changed outside the dialog.
    Loader {
        id: settingsLoader
        active: false
        sourceComponent: Component {
            Settings.SettingsDialog {
                width: 720
                height: 520
                config: storeSettings

                onVisibleChanged: {
                    if (!visible)
                        Qt.callLater(function() { settingsLoader.active = false })
                }

                groups: [
                    Settings.SettingsGroup {
                        key: "store"
                        name: qsTr("Store")

                        children: [
                            Settings.SettingsGroup {
                                key: "api"
                                name: qsTr("Online content")

                                Settings.SettingsOption {
                                    key: "apiEnabled"
                                    name: qsTr("Enable store API")
                                    Settings.CheckBox {}
                                }
                                Settings.SettingsOption {
                                    key: "apiBaseUrl"
                                    name: qsTr("Store API address")
                                    Settings.LineEdit {}
                                }
                                Settings.SettingsOption {
                                    key: "apiProxy"
                                    name: qsTr("Proxy for the store API")
                                    Settings.LineEdit {}
                                }
                            }
                        ]
                    }
                ]
            }
        }
    }

    function openSettings() {
        settingsLoader.active = true
        if (settingsLoader.item)
            settingsLoader.item.show()
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

        // Clicking elsewhere in the window dismisses the search field focus.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: searchField.focus = false
        }

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

                    { label: qsTr("Explore"), icon: "view-grid" }

                ]
                delegate: ItemDelegate {
                    Layout.fillWidth: true
                    icon.source: modelData.iconSource
                    icon.width: 20
                    icon.height: 20
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

                    { label: qsTr("Installed"), iconSource: "qrc:/qml/icons/installed.svg", page: 1 },
                    { label: qsTr("Updates"), iconSource: "qrc:/qml/icons/updates.svg", page: 2 }

                ]
                delegate: ItemDelegate {
                    Layout.fillWidth: true
                    icon.source: modelData.iconSource
                    icon.width: 20
                    icon.height: 20
                    checked: root.pageIndex === modelData.page
                    text: modelData.label
                    onClicked: root.showPage(modelData.page)

                    Rectangle {
                        id: updatesBadge
                        visible: modelData.page === 3 && flatpakBackend.applicationUpdateCount > 0
                        width: Math.max(18, updatesBadgeText.implicitWidth + 8)
                        height: 18
                        radius: 9
                        color: "#d93025"
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            id: updatesBadgeText
                            anchors.centerIn: parent
                            text: flatpakBackend.applicationUpdateCount
                            color: "#ffffff"
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
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

                    { label: qsTr("Remotes"), iconSource: "qrc:/qml/icons/remotes.svg", page: 3 }

                ]
                delegate: ItemDelegate {
                    Layout.fillWidth: true
                    icon.source: modelData.iconSource
                    icon.width: 20
                    icon.height: 20
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

                ExplorePage {
                    id: explorePage
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
