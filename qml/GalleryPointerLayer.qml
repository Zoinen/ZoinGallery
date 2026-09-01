pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: pointerLayer
    required property Item layout
    required property string wheelMode
    required property string presentationMode
    required property bool densityAdjustmentEnabled
    required property bool primarySelectionEnabled
    required property real autoScrollExtent

    readonly property alias scrollingStarted: autoScroll.scrollingStarted
    readonly property alias scrollingStartedAtY: autoScroll.startCoordinate
    readonly property alias scrollingMode: autoScroll.scrollingMode

    signal wheelRequested(real x, real y, real pixelDeltaY,
                          real angleDeltaY, int modifiers,
                          real pixelDeltaX, real angleDeltaX)
    signal consoleWheelRequested(real x, real y, real angleDeltaY,
                                 int modifiers)
    signal hoverMoved(real x, real y)
    signal hoverExited()
    signal middlePressed(real x, real y, int modifiers)
    signal middleReleased(real x, real y, int modifiers)
    signal consoleMiddleCanceled(real x, real y)
    signal pinchStarted()
    signal pinchUpdated(real scale)
    signal pinchFinished()
    signal primaryPressed(real x, real y, int button, int modifiers)
    signal primaryDragged(real x, real y)
    signal primaryReleased()
    signal primaryDoubleClicked(real x, real y, int button)

    function startAutoScroll() {
        autoScroll.start()
    }

    function endAutoScroll() {
        autoScroll.end()
    }

    MouseArea {
        objectName: "galleryWheelArea"
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: wheel => {
            if (pointerLayer.wheelMode === "console") {
                pointerLayer.consoleWheelRequested(
                    wheel.x, wheel.y, wheel.angleDelta.y, wheel.modifiers)
            } else {
                pointerLayer.wheelRequested(
                    wheel.x, wheel.y, wheel.pixelDelta.y,
                    wheel.angleDelta.y, wheel.modifiers,
                    wheel.pixelDelta.x, wheel.angleDelta.x)
            }
            wheel.accepted = true
        }
    }

    // One stable pointer owner replaces a MouseArea in every materialized
    // delegate. Hit testing remains analytical in GalleryViewportItem, so
    // pointer cost is independent of the number of live rows.
    MouseArea {
        objectName: "galleryPrimaryPointerArea"
        anchors.fill: parent
        enabled: pointerLayer.primarySelectionEnabled
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true

        onPressed: mouse => {
            pointerLayer.primaryPressed(mouse.x, mouse.y, mouse.button,
                                        mouse.modifiers)
            mouse.accepted = true
        }
        onPositionChanged: mouse => {
            if (mouse.buttons & (Qt.LeftButton | Qt.RightButton))
                pointerLayer.primaryDragged(mouse.x, mouse.y)
        }
        onReleased: pointerLayer.primaryReleased()
        onCanceled: pointerLayer.primaryReleased()
        onDoubleClicked: mouse => {
            pointerLayer.primaryDoubleClicked(mouse.x, mouse.y,
                                              mouse.button)
            mouse.accepted = true
        }
    }

    MouseArea {
        id: middleButtonArea
        objectName: "galleryMiddleButtonArea"
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        hoverEnabled: true

        onPositionChanged: mouse =>
            pointerLayer.hoverMoved(mouse.x, mouse.y)
        onContainsMouseChanged: {
            if (containsMouse)
                pointerLayer.hoverMoved(mouseX, mouseY)
            else
                pointerLayer.hoverExited()
        }
        onPressed: mouse => {
            pointerLayer.middlePressed(mouse.x, mouse.y, mouse.modifiers)
            mouse.accepted = true
        }
        onReleased: mouse => {
            pointerLayer.middleReleased(mouse.x, mouse.y, mouse.modifiers)
            mouse.accepted = true
        }
        onCanceled: {
            if (pointerLayer.wheelMode === "console") {
                pointerLayer.consoleMiddleCanceled(mouseX, mouseY)
            } else if (pointerLayer.scrollingStarted) {
                autoScroll.end()
            }
        }
    }

    AutoScrollController {
        id: autoScroll
        objectName: "galleryMouseAutoScrollController"
        layout: pointerLayer.layout
        pointerSource: middleButtonArea
        horizontal: pointerLayer.presentationMode === "columns"
        scrollExtent: pointerLayer.autoScrollExtent
    }

    PinchArea {
        objectName: "galleryPinchArea"
        x: pointerLayer.layout.x
        y: pointerLayer.layout.y
        width: pointerLayer.layout.width
        height: pointerLayer.layout.height
        enabled: pointerLayer.densityAdjustmentEnabled

        onPinchStarted: pinch => {
            pointerLayer.pinchStarted()
            pinch.accepted = true
        }
        onPinchUpdated: pinch => {
            pointerLayer.pinchUpdated(pinch.scale)
            pinch.accepted = true
        }
        onPinchFinished: pinch => {
            pointerLayer.pinchFinished()
            pinch.accepted = true
        }
    }
}
