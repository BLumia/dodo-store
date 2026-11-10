// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.deepin.dtk 1.0
import org.deepin.dtk.style 1.0 as DS
import "../components"

Item {
    id: root
    property var openDetail: null

    // With the store API enabled Home offers four curated lists; offline it
    // falls back to the two lists that can be derived locally.
    readonly property bool apiEnabled: flatpakBackend.storeApiEnabled
    readonly property var tabKeys: apiEnabled
        ? ["trending", "popular", "new", "updated"]
        : ["popular", "updated"]
    property int currentIndex: 0

    readonly property string currentKey:
        tabKeys.length > 0 ? tabKeys[Math.min(currentIndex, tabKeys.length - 1)] : ""
    readonly property var currentModel: tabModel(currentKey)
    readonly property string currentStatus: tabStatus(currentKey)

    function tabLabel(key) {
        switch (key) {
        case "trending": return qsTr("Trending")
        case "popular": return qsTr("Popular")
        case "new": return qsTr("New")
        case "updated": return qsTr("Updated")
        }
        return ""
    }

    function tabModel(key) {
        switch (key) {
        case "trending": return flatpakBackend.trendingModel
        case "popular": return flatpakBackend.popularModel
        case "new": return flatpakBackend.newModel
        case "updated": return flatpakBackend.updatedModel
        }
        return null
    }

    function tabStatus(key) {
        switch (key) {
        case "trending": return flatpakBackend.trendingStatus
        case "popular": return flatpakBackend.popularStatus
        case "new": return flatpakBackend.newStatus
        case "updated": return flatpakBackend.updatedStatus
        }
        return ""
    }

    onApiEnabledChanged: currentIndex = 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: qsTr("Discover applications")
                font: DTK.fontManager.t4
                color: palette.windowText
                elide: Text.ElideRight
            }

            Button {
                text: qsTr("Refresh")
                enabled: !flatpakBackend.busy
                onClicked: flatpakBackend.refreshHome()
            }
        }

        // Curated categories in a single segmented pill.
        Item {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: root.tabKeys.length * 112
            width: implicitWidth
            height: 36

            Rectangle {
                anchors.fill: parent
                radius: DS.Style.control.radius
                color: Qt.alpha(palette.windowText, 0.06)
                border.width: 1
                border.color: Qt.alpha(palette.windowText, 0.08)
            }

            Rectangle {
                x: root.currentIndex * width
                width: parent.width / Math.max(1, root.tabKeys.length)
                height: parent.height
                radius: DS.Style.control.radius
                color: palette.button
                border.width: 1
                border.color: Qt.alpha(palette.windowText, 0.10)

                Behavior on x {
                    NumberAnimation {
                        duration: 180
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Row {
                anchors.fill: parent

                Repeater {
                    model: root.tabKeys

                    Item {
                        width: parent.width / Math.max(1, root.tabKeys.length)
                        height: parent.height

                        Text {
                            anchors.centerIn: parent
                            text: root.tabLabel(modelData)
                            color: palette.windowText
                            opacity: index === root.currentIndex ? 1.0 : 0.72
                            font: DTK.fontManager.t6
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentIndex = index
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors.fill: parent
                clip: true
                cellWidth: 200
                cellHeight: 190
                visible: root.currentModel && root.currentModel.count > 0

                model: root.currentModel

                delegate: AppCard {
                    width: grid.cellWidth - 16
                    height: grid.cellHeight - 16
                    onClicked: {
                        if (root.openDetail)
                            root.openDetail(model.item)
                    }
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.currentStatus === "loading"
                         && root.currentModel && root.currentModel.count === 0
                visible: running
            }

            Label {
                anchors.centerIn: parent
                visible: root.currentModel && root.currentModel.count === 0
                         && root.currentStatus === "error"
                text: qsTr("Failed to load. Check the store API settings and retry.")
                color: palette.windowText
                opacity: 0.75
            }

            Label {
                anchors.centerIn: parent
                visible: root.currentModel && root.currentModel.count === 0
                         && (root.currentStatus === "ok" || root.currentStatus === "stale")
                text: qsTr("No applications")
                color: palette.windowText
                opacity: 0.75
            }
        }
    }
}
