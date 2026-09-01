pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property var viewport
    required property Item imageItem
    required property var viewportAnimation
    required property var zoomAnimation
    required property var xAnimation
    required property var yAnimation

    readonly property alias pressed: mouseArea.pressed
    readonly property alias mouseX: mouseArea.mouseX
    readonly property alias mouseY: mouseArea.mouseY

    // Keep the gesture entry points on the pointer layer itself.  Besides
    // making the extracted layer independently testable, this preserves the
    // public surface that callers reached through the pointer area's parent
    // before the layer was split out of FlickableZoomable.
    function beginPinchZoom(centerX, centerY) {
        root.viewport.beginPinchZoom(centerX, centerY)
    }

    function updatePinchZoom(scale) {
        root.viewport.updatePinchZoom(scale)
    }

    function finishPinchZoom() {
        root.viewport.finishPinchZoom()
    }

    PinchArea {
        anchors.fill: parent
        enabled: root.viewport.pinchZoomEnabled && root.viewport.active
        z: 2

        onPinchStarted: pinch => {
            root.beginPinchZoom(pinch.center.x, pinch.center.y)
            root.updatePinchZoom(pinch.scale)
            pinch.accepted = true
        }
        onPinchUpdated: pinch => {
            root.updatePinchZoom(pinch.scale)
            pinch.accepted = true
        }
        onPinchFinished: pinch => {
            root.finishPinchZoom()
            pinch.accepted = true
        }
    }

    MouseArea {
        id: mouseArea
        objectName: "galleryViewerPointerArea"
        anchors.fill: parent
        enabled: root.viewport.active
        acceptedButtons: Qt.AllButtons

        property real mousePressedX: 0
        property real mousePressedY: 0
        property point lastPosition
        property real lastTime: 0
        property var velocityHistoryX: []
        property var velocityHistoryY: []
        readonly property int historySize: 5

        function updateVelocityHistory(history, velocity) {
            history.push(velocity)
            if (history.length > historySize)
                history.shift()
        }

        function averageVelocity(history) {
            if (!history.length)
                return 0
            return history.reduce((sum, value) => sum + value, 0)
                    / history.length
        }

        onPressed: mouse => {
            mousePressedX = mouse.x
            mousePressedY = mouse.y
            root.viewport.imagePressedX =
                    (mouse.x - root.imageItem.x) / root.viewport.zoomScale
            root.viewport.imagePressedY =
                    (mouse.y - root.imageItem.y) / root.viewport.zoomScale

            if (mouse.button === Qt.LeftButton) {
                lastPosition = Qt.point(mouse.x, mouse.y)
                lastTime = Date.now()
                velocityHistoryX = []
                velocityHistoryY = []
                root.viewportAnimation.stop()
            } else if (mouse.button === Qt.RightButton) {
                root.viewport.toggleZoomToFit(true)
            } else if (mouse.button === Qt.MiddleButton) {
                root.viewport.middleClickRequested()
            }
        }

        onPositionChanged: mouse => {
            if (!(mouse.buttons & Qt.LeftButton))
                return

            root.imageItem.x = mouseX - root.viewport.imagePressedX
                    * root.viewport.zoomScale
            root.imageItem.y = mouseY - root.viewport.imagePressedY
                    * root.viewport.zoomScale
            root.xAnimation.to = mouseX - root.viewport.imagePressedX
                    * root.zoomAnimation.to
            root.yAnimation.to = mouseY - root.viewport.imagePressedY
                    * root.zoomAnimation.to
            root.viewportAnimation.restart()

            const currentTime = Date.now()
            const elapsed = currentTime - lastTime || 1
            updateVelocityHistory(velocityHistoryX,
                                  (mouseX - lastPosition.x) / elapsed * 1000)
            updateVelocityHistory(velocityHistoryY,
                                  (mouseY - lastPosition.y) / elapsed * 1000)
            lastPosition = Qt.point(mouseX, mouseY)
            lastTime = currentTime
        }

        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton)
                return

            const paused = Date.now() - lastTime > 100
            const velocityX = averageVelocity(velocityHistoryX)
            const velocityY = averageVelocity(velocityHistoryY)
            const outsideBounds = root.imageItem.x > 0
                    || root.imageItem.y > 0
                    || root.imageItem.x + root.imageItem.width
                       < root.viewport.width
                    || root.imageItem.y + root.imageItem.height
                       < root.viewport.height
            const targetX = root.imageItem.x
                    + (paused && !outsideBounds ? 0 : velocityX * 0.1)
            const targetY = root.imageItem.y
                    + (paused && !outsideBounds ? 0 : velocityY * 0.1)

            root.xAnimation.to =
                    root.viewport.fitViewerImageInViewportBoundsX(targetX)
            root.yAnimation.to =
                    root.viewport.fitViewerImageInViewportBoundsY(targetY)
            root.zoomAnimation.to = root.viewport.zoomScale
            root.xAnimation.duration = 500
            root.yAnimation.duration = 500
            root.zoomAnimation.duration = 0
            root.viewportAnimation.easing = Easing.OutCirc
            root.viewportAnimation.restart()
        }

        onWheel: wheel => root.viewport.handleZoomWheel(
                     wheel.angleDelta.y, wheel.modifiers, wheel.buttons)

        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.viewport.closeRequested()
            if (mouse.button === Qt.LeftButton
                    && mousePressedX === mouse.x
                    && mousePressedY === mouse.y)
                root.viewport.clicked()
        }

        onClicked: mouse => {
            if (mouse.button === Qt.LeftButton
                    && mousePressedX === mouse.x
                    && mousePressedY === mouse.y)
                root.viewport.clicked()
        }
    }
}
