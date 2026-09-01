pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl

Button {
    id: titleButton

    objectName: "titleBarButton"

    property real devicePixelRatio:
        titleButton.Window.window && titleButton.Window.window.screen
        ? titleButton.Window.window.screen.devicePixelRatio : 1.0
    readonly property real iconLogicalSize: 10
    readonly property real pixelAlignmentRevision:
        titleButton.Window.window
        ? titleButton.Window.window.width + titleButton.Window.window.height
          + devicePixelRatio : devicePixelRatio
    function snap(value) {
        return Math.round(Number(value || 0) * devicePixelRatio)
               / devicePixelRatio
    }
    function iconPixelOffsetX(item) {
        if (!item || !item.parent)
            return 0
        const revision = pixelAlignmentRevision
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return snap(scenePoint.x) - scenePoint.x + revision * 0
    }
    function iconPixelOffsetY(item) {
        if (!item || !item.parent)
            return 0
        const revision = pixelAlignmentRevision
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return snap(scenePoint.y) - scenePoint.y + revision * 0
    }

    implicitHeight: titleBar.height
    implicitWidth: snap(46)

    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0
    leftInset: 0
    topInset: 0
    rightInset: 0
    bottomInset: 0

    opacity: topLevelWindow.active || titleButton.hoveredOverride ? 1 : 0.4
    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
    }

    property alias source: titleIcon.source
    property bool pressedOverride: pressed
    property bool hoveredOverride: hovered
    property color backgroundColor: {
        if (!titleButton.enabled) {
            return "gray";
        }
        if (titleButton.pressedOverride) {
            return Style.darker;
        }
        if (titleButton.hoveredOverride) {
            return Style.lighter;
        }
        return "transparent";
    }

    icon.width: iconLogicalSize
    icon.height: iconLogicalSize
    icon.color: Style.text

    contentItem: Item {
        IconImage {
            id: titleIcon
            objectName: "titleBarButtonIcon"
            anchors.centerIn: parent
            width: titleButton.snap(titleButton.iconLogicalSize)
            height: titleButton.snap(titleButton.iconLogicalSize)
            sourceSize: Qt.size(titleButton.iconLogicalSize,
                                titleButton.iconLogicalSize)
            color: titleButton.icon.color
            fillMode: Image.PreserveAspectFit
            smooth: false
            mipmap: false
            transform: Translate {
                x: titleButton.iconPixelOffsetX(titleIcon)
                y: titleButton.iconPixelOffsetY(titleIcon)
            }
        }
    }
    background: Rectangle {
        color: backgroundColor
    }
}
