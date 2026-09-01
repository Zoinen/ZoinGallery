pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property var viewport
    required property Item imageItem

    readonly property alias verticalBar: verticalBar
    readonly property alias horizontalBar: horizontalBar
    readonly property alias animation: viewportAnimation
    readonly property alias zoomAnimation: zoomAnimation
    readonly property alias xAnimation: xAnimation
    readonly property alias yAnimation: yAnimation

    GalleryScrollBar {
        id: verticalBar
        objectName: "galleryViewerVerticalScrollBar"
        theme: root.viewport.scrollBarTheme
        anchors {
            right: parent.right
            rightMargin: root.viewport.scrollBarsRightMargin
            top: parent.top
            topMargin: root.viewport.topInset
            bottom: parent.bottom
        }
        z: 1
        width: 16
        size: root.viewport.height / root.imageItem.height
        position: ((-root.imageItem.y)
                   / (root.imageItem.height - root.viewport.height))
                  * (1 - size)
        orientation: Qt.Vertical
        active: root.viewport.dragZoomActive
        visible: !root.viewport.hideVerticalScrollBar
                 && (!root.viewport.zoomFitView || viewportAnimation.running)
                 && size < 1
        opacity: 0

        onPositionChanged: {
            if (pressed) {
                root.imageItem.y = position
                        * (-root.viewport.height + root.imageItem.height)
                        / (size - 1)
            }
        }
    }

    GalleryScrollBar {
        id: horizontalBar
        objectName: "galleryViewerHorizontalScrollBar"
        theme: root.viewport.scrollBarTheme
        anchors {
            left: parent.left
            right: parent.right
            rightMargin: root.viewport.scrollBarsRightMargin
            bottom: parent.bottom
        }
        z: 1
        height: 16
        size: root.viewport.width / root.imageItem.width
        position: ((-root.imageItem.x)
                   / (root.imageItem.width - root.viewport.width))
                  * (1 - size)
        orientation: Qt.Horizontal
        active: root.viewport.dragZoomActive
        visible: (!root.viewport.zoomFitView || viewportAnimation.running)
                 && size < 1
        opacity: 0

        onPositionChanged: {
            if (pressed) {
                root.imageItem.x = position
                        * (-root.viewport.width + root.imageItem.width)
                        / (size - 1)
            }
        }
    }

    ParallelAnimation {
        id: viewportAnimation
        property var easing: Easing.InOutQuad

        NumberAnimation {
            id: zoomAnimation
            target: root.viewport
            property: "zoomScale"
            easing.type: viewportAnimation.easing
        }
        NumberAnimation {
            id: xAnimation
            target: root.imageItem
            property: "x"
            easing.type: viewportAnimation.easing
        }
        NumberAnimation {
            id: yAnimation
            target: root.imageItem
            property: "y"
            easing.type: viewportAnimation.easing
        }
    }
}
