pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

Rectangle {
    id: root
    objectName: "galleryQuickSearchOverlay"

    required property GalleryPanelController controller
    required property color backgroundColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    property real devicePixelRatio: 1

    readonly property real pixel: 1 / Math.max(1, devicePixelRatio)
    readonly property string query: controller.quickSearchQuery
    property int blinkInterval: 520
    property int blinkTransitionsRemaining: 0
    readonly property bool blinkTimerRunning: blinkTimer.running
    readonly property bool blinkEligible:
        visible && (!Window.window || Window.window.active)

    function settleBlink() {
        blinkTimer.stop()
        blinkTransitionsRemaining = 0
        pseudoCursor.blinkOn = true
    }

    function restartBlink() {
        blinkTimer.stop()
        pseudoCursor.blinkOn = true
        if (!blinkEligible) {
            blinkTransitionsRemaining = 0
            return
        }
        blinkTransitionsRemaining = 2
        blinkTimer.start()
    }

    onBlinkEligibleChanged: {
        if (blinkEligible)
            restartBlink()
        else
            settleBlink()
    }

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
        objectName: "galleryQuickSearchCursor"
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
                root.restartBlink()
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
        interval: root.blinkInterval
        repeat: false
        onTriggered: {
            if (!root.blinkEligible
                    || root.blinkTransitionsRemaining <= 0) {
                root.settleBlink()
                return
            }
            pseudoCursor.blinkOn = !pseudoCursor.blinkOn
            --root.blinkTransitionsRemaining
            if (root.blinkTransitionsRemaining > 0)
                blinkTimer.start()
            else
                pseudoCursor.blinkOn = true
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => mouse.accepted = true
    }
}
