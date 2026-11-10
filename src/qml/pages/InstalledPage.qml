// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.deepin.dtk 1.0
import dodo.store 1.0
import "../components"

Item {
    id: root
    property var openDetail: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        AppFilterProxyModel {
            id: installedCountModel
            sourceModel: flatpakBackend.installedModel
            hideRuntimes: true
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("%1 applications installed").arg(installedCountModel.count)
            font: DTK.fontManager.t8
            color: palette.windowText
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: AppFilterProxyModel {
                sourceModel: flatpakBackend.installedModel
                hideRuntimes: true
            }
            delegate: AppListItem {
                width: list.width
                actionText: qsTr("Uninstall")
                onClicked: {
                    if (root.openDetail)
                        root.openDetail(model.item)
                }
                onActionClicked: flatpakBackend.uninstallApp(model.appId, model.branch, model.arch)
            }
        }

        Label {
            visible: list.count === 0 && !flatpakBackend.busy
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Nothing installed yet.")
            color: palette.windowText
        }
    }
}