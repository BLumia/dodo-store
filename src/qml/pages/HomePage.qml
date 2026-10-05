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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Discover applications")
            font: DTK.fontManager.t4
            color: palette.windowText
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("A curated selection of flatpak apps from the Flathub catalog.")
            font: DTK.fontManager.t8
            color: palette.windowText
            wrapMode: Text.Wrap
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 200
            cellHeight: 190
            model: flatpakBackend.browseModel
            delegate: AppCard {
                width: grid.cellWidth - 16
                height: grid.cellHeight - 16
                onClicked: {
                    if (root.openDetail)
                        root.openDetail(model.item)
                }
            }
        }
    }
}