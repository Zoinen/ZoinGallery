pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: flickableArea

    // Host policy is explicit so the reusable viewport does not depend on
    // dynamically scoped shell objects.
    property int animationDuration: 300
    property int easingType: Easing.OutSine

    property alias source: viewerImageShader.source
    property size originalSize
    property alias fovVisual: viewerImageShader.fov

    property real fov: 90
    property alias tilt: viewerImageShader.tilt
    property alias pan: viewerImageShader.pan
    readonly property alias inertiaRunning: scrollAnimation.running

    signal closeRequested
    signal sphereScrollingMouseCursorRequested(bool set, bool idle,
                                               real rotation)

    // ViewerWheelArea sits above the reusable viewer so it can preserve the
    // original horizontal image-navigation gestures.  Keep the panorama's
    // legacy FOV calculation callable from that input layer as well as from
    // the standalone MouseArea; both paths therefore operate on the exact
    // same state and never synthesize a second wheel event.
    function handleZoomWheel(angleDeltaY, modifiers, buttons) {
        if (modifiers !== Qt.ControlModifier
                && !(buttons & Qt.LeftButton))
            return false

        const delta = -angleDeltaY / 100
        const minFOV = 1
        const maxFOV = 180
        const normalizedFOV = (fov - minFOV) / (maxFOV - minFOV)
        const fovChange = Math.exp(
                    delta * panoramaMouseArea.zoomSensitivity
                    * (1 - normalizedFOV))
        fov = Math.max(minFOV, Math.min(170, fov * fovChange))
        return true
    }

    ShaderEffect {
        id: viewerImageShader
        anchors.fill: parent
        // visible: !viewerMouse.pressed

        property var source
        // property var viewportSize: Qt.size(viewerImageShader.width * dpr, viewerImageShader.height * dpr)
        // property real sharpenAmount: zoomScale < 1 ? 1.5 : 0

        property real fov: flickableArea.fov // Field of view in degrees
        Behavior on fov {
            NumberAnimation {
                duration: flickableArea.animationDuration
                easing.type: flickableArea.easingType
            }
        }
        property real tilt: 0 // Tilt angle in degrees
        property real pan: 0 // Pan angle in degrees
        property real aspect: originalSize.height > 0 ? originalSize.width / originalSize.height : 1.0

        fragmentShader: "qrc:/ZoinGallery/resources/sphere.frag.qsb"
    }

    MouseArea {
        id: panoramaMouseArea
        objectName: "sphericViewerPointerArea"
        anchors.fill: parent
        enabled: true
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton

        property real sensitivity: 0.0025
        property real zoomSensitivity: 0.1
        property real inertiaFactor: 0.95
        property real baseX: 0
        property real baseY: 0
        property real scrollSpeedX: 0
        property real scrollSpeedY: 0

        onWheel:
            (wheel) => {
                flickableArea.handleZoomWheel(
                            wheel.angleDelta.y,
                            wheel.modifiers, wheel.buttons)
            }

        onPressed: {
            scrollAnimation.inertiaMode = false
            scrollAnimation.stop()

            baseX = mouseX;
            baseY = mouseY;
            scrollSpeedX = 0;
            scrollSpeedY = 0;
            scrollAnimation.start();

            flickableArea.sphereScrollingMouseCursorRequested(true, true, 0)
        }

        function calculateAngle(x1, y1, x2, y2) {
            var deltaX = x2 - x1;
            var deltaY = y2 - y1;
            var radians = Math.atan2(deltaY, deltaX);
            var angle = radians * 180 / Math.PI;

            // Convert negative angles to positive
            if (angle < 0) {
                angle += 360;
            }

            return angle;
        }

        onPositionChanged: {
            if (pressed) {
                var deltaX = mouseX - baseX;
                var deltaY = mouseY - baseY;

                var distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);

                var speed = distance * sensitivity
                if (!distance) {
                    scrollSpeedX = 0
                    scrollSpeedY = 0
                }
                else {
                    let decel = Math.pow(fov / 90.0, 0.5)
                    scrollSpeedX = deltaX / distance * speed * decel;
                    scrollSpeedY = deltaY / distance * speed * decel;
                }
                flickableArea.sphereScrollingMouseCursorRequested(
                            true, false,
                            calculateAngle(baseX, baseY, mouseX, mouseY) - 90)
            }
        }

        onReleased: {
            // Smoothly transition to inertia using the current scroll speed
            scrollAnimation.inertiaMode = true;
            flickableArea.sphereScrollingMouseCursorRequested(
                        false, false, 0)
        }

        onEntered: {
            // Hide the cursor when the mouse enters the area
            cursorShape = Qt.BlankCursor;
        }

        onExited: {
            // Restore the cursor when the mouse leaves the area
            cursorShape = Qt.ArrowCursor;
        }

        onDoubleClicked:
            (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    flickableArea.closeRequested()
                }
            }


        FrameAnimation {
            id: scrollAnimation
            property bool inertiaMode: false

            onTriggered: {
                pan += panoramaMouseArea.scrollSpeedX;
                tilt += panoramaMouseArea.scrollSpeedY;

                tilt = Math.max(-90.0, Math.min(90.0, tilt));

                if (inertiaMode) {
                    // Gradually decrease the scroll speed for inertia effect
                    panoramaMouseArea.scrollSpeedX *= panoramaMouseArea.inertiaFactor;
                    panoramaMouseArea.scrollSpeedY *= panoramaMouseArea.inertiaFactor;

                    // Stop the animation when the scroll speed is close to zero
                    if (Math.abs(panoramaMouseArea.scrollSpeedX) < 0.01 && Math.abs(panoramaMouseArea.scrollSpeedY) < 0.01) {
                        inertiaMode = false;
                        stop()
                    }
                }
            }
        }
    }
}
