// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.deepin.dtk 1.0
import "../components"

Item {
    id: root
    property var openDetail: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 application updates").arg(flatpakBackend.updatesModel.count)
                      + (flatpakBackend.runtimeUpdatesModel.count > 0
                         ? qsTr(" · %1 runtime updates").arg(flatpakBackend.runtimeUpdatesModel.count)
                         : "")
                font: DTK.fontManager.t8
                color: palette.windowText
            }
            Button {
                text: qsTr("Update All")
                enabled: (flatpakBackend.updatesModel.count + flatpakBackend.runtimeUpdatesModel.count) > 0
                         && !flatpakBackend.busy
                onClicked: flatpakBackend.updateAll()
            }
            Button {
                text: qsTr("Check for updates")
                onClicked: flatpakBackend.refreshInstalled()
            }
            Button {
                text: qsTr("Cancel")
                visible: flatpakBackend.busy
                onClicked: flatpakBackend.cancelOperation()
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: flatpakBackend.busy
            visible: running
        }

        Label {
            visible: flatpakBackend.updatesModel.count > 0
            Layout.fillWidth: true
            text: qsTr("Applications")
            font: DTK.fontManager.t6
            color: palette.windowText
        }

        ListView {
            id: appList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 360)
            Layout.minimumHeight: 0
            clip: true
            spacing: 4
            model: flatpakBackend.updatesModel
            delegate: AppListItem {
                width: appList.width
                actionText: qsTr("Update")
                onClicked: {
                    if (root.openDetail)
                        root.openDetail(model.item)
                }
                onActionClicked: flatpakBackend.updateApp(model.appId, model.branch, model.arch)
            }
        }

        Label {
            visible: flatpakBackend.runtimeUpdatesModel.count > 0
            Layout.fillWidth: true
            text: qsTr("Runtimes and dependencies")
            font: DTK.fontManager.t6
            color: palette.windowText
        }

        ListView {
            id: runtimeList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 180)
            visible: count > 0
            clip: true
            spacing: 4
            model: flatpakBackend.runtimeUpdatesModel
            delegate: AppListItem {
                width: runtimeList.width
                actionText: ""
                onClicked: {}
            }
        }

        Label {
            visible: flatpakBackend.updatesModel.count === 0
                     && flatpakBackend.runtimeUpdatesModel.count === 0
                     && !flatpakBackend.busy
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("You're up to date.")
            color: palette.windowText
        }
    }
}