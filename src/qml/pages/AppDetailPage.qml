// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0

// Detail page layout follows the deepin App Store detail view: a header with
// icon / title / summary + tags and the primary action on the right, a facts
// strip (category / package / size / version / developer), a screenshot
// gallery, then the homepage, description and "what's new". Data that the
// Flatpak/AppStream stack does not provide (ratings, download counts, paid
// content) is deliberately omitted.
Item {
    id: root
    // AppItem instance (populated by the openDetail callback in main.qml).
    property var appItem: null
    signal backClicked()

    // Snapshot of the display fields. The underlying AppItem can be deleted
    // at any time by a model refresh (e.g. right after an operation finishes),
    // so the page must not keep long-lived bindings to it.
    property string iconUrl: ""
    property string appName: ""
    property string appSummary: ""
    property string appDescription: ""
    property string appAppId: ""
    property string appVersion: ""
    property real appInstalledSize: 0
    property real appDownloadSize: 0
    property string appLicense: ""
    property string appRemote: ""
    property string appBranch: ""
    property string appArch: ""
    property string appDeveloper: ""
    property string appHomepage: ""
    property string appCategory: ""
    property string appReleaseNotes: ""
    property var appScreenshots: []

    // URL of the screenshot shown in the full-screen preview ("" = closed).
    property string previewUrl: ""

    // Screenshots are shown as a horizontal filmstrip (macOS App Store style)
    // with a fixed height and a natural, per-image width.
    readonly property int galleryTileHeight: 340

    function refKey() {
        return appRemote + ":" + appAppId + ":" + appBranch
    }

    // Dynamic state. The "count >= 0" term makes the binding depend on the
    // model count so it is re-evaluated (and re-queries the backend) every
    // time the lists are refreshed.
    readonly property bool installed: appAppId.length > 0
        && flatpakBackend.isInstalled(refKey())
    readonly property bool updateAvailable: appAppId.length > 0
        && flatpakBackend.updatesModel.count >= 0
        && flatpakBackend.hasUpdate(refKey())

    function snapshot() {
        if (!appItem)
            return
        iconUrl = appItem.icon || ""
        appName = appItem.name || appItem.appId || ""
        appSummary = appItem.summary || ""
        appDescription = appItem.description || ""
        appAppId = appItem.appId || ""
        appVersion = appItem.version || ""
        appInstalledSize = appItem.installedSize || 0
        appDownloadSize = appItem.downloadSize || 0
        appLicense = appItem.license || ""
        appRemote = appItem.remote || ""
        appBranch = appItem.branch || ""
        appArch = appItem.arch || ""
        appDeveloper = appItem.developer || ""
        appHomepage = appItem.homepage || ""
        appCategory = appItem.category || ""
        appReleaseNotes = appItem.releaseNotes || ""
        appScreenshots = appItem.screenshots || []
    }

    Component.onCompleted: snapshot()
    onAppItemChanged: snapshot()

    // AppStream enrichment is asynchronous and may land after the page opened;
    // re-snapshot when the fields we display are updated.
    Connections {
        target: root.appItem
        function onScreenshotsChanged() { root.snapshot() }
        function onDescriptionChanged() { root.snapshot() }
        function onDeveloperChanged() { root.snapshot() }
        function onHomepageChanged() { root.snapshot() }
        function onCategoryChanged() { root.snapshot() }
        function onReleaseNotesChanged() { root.snapshot() }
        function onNameChanged() { root.snapshot() }
        function onSummaryChanged() { root.snapshot() }
    }

    function fmtSize(bytes) {
        if (!bytes || bytes <= 0)
            return "--"
        if (bytes < 1024 * 1024)
            return Math.round(bytes / 1024) + " KB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
    }

    // Size shown in the facts strip: installed size once installed, otherwise
    // the download size advertised by the remote.
    readonly property real displaySize: appInstalledSize > 0 ? appInstalledSize
                                                             : appDownloadSize

    function copyToClipboard(text) {
        clipHelper.text = text
        clipHelper.selectAll()
        clipHelper.copy()
        DTK.sendMessage(root, qsTr("Copied to clipboard"))
    }

    // Hidden helper used to talk to the system clipboard.
    TextEdit {
        id: clipHelper
        visible: false
        width: 1
        height: 1
    }

    // A small labelled value cell used by the facts strip.
    component InfoCell: ColumnLayout {
        id: cell
        property string label: ""
        property string value: ""
        property bool copyable: false
        spacing: 4

        Text {
            Layout.fillWidth: true
            text: cell.label
            font: DTK.fontManager.t9
            color: palette.windowText
            opacity: 0.55
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                text: cell.value
                font: DTK.fontManager.t7
                color: palette.windowText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                ToolTip.visible: valueHover.hovered && cell.value.length > 0
                ToolTip.text: cell.value
                HoverHandler { id: valueHover }
            }

            Text {
                visible: cell.copyable
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Copy")
                font: DTK.fontManager.t9
                color: palette.highlight
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.copyToClipboard(cell.value)
                }
            }
        }
    }

    // A rounded tag chip (remote / category).
    component TagChip: Rectangle {
        property string label: ""
        implicitWidth: chipText.implicitWidth + 20
        implicitHeight: 24
        radius: 4
        color: Qt.rgba(palette.highlight.r, palette.highlight.g,
                       palette.highlight.b, 0.12)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            font: DTK.fontManager.t9
            color: palette.highlight
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + 48
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            x: 24
            y: 24
            width: flick.width - 48
            spacing: 20

            // ---------------------------------------------------------------
            // Header: icon, title / summary / tags, primary action.
            // ---------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 20

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    width: 96
                    height: 96
                    radius: 20
                    color: palette.base
                    Image {
                        anchors.fill: parent
                        anchors.margins: 12
                        source: root.iconUrl
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        sourceSize { width: 96; height: 96 }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: root.appName
                        font: DTK.fontManager.t4
                        color: palette.windowText
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.appSummary
                        font: DTK.fontManager.t7
                        color: palette.windowText
                        opacity: 0.75
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TagChip { label: root.appRemote }
                        TagChip {
                            visible: root.appCategory.length > 0
                            label: root.appCategory
                        }
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 8

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 8

                        // Primary action: Install, or Update when one is available.
                        Button {
                            id: primaryButton
                            visible: !root.installed || root.updateAvailable
                            enabled: !flatpakBackend.busy
                            implicitWidth: 112
                            implicitHeight: 36
                            text: root.updateAvailable ? qsTr("Update") : qsTr("Install")
                            contentItem: Text {
                                text: primaryButton.text
                                font: DTK.fontManager.t6
                                color: "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: primaryButton.pressed
                                       ? Qt.darker(palette.highlight, 1.15)
                                       : palette.highlight
                                opacity: primaryButton.enabled ? 1 : 0.5
                            }
                            onClicked: {
                                if (root.updateAvailable)
                                    flatpakBackend.updateApp(root.appAppId, root.appBranch, root.appArch)
                                else
                                    flatpakBackend.installApp(root.appAppId, root.appRemote,
                                                              root.appBranch, root.appArch)
                            }
                        }

                        Button {
                            visible: root.installed
                            enabled: !flatpakBackend.busy
                            implicitWidth: 112
                            implicitHeight: 36
                            text: qsTr("Uninstall")
                            onClicked: flatpakBackend.uninstallApp(root.appAppId,
                                                                   root.appBranch, root.appArch)
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignRight
                        visible: flatpakBackend.busy
                        text: Math.round(flatpakBackend.progress * 100) + "%"
                        font: DTK.fontManager.t9
                        color: palette.windowText
                        opacity: 0.6
                    }
                }
            }

            // Operation progress while busy.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: flatpakBackend.busy
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: flatpakBackend.progress * 100
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: flatpakBackend.status
                        font: DTK.fontManager.t9
                        color: palette.windowText
                        elide: Text.ElideRight
                    }
                    Button {
                        text: qsTr("Cancel")
                        onClicked: flatpakBackend.cancelOperation()
                    }
                }
            }

            // ---------------------------------------------------------------
            // Facts strip.
            // ---------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 68
                radius: 8
                color: Qt.rgba(palette.windowText.r, palette.windowText.g,
                               palette.windowText.b, 0.03)

                RowLayout {
                    id: factsRow
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 0

                    Repeater {
                        model: [
                            { label: qsTr("Category"), value: root.appCategory || "--", copyable: false, weight: 2 },
                            { label: qsTr("Package"), value: root.appAppId || "--", copyable: true, weight: 6 },
                            { label: qsTr("Size"), value: root.fmtSize(root.displaySize), copyable: false, weight: 2 },
                            { label: qsTr("Version"), value: root.appVersion || "--", copyable: false, weight: 2 },
                            { label: qsTr("Developer"), value: root.appDeveloper || "--", copyable: false, weight: 3 }
                        ]
                        delegate: RowLayout {
                            // Weighted cells (the package id needs the most room)
                            // plus four one-pixel dividers.
                            readonly property real totalWeight: 15
                            Layout.fillHeight: true
                            Layout.preferredWidth: (factsRow.width - 4) / totalWeight
                                                   * modelData.weight
                                                   + (index < 4 ? 1 : 0)
                            spacing: 0

                            InfoCell {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.topMargin: 14
                                Layout.bottomMargin: 14
                                label: modelData.label
                                value: modelData.value
                                copyable: modelData.copyable
                            }

                            Rectangle {
                                visible: index < 4
                                Layout.preferredWidth: 1
                                Layout.fillHeight: true
                                Layout.topMargin: 12
                                Layout.bottomMargin: 12
                                color: Qt.rgba(palette.windowText.r, palette.windowText.g,
                                               palette.windowText.b, 0.12)
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------------
            // Screenshot gallery (horizontal filmstrip, macOS App Store style).
            // ---------------------------------------------------------------
            Item {
                id: galleryArea
                Layout.fillWidth: true
                Layout.preferredHeight: root.galleryTileHeight
                visible: repeater.count > 0

                readonly property real maxContentX: Math.max(0, film.contentWidth - film.width)

                Flickable {
                    id: film
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: root.galleryTileHeight
                    contentWidth: galleryRow.width
                    contentHeight: galleryRow.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    interactive: contentWidth > width

                    Row {
                        id: galleryRow
                        height: root.galleryTileHeight
                        spacing: 12

                        Repeater {
                            id: repeater
                            model: root.appScreenshots

                            delegate: Rectangle {
                                id: tile
                                // Natural aspect ratio so nothing is cropped;
                                // fall back to 16:9 until the image loads.
                                height: root.galleryTileHeight
                                width: shotImage.implicitWidth > 0 && shotImage.implicitHeight > 0
                                       ? height * shotImage.implicitWidth / shotImage.implicitHeight
                                       : height * 16 / 9
                                radius: 12
                                // Flathub screenshots carry soft transparent
                                // margins; let the page background show through.
                                color: "transparent"
                                clip: true

                                Image {
                                    id: shotImage
                                    anchors.fill: parent
                                    source: modelData
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true
                                    sourceSize { width: 720; height: 720 }
                                }

                                // Slight overlay on hover so the tile reads as
                                // clickable.
                                Rectangle {
                                    anchors.fill: parent
                                    color: Qt.rgba(0, 0, 0, 0.12)
                                    opacity: hoverHandler.hovered ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 120 } }
                                }

                                HoverHandler { id: hoverHandler }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.previewUrl = modelData
                                }
                            }
                        }
                    }

                    // Translate the vertical wheel into horizontal scrolling
                    // while the pointer is over the filmstrip.
                    WheelHandler {
                        onWheel: function(event) {
                            if (!film.interactive)
                                return
                            const delta = event.angleDelta.y !== 0
                                          ? -event.angleDelta.y
                                          : -event.angleDelta.x
                            film.contentX = Math.max(0,
                                Math.min(galleryArea.maxContentX, film.contentX + delta))
                            event.accepted = true
                        }
                    }
                }

                // macOS-style edge navigation arrows.
                IconButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: film.verticalCenter
                    visible: film.contentX > 1
                    icon.name: "go-previous"
                    onClicked: film.contentX = Math.max(0, film.contentX - film.width * 0.8)
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: film.verticalCenter
                    visible: film.contentX < galleryArea.maxContentX - 1
                    icon.name: "go-next"
                    onClicked: film.contentX = Math.min(galleryArea.maxContentX,
                                                       film.contentX + film.width * 0.8)
                }
            }

            // ---------------------------------------------------------------
            // Homepage.
            // ---------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                visible: root.appHomepage.length > 0
                spacing: 8
                Text {
                    text: qsTr("Homepage:")
                    font: DTK.fontManager.t8
                    color: palette.windowText
                }
                Text {
                    Layout.fillWidth: true
                    text: root.appHomepage
                    font: DTK.fontManager.t8
                    color: palette.highlight
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally(root.appHomepage)
                    }
                }
            }

            // ---------------------------------------------------------------
            // Description.
            // ---------------------------------------------------------------
            Text {
                Layout.fillWidth: true
                visible: root.appDescription.length > 0
                text: root.appDescription
                font: DTK.fontManager.t8
                color: palette.windowText
                wrapMode: Text.Wrap
                lineHeight: 1.4
            }

            // ---------------------------------------------------------------
            // What's new (newest release notes).
            // ---------------------------------------------------------------
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: root.appReleaseNotes.length > 0
                text: qsTr("What's new")
                font: DTK.fontManager.t6
                color: palette.windowText
            }

            Text {
                Layout.fillWidth: true
                visible: root.appReleaseNotes.length > 0
                text: root.appReleaseNotes
                font: DTK.fontManager.t8
                color: palette.windowText
                opacity: 0.85
                wrapMode: Text.Wrap
                lineHeight: 1.4
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
            }
        }
    }

    // ---------------------------------------------------------------
    // Full-screen screenshot preview.
    // ---------------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        visible: root.previewUrl.length > 0
        color: Qt.rgba(0, 0, 0, 0.85)
        z: 10

        Image {
            anchors.fill: parent
            anchors.margins: 40
            anchors.bottomMargin: 64
            source: root.previewUrl
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            text: qsTr("Click anywhere to close")
            font: DTK.fontManager.t8
            color: "white"
            opacity: 0.8
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.previewUrl = ""
        }
    }
}
