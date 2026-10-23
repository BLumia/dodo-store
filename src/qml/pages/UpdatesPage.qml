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

    // The model holds application updates first, then runtime updates. This
    // keeps the header counts and the "Update All" state without a second model.
    readonly property int runtimeUpdateCount:
        Math.max(0, flatpakBackend.updatesModel.count - flatpakBackend.applicationUpdateCount)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 application updates").arg(flatpakBackend.applicationUpdateCount)
                      + (root.runtimeUpdateCount > 0
                         ? qsTr(" · %1 runtime updates").arg(root.runtimeUpdateCount)
                         : "")
                font: DTK.fontManager.t8
                color: palette.windowText
            }
            Button {
                text: qsTr("Update All")
                enabled: flatpakBackend.updatesModel.count > 0 && !flatpakBackend.busy
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

        // One list for both kinds of update, grouped with ListView sections.
        // The backend keeps application rows before runtime rows so each
        // section is contiguous (isRuntime false first, then true).
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: flatpakBackend.updatesModel

            section.property: "isRuntime"
            section.delegate: Label {
                width: list.width
                height: 36
                leftPadding: 4
                verticalAlignment: Text.AlignBottom
                text: String(section) === "true"
                      ? qsTr("Runtimes and dependencies")
                      : qsTr("Applications")
                font: DTK.fontManager.t6
                color: palette.windowText
                opacity: 0.85
            }

            delegate: AppListItem {
                width: list.width
                // Runtimes are updated through Update All / as dependencies,
                // so only applications get a per-row Update button and a
                // detail page.
                actionText: model.isRuntime ? "" : qsTr("Update")
                onClicked: {
                    if (!model.isRuntime && root.openDetail)
                        root.openDetail(model.item)
                }
                onActionClicked: {
                    if (!model.isRuntime)
                        flatpakBackend.updateApp(model.appId, model.branch, model.arch)
                }
            }
        }

        Label {
            visible: flatpakBackend.updatesModel.count === 0 && !flatpakBackend.busy
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("You're up to date.")
            color: palette.windowText
        }
    }
}
