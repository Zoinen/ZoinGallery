pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

Rectangle {
    id: root

    required property GalleryPanelController controller
    required property color backgroundColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    property real devicePixelRatio: 1

    readonly property real pixel: 1 / Math.max(1, devicePixelRatio)
    readonly property string query: controller.quickSearchQuery

    implicitWidth: Math.max(220, queryText.implicitWidth
                            + matchText.implicitWidth + 56)
    implicitHeight: 36
    radius: 8
    color: backgroundColor
    border.width: pixel
    border.color: borderColor
    clip: true

    Text {
        id: searchGlyph
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: "⌕"
        color: root.mutedTextColor
        font.pixelSize: 16
    }

    Text {
        id: queryText
        anchors.left: searchGlyph.right
        anchors.leftMargin: 8
        anchors.right: matchText.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: root.query
        color: root.textColor
        elide: Text.ElideLeft
        font.pixelSize: 13
    }

    Rectangle {
        id: pseudoCursor
        readonly property real textAdvance:
            queryMetrics.advanceWidth(queryText.text)
        x: Math.min(queryText.x + queryText.width,
                    queryText.x + textAdvance)
        y: queryText.y + 2
        width: Math.max(root.pixel, 2 * root.pixel)
        height: Math.max(root.pixel, queryText.height - 4)
        color: root.textColor
        opacity: blinkOn ? 1 : 0
        property bool blinkOn: true

        Connections {
            target: root.controller
            function onQuickSearchChanged() {
                pseudoCursor.blinkOn = true
                blinkTimer.restart()
            }
        }
    }

    FontMetrics {
        id: queryMetrics
        font: queryText.font
    }

    Text {
        id: matchText
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.controller.quickSearchMatchCount.toString()
        color: root.mutedTextColor
        font.pixelSize: 11
    }

    Timer {
        id: blinkTimer
        interval: 520
        repeat: true
        running: root.visible
        onTriggered: pseudoCursor.blinkOn = !pseudoCursor.blinkOn
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => mouse.accepted = true
    }
}
