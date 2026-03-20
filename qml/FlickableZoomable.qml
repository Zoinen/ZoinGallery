import QtQuick

import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: flickableArea

    property alias image: viewerImage
    property size originalSize

    property bool zoomFitView: true

    property real zoomScale: 1.5
    readonly property real minZoomScale: 0.005
    readonly property real maxZoomScale: 128
    property int animationDuration

    property real imagePressedX: 0
    property real imagePressedY: 0

    property bool forceShowScrollBars: false
    property bool dragZoomActive: viewerMouse.pressed || viewportAnimation.running
    property bool scrollBarsVisible: (hbar.hovered || hbar.pressed || vbar.hovered || vbar.pressed || dragZoomActive || frameAnimation.running || forceShowScrollBars) && !zoomFitView

    property bool fitToHeight: originalSize.width / originalSize.height <= flickableArea.width / flickableArea.height
    property real scrollBarsRightMargin: 0

    signal clicked

    onScrollBarsVisibleChanged: {
        if (scrollBarsVisible) {
            delayedScrollbarHiding.stop()
            scrollBarHidingAnimation.stop()
            hbar.opacity = 1
            vbar.opacity = 1
        }
        else {
            delayedScrollbarHiding.start()
        }
    }

    Timer {
        id: delayedScrollbarHiding

        interval: 500

        onTriggered: scrollBarHidingAnimation.start()
    }

    NumberAnimation {
        id: scrollBarHidingAnimation
        targets: [hbar, vbar]
        property: "opacity"
        to: 0
        duration: 500
        easing.type: Easing.OutSine
    }


    property point lastPos
    property real lastTime: 0
    property var velocityHistoryX: [] // Array to hold the history of x velocities
    property var velocityHistoryY: [] // Array to hold the history of y velocities
    property int historySize: 5 // Size of the history buffer

    property real infiniteMoveSpeed: 1.4
    property real infiniteZoomSpeed: 2.5

    onWidthChanged: if (zoomFitView) { zoomToFit(true) }
    onHeightChanged: if (zoomFitView) { zoomToFit(true) }

    FrameAnimation {
        id: frameAnimation

        property real x: 0
        property real y: 0
        property real scale: 0

        onTriggered: {
            let sourceZoom = zoomScale
            let targetZoom = Math.max(minZoomScale, Math.min(maxZoomScale, zoomScale * (1 + scale * frameTime * infiniteZoomSpeed)))
            let targetX = flickableArea.width / 2 - ((flickableArea.width / 2 - viewerImage.x) / sourceZoom) * targetZoom +
                          x * infiniteMoveSpeed * frameTime * 1000
            let targetY = flickableArea.height / 2 - ((flickableArea.height / 2 - viewerImage.y) / sourceZoom) * targetZoom +
                          y * infiniteMoveSpeed * frameTime * 1000

            zoomScale = targetZoom
            zoomAnimation.to = zoomScale

            targetX = fitViewerImageInViewportBoundsX(targetX)
            targetY = fitViewerImageInViewportBoundsY(targetY)

            imagePressedX = imagePressedX / zoomScale * sourceZoom + (viewerImage.x - targetX) / zoomScale
            imagePressedY = imagePressedY / zoomScale * sourceZoom + (viewerImage.y - targetY) / zoomScale

            viewerImage.x = targetX
            viewerImage.y = targetY

            // console.log(frameTime)
        }
    }

    function startZoomScrollingAnimation(x, y, scale) {
        zoomFitView = false
        viewportAnimation.stop()

        frameAnimation.x = x
        frameAnimation.y = y
        frameAnimation.scale = scale
        frameAnimation.running = x || y || scale
    }

    function zoomTo100(keepMousePosition) {
        zoomAnimation.to = 1
        let targetX
        let targetY
        if (keepMousePosition) {
            targetX = viewerMouse.mouseX - (imagePressedX) * 1
            targetY = viewerMouse.mouseY - (imagePressedY) * 1
        }
        else {
            if (!viewportAnimation.running) {
                imagePressedX = (flickableArea.width / 2 - viewerImage.x) / zoomScale
                imagePressedY = (flickableArea.height / 2 - viewerImage.y) / zoomScale
            }
            targetX = flickableArea.width / 2 - (imagePressedX) * 1
            targetY = flickableArea.height / 2 - (imagePressedY) * 1
        }

        xAnimation.to = fitViewerImageInViewportBoundsX(targetX, zoomScale)
        yAnimation.to = fitViewerImageInViewportBoundsY(targetY, zoomScale)

        xAnimation.duration = animationDuration
        yAnimation.duration = animationDuration
        zoomAnimation.duration = animationDuration

        viewportAnimation.easing = Easing.InOutQuad
        viewportAnimation.restart()

        zoomFitView = false
    }

    function zoomToFit(skipAnimation) {
        // Show scrollbars
        delayedScrollbarHiding.stop()
        scrollBarHidingAnimation.stop()
        hbar.opacity = 1
        vbar.opacity = 1

        zoomFitView = true

        let targetWidth = fitToHeight ? flickableArea.height * (originalSize.width / originalSize.height) :
                                                    flickableArea.width
        let targetHeight = fitToHeight ? flickableArea.height :
                                                     flickableArea.width / (originalSize.width / originalSize.height)

        if (skipAnimation) {
            xAnimation.duration = 0
            yAnimation.duration = 0
            zoomAnimation.duration = 0
        }
        else {
            xAnimation.duration = animationDuration
            yAnimation.duration = animationDuration
            zoomAnimation.duration = animationDuration
        }

        zoomAnimation.to = fitToHeight ? flickableArea.height / originalSize.height : flickableArea.width / originalSize.width
        xAnimation.to = flickableArea.width / 2 - targetWidth / 2
        yAnimation.to = flickableArea.height / 2 - targetHeight / 2

        viewportAnimation.easing = Easing.InOutQuad
        viewportAnimation.restart()

        if (skipAnimation) {
            xAnimation.duration = animationDuration
            yAnimation.duration = animationDuration
            zoomAnimation.duration = animationDuration
        }
    }

    function toggleZoomToFit(keepMousePosition) {
        if (!zoomFitView) {
            zoomToFit()
        }
        else {
            zoomTo100(keepMousePosition)
        }
    }

    function fitViewerImageInViewportBounds() {
        viewerImage.x = fitViewerImageInViewportBoundsX(viewerImage.x)
        viewerImage.y = fitViewerImageInViewportBoundsY(viewerImage.y)
    }

    function fitViewerImageInViewportBoundsX(targetX, targetZoomScale = 1) {
        targetX = Math.min(0, Math.max(targetX, flickableArea.width - viewerImage.width / targetZoomScale))
        if (viewerImage.width / targetZoomScale < flickableArea.width) {
            targetX = flickableArea.width / 2 - viewerImage.width / targetZoomScale / 2
        }
        return targetX
    }

    function fitViewerImageInViewportBoundsY(targetY, targetZoomScale = 1) {
        targetY = Math.min(0, Math.max(targetY, flickableArea.height - viewerImage.height / targetZoomScale))
        if (viewerImage.height / targetZoomScale < flickableArea.height) {
            targetY = flickableArea.height / 2 - viewerImage.height / targetZoomScale / 2
        }
        return targetY
    }

    function onControlReleased() {
        xAnimation.to = fitViewerImageInViewportBoundsX(viewerImage.x)
        yAnimation.to = fitViewerImageInViewportBoundsY(viewerImage.y)
        zoomAnimation.to = flickableArea.zoomScale

        xAnimation.duration = 500
        yAnimation.duration = 500
        zoomAnimation.duration = 0

        viewportAnimation.easing = Easing.OutCirc
        viewportAnimation.restart();
    }

    Connections {
        target: fileListModel
        function onViewerReset() {
            console.log("VIEWER RESET-----------------------")
            image.fromIndex = -1
            image.fromLevel = -1
            viewerImage2.fromIndex = -1
        }
    }

    function setImage(imageIdUrl, originalSize, fromIndex, level) {
        console.log("SET IMAGE |", imageIdUrl, "|", originalSize, fromIndex, level)
        delayedIdSetter.stop()
        if (level === 0 && fromIndex !== image.fromIndex) {
            image.source = imageIdUrl
            image.fromIndex = fromIndex
            image.fromLevel = level
            if (viewerImage2.fromIndex !== fromIndex) {
                viewerImage2.source = ""
                viewerImageCrop.source = ""
                viewerImage2.fromIndex = -1
            }
        }
        else if (level === 1 && (fromIndex !== image.fromIndex || image.fromLevel === 0)) {
            image.source = imageIdUrl
            image.fromIndex = fromIndex
            image.fromLevel = level
            if (viewerImage2.fromIndex !== fromIndex) {
                viewerImage2.source = ""
                viewerImageCrop.source = ""
                viewerImage2.fromIndex = -1
            }
        }
        else if (level === 2 && fromIndex !== viewerImage2.fromIndex) {
            let targetX
            let targetY
            if (viewportAnimation.running) {
                targetX = xAnimation.to
            }
            else {
                targetX = viewerImage.x
            }

            if (viewportAnimation.running) {
                targetY = yAnimation.to
            }
            else {
                targetY = viewerImage.y
            }

            if (flickableArea.width > originalSize.width / dpr * zoomAnimation.to || flickableArea.height > originalSize.height / dpr * zoomAnimation.to) {
                console.log("Not using partial decode", zoomScale, zoomAnimation.running, zoomAnimation.to)
                viewerImage2.source = ""
                viewerImageCrop.source = ""
                viewerImage2.fromIndex = -1
            }
            else {
                console.log("Requesting partial decode")
                viewerImageCrop.targetX = -Math.floor(targetX)
                viewerImageCrop.targetY = -Math.floor(targetY)
                viewerImageCrop.targetWidth = flickableArea.width
                viewerImageCrop.targetHeight = flickableArea.height
                // TODO: Math.round() is not accurate here
                viewerImageCrop.source = imageIdUrl + "/" +
                        Math.round(viewerImageCrop.targetX * dpr) + "," +
                        Math.round(viewerImageCrop.targetY * dpr) + "," +
                        Math.round(flickableArea.width * dpr) + "," +
                        Math.round(flickableArea.height * dpr)
                viewerImage2.fromIndex = fromIndex
            }

            delayedIdSetter.idToSet = imageIdUrl
            delayedIdSetter.restart()
        }
        flickableArea.originalSize = Qt.size(originalSize.width / dpr, originalSize.height / dpr)
    }

    Timer {
        id: delayedIdSetter
        property string idToSet
        interval: animationDuration
        onTriggered: viewerImage2.source = idToSet
    }

    ScrollBar {
        id: vbar
        anchors {
            right: parent.right
            rightMargin: scrollBarsRightMargin
            top: parent.top
            topMargin: titleBar.viewerHeight
            bottom: parent.bottom
        }
        z: 1
        width: 16
        size: flickableArea.height / viewerImage.height
        position: ((-viewerImage.y) / (viewerImage.height - flickableArea.height)) * (1 - size)
        orientation: Qt.Vertical

        active: dragZoomActive
        visible: (!zoomFitView || viewportAnimation.running) && size < 1
        opacity: 0

        onPositionChanged: {
            if (pressed) {
                viewerImage.y = position * (-flickableArea.height + viewerImage.height) / (size - 1)
            }
        }
    }

    ScrollBar {
        id: hbar
        anchors {
            left: parent.left
            right: parent.right
            rightMargin: scrollBarsRightMargin
            bottom: parent.bottom
        }
        z: 1
        height: 16
        size: flickableArea.width / viewerImage.width
        position: ((-viewerImage.x) / (viewerImage.width - flickableArea.width)) * (1 - size)
        orientation: Qt.Horizontal

        active: dragZoomActive
        visible: (!zoomFitView || viewportAnimation.running) && size < 1
        opacity: 0

        onPositionChanged: {
            if (pressed) {
                viewerImage.x = position * (-flickableArea.width + viewerImage.width) / (size - 1)
            }
        }
    }

    ParallelAnimation {
        id: viewportAnimation

        property var easing: Easing.InOutQuad

        NumberAnimation {
            id: zoomAnimation
            target: flickableArea
            property: "zoomScale"
            easing.type: viewportAnimation.easing
        }

        NumberAnimation {
            id: xAnimation
            target: viewerImage
            property: "x"
            easing.type: viewportAnimation.easing
        }

        NumberAnimation {
            id: yAnimation
            target: viewerImage
            property: "y"
            easing.type: viewportAnimation.easing
        }
    }

    Image {
        id: viewerImage
        cache: false

        width: originalSize.width * zoomScale
        height: originalSize.height * zoomScale
        mipmap: false
        asynchronous: false

        property int fromIndex: -1
        property int fromLevel: -1

        visible: false


        Image {
            id: viewerImage2
            cache: false

            width: parent.width
            height: parent.height
            mipmap: true
            asynchronous: true

            property int fromIndex: -1

            // visible: false
        }
    }

    ShaderEffect {
        id: viewerImageShader
        anchors.fill: viewerImage
        // visible: !viewerMouse.pressed

        property var source: viewerImage2.status === Image.Ready ? viewerImage2 : viewerImage
        property var viewportSize: Qt.size(viewerImageShader.width * dpr, viewerImageShader.height * dpr)
        property real sharpenAmount: zoomScale < 1 ? 1.5 : 0
        property bool showCheckerboard: masonryLayout.view.showTransparentGrid
        property int checkerboardSize: 4 * dpr
        property int borderRadius: 0

        fragmentShader: "qrc:/resources/shader.frag.qsb"

        Image {
            id: viewerImageCrop
            cache: false

            property real targetX
            property real targetY
            property real targetWidth
            property real targetHeight

            x: targetX * zoomScale
            y: targetY * zoomScale
            width: targetWidth * zoomScale
            height: targetHeight * zoomScale

            // mipmap: false
            // asynchronous: false

            property int fromIndex: -1

            visible: viewerImage2.status !== Image.Ready
        }
    }

    MouseArea {
        id: viewerMouse
        anchors.fill: parent
        enabled: root.state === "viewer" // && zoomFitView

        acceptedButtons: Qt.AllButtons

        property real mousePressedX: 0
        property real mousePressedY: 0

        function updateVelocityHistory(history, velocity, size) {
            history.push(velocity);
            if (history.length > size) {
                history.shift(); // Remove the oldest element to maintain the size
            }
        }

        function averageVelocity(history) {
            if (!history.length) {
                return 0
            }

            var sum = history.reduce(function(a, b) {
                return a + b;
            }, 0);
            return sum / history.length;
        }

        onPressed:
            (mouse) => {
                mousePressedX = mouse.x
                mousePressedY = mouse.y

                imagePressedX = (mouse.x - viewerImage.x) / zoomScale
                imagePressedY = (mouse.y - viewerImage.y) / zoomScale

                if (mouse.button === Qt.LeftButton) {
                    lastPos = Qt.point(mouse.x, mouse.y);
                    lastTime = Date.now();
                    velocityHistoryX = [];
                    velocityHistoryY = [];
                    viewportAnimation.stop()
                }
                else if (mouse.button === Qt.RightButton) {
                    flickableArea.toggleZoomToFit(true)
                }
            }

        onPositionChanged:
            (mouse) => {
                if (!(mouse.buttons & Qt.LeftButton)) {
                    return
                }
                viewerImage.x = viewerMouse.mouseX - (imagePressedX) * zoomScale
                viewerImage.y = viewerMouse.mouseY - (imagePressedY) * zoomScale

                xAnimation.to = viewerMouse.mouseX - (imagePressedX) * zoomAnimation.to
                yAnimation.to = viewerMouse.mouseY - (imagePressedY) * zoomAnimation.to
                viewportAnimation.restart()



                var currentTime = Date.now();
                var dt = (currentTime - lastTime) || 1; // Avoid division by zero
                var currentVelocityX = (mouseX - lastPos.x) / dt * 1000;
                var currentVelocityY = (mouseY - lastPos.y) / dt * 1000;

                // Update the velocity history buffers
                updateVelocityHistory(velocityHistoryX, currentVelocityX, historySize);
                updateVelocityHistory(velocityHistoryY, currentVelocityY, historySize);

                // viewerImage.x += mouseX - lastPos.x; // Update position directly
                // viewerImage.y += mouseY - lastPos.y;
                lastPos = Qt.point(mouseX, mouseY);
                lastTime = currentTime;
            }

        onReleased:
            (mouse) => {
                if (mouse.button !== Qt.LeftButton) {
                    return
                }

                // Time threshold to consider a pause (in milliseconds)
                var timeThreshold = 100; // Example threshold, adjust as needed
                // Velocity threshold to consider slow dragging (pixels per second)
                var velocityThreshold = 50; // Example threshold, adjust as needed

                var currentTime = Date.now();
                var timeSinceLastMove = currentTime - lastTime;

                // Calculate the average velocity
                var avgVelocityX = averageVelocity(velocityHistoryX);
                var avgVelocityY = averageVelocity(velocityHistoryY);

                // Determine if movement was slow or paused before release
                var slowOrPaused = /*Math.abs(avgVelocityX) < velocityThreshold || Math.abs(avgVelocityY) < velocityThreshold ||*/ timeSinceLastMove > timeThreshold;

                // Check if the content is outside the flickableArea boundaries
                var outsideBounds = viewerImage.x > 0 || viewerImage.y > 0 || viewerImage.x + viewerImage.width < flickableArea.width || viewerImage.y + viewerImage.height < flickableArea.height;

                // Calculate target positions for inertia using the average velocity
                var decelerationFactor = 0.1; // Control the deceleration
                var targetX = viewerImage.x + (slowOrPaused && !outsideBounds ? 0 : avgVelocityX * decelerationFactor);
                var targetY = viewerImage.y + (slowOrPaused && !outsideBounds ? 0 : avgVelocityY * decelerationFactor);

                xAnimation.to = fitViewerImageInViewportBoundsX(targetX)
                yAnimation.to = fitViewerImageInViewportBoundsY(targetY)
                zoomAnimation.to = flickableArea.zoomScale

                // xAnimation.easing.type = slowOrPaused ? Easing.InOutCirc : Easing.OutCirc
                // yAnimation.easing.type = slowOrPaused ? Easing.InOutCirc : Easing.OutCirc
                xAnimation.duration = 500
                yAnimation.duration = 500
                zoomAnimation.duration = 0

                viewportAnimation.easing = Easing.OutCirc
                viewportAnimation.restart();
            }

        onWheel:
            (wheel) => {
                if (wheel.modifiers === Qt.ControlModifier || (wheel.buttons & Qt.LeftButton)) {
                    let angleDelta = wheel.angleDelta.y * 1.1
                    let delta = Math.abs(angleDelta / 100)
                    if (angleDelta < 0) {
                        delta = 1 / delta
                    }

                    zoomFitView = false

                    zoomAnimation.to *= delta
                    if (wheel.modifiers === Qt.ControlModifier && !(wheel.buttons & Qt.LeftButton)) {
                        // console.log("ZZ CENTER") //(mouse.x - viewerImage.x) / zoomScale
                        xAnimation.to = flickableArea.width / 2 - ((flickableArea.width / 2 - viewerImage.x) / zoomScale) * zoomAnimation.to
                        yAnimation.to = flickableArea.height / 2 - ((flickableArea.height / 2 - viewerImage.y) / zoomScale) * zoomAnimation.to

                        // viewerImage.x = flickableArea.width / 2 - (imagePressedX) * zoomAnimation.to
                        // viewerImage.y = flickableArea.height / 2 - (imagePressedY) * zoomAnimation.to
                    }
                    else {
                        xAnimation.to = viewerMouse.mouseX - (imagePressedX) * zoomAnimation.to
                        yAnimation.to = viewerMouse.mouseY - (imagePressedY) * zoomAnimation.to
                    }


                    // xAnimation.easing.type = slowOrPaused ? Easing.InOutCirc : Easing.OutCirc
                    // yAnimation.easing.type = slowOrPaused ? Easing.InOutCirc : Easing.OutCirc
                    xAnimation.duration = 100
                    yAnimation.duration = 100
                    zoomAnimation.duration = 100

                    viewportAnimation.easing = Easing.OutSine
                    viewportAnimation.restart()
                }
                else {
                    // wheel.accepted = false
                }
            }

        onDoubleClicked:
            (mouse) => {
                // console.log("ZZ DBL")
                if (mouse.button === Qt.LeftButton) {
                    root.toggleViewer()
                }
                if (mouse.button === Qt.LeftButton && mousePressedX === mouse.x && mousePressedY === mouse.y) {
                    flickableArea.clicked()
                }
            }

        onClicked:
            (mouse) => {
                if (mouse.button === Qt.LeftButton && mousePressedX === mouse.x && mousePressedY === mouse.y) {
                    flickableArea.clicked()
                }
            }
    }

    // Rectangle {
    //     anchors.horizontalCenter: parent.horizontalCenter
    //     width: childrenRect.width
    //     height: childrenRect.height

    //     color: Qt.rgba(0, 0, 0, 0.7)

    //     Column {
    //         Text {
    //             text: "x: " + xAnimation.from.toFixed(2) + " -> " + xAnimation.to.toFixed(2) + " / " + xAnimation.duration.toFixed(2)
    //             color: Style.text
    //         }

    //         Text {
    //             text: "y: " + yAnimation.from.toFixed(2) + " -> " + yAnimation.to.toFixed(2) + " / " + yAnimation.duration.toFixed(2)
    //             color: Style.text
    //         }

    //         Text {
    //             text: "zoom: " + zoomAnimation.from.toFixed(2) + " -> " + zoomAnimation.to.toFixed(2) + " / " + zoomAnimation.duration.toFixed(2)
    //             color: Style.text
    //         }

    //         Text {
    //             text: "zoom: " + (zoomScale * 100).toFixed(2)
    //             color: Style.text
    //         }

    //         Text {
    //             text: "press: " + imagePressedX.toFixed(2) + ", " + imagePressedY.toFixed(2)
    //             color: Style.text
    //         }
    //     }
    // }
}
