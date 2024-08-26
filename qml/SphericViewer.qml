import QtQuick

import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: flickableArea

    property alias source: viewerImageShader.source
    property size originalSize
    property alias fovVisual: viewerImageShader.fov

    property real fov: 90
    property alias tilt: viewerImageShader.tilt
    property alias pan: viewerImageShader.pan

    ShaderEffect {
        id: viewerImageShader
        anchors.fill: parent
        // visible: !viewerMouse.pressed

        property var source //: viewerImage
        // property var viewportSize: Qt.size(viewerImageShader.width * dpr, viewerImageShader.height * dpr)
        // property real sharpenAmount: zoomScale < 1 ? 1.5 : 0

        property real fov: flickableArea.fov // Field of view in degrees
        Behavior on fov {
            NumberAnimation { duration: 300; easing.type: viewerMode.easingType }
        }
        property real tilt: 0 // Tilt angle in degrees
        property real pan: 0 // Pan angle in degrees
        property real aspect: originalSize.width / originalSize.height

        fragmentShader: "qrc:/resources/sphere.frag.qsb"
    }

    /*Column {
        y: 300
        z: 1000
        spacing: 10
        Row {
            Text {
                color: Style.text
                text: "FOV"
            }

            Slider {
                from: 1
                to: 170
                value: 90
                onValueChanged: viewerImageShader.fov = value
            }
        }
        Row {
            Text {
                color: Style.text
                text: "Tilt"
            }

            Slider {
                from: -90
                to: 90
                onValueChanged: viewerImageShader.tilt = value
            }
        }
        Row {
            Text {
                color: Style.text
                text: "Pan"
            }

            Slider {
                from: 0
                to: 360
                onValueChanged: viewerImageShader.pan = value
            }
        }
    }*/


    /*MouseArea {
        anchors.fill: parent
        enabled: true
        hoverEnabled: true

        property real sensitivity: 0.1
        property real lastX: 0
        property real lastY: 0

        onWheel: {
            if (wheel.angleDelta.y > 0) {
                fov = Math.max(1.0, fov - 5.0);
            } else if (wheel.angleDelta.y < 0) {
                fov = Math.min(180.0, fov + 5.0);
            }
        }

        onPositionChanged: {
            var deltaX = mouseX - lastX;
            var deltaY = mouseY - lastY;

            pan += deltaX * sensitivity;
            tilt += deltaY * sensitivity;

            tilt = Math.max(-90.0, Math.min(90.0, tilt));

            // Reset the mouse position to the center of the screen
            lastX = parent.width / 2;
            lastY = parent.height / 2;
            topLevelWindow.setMousePos(parent.mapToGlobal(Qt.point(lastX, lastY)));
        }

        onEntered: {
            // Hide the cursor when the mouse enters the area
            cursorShape = Qt.BlankCursor;
        }

        onExited: {
            // Restore the cursor when the mouse leaves the area
            cursorShape = Qt.ArrowCursor;
        }
    }*/

    MouseArea {
        id: panoramaMouseArea
        anchors.fill: parent
        enabled: true
        hoverEnabled: true

        property real sensitivity: 0.0025
        property real zoomSensitivity: 0.1
        property real inertiaFactor: 0.95
        property real baseX: 0
        property real baseY: 0
        property real scrollSpeedX: 0
        property real scrollSpeedY: 0

        onWheel:
            (wheel) => {
                if (wheel.modifiers === Qt.ControlModifier || (wheel.buttons & Qt.LeftButton)) {
                    let delta = -wheel.angleDelta.y / 100

                    let minFOV = 1
                    let maxFOV = 180

                    /*                const fovChange = Math.exp(-delta * 0.1);

                // Update the current FOV by multiplying with the FOV change
                fov *= fovChange;

                // Clamp the FOV within the desired range
                fov = Math.max(1, Math.min(170, fov));*/


                    const normalizedFOV = (fov - minFOV) / (maxFOV - minFOV);

                    // Calculate the FOV change based on the wheel delta and current FOV
                    const fovChange = Math.exp(delta * zoomSensitivity * (1 - normalizedFOV));

                    // Update the current FOV by multiplying with the FOV change
                    fov *= fovChange;

                    // Clamp the FOV within the desired range
                    fov = Math.max(minFOV, Math.min(170, fov));
                }
            }

        onPressed: {
            scrollAnimation.inertiaMode = false
            scrollAnimation.stop()

            baseX = mouseX;
            baseY = mouseY;
            scrollSpeedX = 0;
            scrollSpeedY = 0;
            scrollAnimation.start();

            topLevelWindow.setSphereScrollingMouseCursor(true, true)
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
                topLevelWindow.setSphereScrollingMouseCursor(true, false, calculateAngle(baseX, baseY, mouseX, mouseY) - 90)
            }
        }

        onReleased: {
            // Smoothly transition to inertia using the current scroll speed
            scrollAnimation.inertiaMode = true;
            topLevelWindow.setSphereScrollingMouseCursor(false)
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
                console.log("ZZ2 DBL")
                if (mouse.button === Qt.LeftButton) {
                    root.toggleViewer()
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
