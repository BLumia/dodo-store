// SPDX-FileCopyrightText: 2026 dodo-store authors
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick 2.15
import org.deepin.dtk 1.0

// Row delegate for the Installed / Updates lists. Emits two signals that the
// host page decides what to do with: `clicked` (open detail), `actionClicked`
// (per-row primary action, e.g. uninstall or update).
//
// Anchored instead of a RowLayout on purpose: the right-hand columns are
// pinned to the right edge so version numbers and action buttons line up
// across rows regardless of the name/summary lengths.
Rectangle {
    id: root
    property bool hovered: false
    property string actionText: ""
    signal clicked()
    signal actionClicked()
    color: hovered ? Qt.alpha(palette.highlight, 0.15) : "transparent"
    radius: 12
    height: 72
    clip: true

    Rectangle {
        id: iconFrame
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 40
        height: 40
        radius: 8
        color: palette.base
        Image {
            anchors.fill: parent
            anchors.margins: 6
            source: model.icon
            fillMode: Image.PreserveAspectFit
            sourceSize { width: 28; height: 28 }
        }
    }

    Column {
        id: nameCol
        anchors.left: iconFrame.right
        anchors.leftMargin: 12
        anchors.right: verText.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            // Fall back to the app id when AppStream metadata is missing
            // (base apps / runtimes listed without a display name).
            text: model.name || model.appId
            font: DTK.fontManager.t7
            color: palette.windowText
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        Text {
            width: parent.width
            text: model.summary || ""
            visible: text.length > 0
            font: DTK.fontManager.t9
            color: palette.windowText
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    Text {
        id: verText
        anchors.right: actionBtn.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 110
        text: model.version
        font: DTK.fontManager.t9
        color: palette.windowText
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
    }

    Button {
        id: actionBtn
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 130
        height: 36
        text: root.actionText
        visible: root.actionText.length > 0
        onClicked: root.actionClicked()
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: root.hovered = true
        onExited: root.hovered = false
        onClicked: root.clicked()
    }
}
