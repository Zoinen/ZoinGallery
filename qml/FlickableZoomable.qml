import QtQuick

import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: flickableArea

    property var viewerModel: null
    property var sourceMasonry: null

    property alias image: viewerImage
    property var textureSource: viewerImage2.status === Image.Ready ? viewerImage2 : viewerImageBase
    property size originalSize
    property int rotationMode: 0
    property bool animateRotation: false
    property size effectiveOriginalSize: (rotationMode % 2 === 1) ? Qt.size(originalSize.height, originalSize.width) : originalSize

    property real animatedEffectiveWidth: effectiveOriginalSize.width
    onAnimatedEffectiveWidthChanged: {
        if (isRotating) updatePinnedPosition()
    }
    Behavior on animatedEffectiveWidth {
        enabled: animateRotation && zoomFitView
        NumberAnimation { duration: animationDuration; easing.type: Easing.InOutQuad }
    }

    property real animatedEffectiveHeight: effectiveOriginalSize.height
    onAnimatedEffectiveHeightChanged: {
        if (isRotating) updatePinnedPosition()
    }
    Behavior on animatedEffectiveHeight {
        enabled: animateRotation && zoomFitView
        NumberAnimation { duration: animationDuration; easing.type: Easing.InOutQuad }
    }

    onZoomScaleChanged: {
        if (isRotating) updatePinnedPosition()
    }

    NumberAnimation {
        id: rotationZoomAnimation
        target: flickableArea
        property: "zoomScale"
        easing.type: Easing.InOutQuad
    }

    property bool zoomFitView: true
    readonly property bool viewportAnimationRunning: viewportAnimation.running
    property bool pinchZoomEnabled: true
    property real pinchZoomOutToThumbnailsStartScaleDistanceRatio: 0.5
    property real pinchZoomOutToThumbnailsScaleDistanceRatio: 0.45
    property real pinchZoomOutToThumbnailsCommitProgress: 0.35

    property bool imageTextureReady: (viewerImage2.status === Image.Ready && viewerImage2.implicitWidth > 1 && viewerImage2.implicitHeight > 1)
        || (viewerImage2.status !== Image.Ready && viewerImageBase.status === Image.Ready
            && viewerImageBase.implicitWidth > 1 && viewerImageBase.implicitHeight > 1)

    property real zoomScale: 1.5
    readonly property real minZoomScale: 0.005
    readonly property real maxZoomScale: 128
    property int animationDuration

    property real imagePressedX: 0
    property real imagePressedY: 0

    property bool forceShowScrollBars: false
    property bool hideVerticalScrollBar: false
    property bool dragZoomActive: viewerMouse.pressed || viewportAnimation.running
    property bool scrollBarsVisible: (hbar.hovered || hbar.pressed ||
            (!hideVerticalScrollBar && (vbar.hovered || vbar.pressed)) ||
            dragZoomActive || frameAnimation.running || forceShowScrollBars) && !zoomFitView

    property bool fitToHeight: effectiveOriginalSize.width / effectiveOriginalSize.height <= flickableArea.width / flickableArea.height
    property real scrollBarsRightMargin: 0

    signal clicked

    onScrollBarsVisibleChanged: {
        if (scrollBarsVisible) {
            delayedScrollbarHiding.stop()
            scrollBarHidingAnimation.stop()
            hbar.opacity = 1
            if (!hideVerticalScrollBar) {
                vbar.opacity = 1
            }
        }
        else {
            delayedScrollbarHiding.start()
        }
    }

    onHideVerticalScrollBarChanged: {
        if (hideVerticalScrollBar) {
            vbar.opacity = 0
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
    property var wheelPanVelocityHistoryX: []
    property var wheelPanVelocityHistoryY: []
    property int wheelPanHistorySize: 5
    property real wheelPanLastTime: 0
    property bool wheelPanActive: false

    property real infiniteMoveSpeed: 1.4
    property real infiniteZoomSpeed: 2.5
    property real pinchStartZoomScale: 1
    property real pinchStartImagePointX: 0
    property real pinchStartImagePointY: 0
    property real pinchStartCenterX: 0
    property real pinchStartCenterY: 0
    property bool pinchZoomOutToThumbnailsActive: false
    property real pinchZoomOutToThumbnailsProgress: 0

    signal pinchZoomOutToThumbnailsProgressed(real progress)
    signal pinchZoomOutToThumbnailsFinished(bool commit)

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

    function fitZoomScale() {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return 1
        }

        return fitToHeight ? flickableArea.height / effectiveOriginalSize.height :
                             flickableArea.width / effectiveOriginalSize.width
    }

    function clampZoomScale(targetScale) {
        return Math.max(minZoomScale, Math.min(maxZoomScale, targetScale))
    }

    function imageRectFittedInRect(targetRect) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1 ||
                targetRect === undefined || targetRect.width <= 1 || targetRect.height <= 1) {
            return Qt.rect(0, 0, 0, 0)
        }

        let targetScale = effectiveOriginalSize.width / effectiveOriginalSize.height <= targetRect.width / targetRect.height ?
                targetRect.height / effectiveOriginalSize.height :
                targetRect.width / effectiveOriginalSize.width
        let targetWidth = effectiveOriginalSize.width * targetScale
        let targetHeight = effectiveOriginalSize.height * targetScale
        return Qt.rect(targetRect.x + (targetRect.width - targetWidth) / 2,
                       targetRect.y + (targetRect.height - targetHeight) / 2,
                       targetWidth,
                       targetHeight)
    }

    function setImageRect(targetRect) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1 ||
                targetRect === undefined || targetRect.width <= 1 || targetRect.height <= 1) {
            return
        }

        viewportAnimation.stop()
        frameAnimation.running = false

        let targetScale = clampZoomScale(targetRect.width / effectiveOriginalSize.width)
        zoomFitView = false
        zoomScale = targetScale
        viewerImage.x = targetRect.x
        viewerImage.y = targetRect.y
        zoomAnimation.to = targetScale
        xAnimation.to = targetRect.x
        yAnimation.to = targetRect.y
    }

    function setViewport(targetScale, targetX, targetY) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        viewportAnimation.stop()
        frameAnimation.running = false

        targetScale = clampZoomScale(targetScale)
        targetX = fitViewerImageInViewportBoundsX(targetX, targetScale)
        targetY = fitViewerImageInViewportBoundsY(targetY, targetScale)

        zoomFitView = false
        zoomScale = targetScale
        viewerImage.x = targetX
        viewerImage.y = targetY
        zoomAnimation.to = targetScale
        xAnimation.to = targetX
        yAnimation.to = targetY

        forceShowScrollBars = true
        forceShowScrollBars = false
    }

    function setZoomScaleAt(targetScale, centerX, centerY) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        viewportAnimation.stop()
        frameAnimation.running = false

        targetScale = clampZoomScale(targetScale)
        let imagePointX = (centerX - viewerImage.x) / zoomScale
        let imagePointY = (centerY - viewerImage.y) / zoomScale
        let targetX = fitViewerImageInViewportBoundsX(centerX - imagePointX * targetScale, targetScale)
        let targetY = fitViewerImageInViewportBoundsY(centerY - imagePointY * targetScale, targetScale)

        zoomFitView = false
        zoomScale = targetScale
        viewerImage.x = targetX
        viewerImage.y = targetY
        zoomAnimation.to = targetScale
        xAnimation.to = targetX
        yAnimation.to = targetY

        forceShowScrollBars = true
        forceShowScrollBars = false
    }

    function beginPinchZoom(centerX, centerY) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        viewportAnimation.stop()
        frameAnimation.running = false
        pinchZoomOutToThumbnailsActive = false
        pinchZoomOutToThumbnailsProgress = 0
        pinchStartZoomScale = zoomScale
        pinchStartCenterX = centerX
        pinchStartCenterY = centerY
        pinchStartImagePointX = (centerX - viewerImage.x) / zoomScale
        pinchStartImagePointY = (centerY - viewerImage.y) / zoomScale
    }

    function updatePinchZoom(scale) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        let targetScale = clampZoomScale(pinchStartZoomScale * scale)
        let fittedScale = fitZoomScale()
        let transitionDistance = Math.max(0.001, fittedScale * pinchZoomOutToThumbnailsScaleDistanceRatio)
        let transitionStartDistance = Math.min(transitionDistance * 0.8,
                Math.max(0, fittedScale * pinchZoomOutToThumbnailsStartScaleDistanceRatio))
        let activeTransitionDistance = Math.max(0.001, transitionDistance - transitionStartDistance)
        let transitionProgress = Math.max(0,
                Math.min(1, (fittedScale - transitionStartDistance - targetScale) / activeTransitionDistance))

        if (pinchZoomOutToThumbnailsActive) {
            if (transitionProgress > 0) {
                pinchZoomOutToThumbnailsProgress = transitionProgress
                pinchZoomOutToThumbnailsProgressed(transitionProgress)
                return
            }

            pinchZoomOutToThumbnailsActive = false
            pinchZoomOutToThumbnailsProgress = 0
            pinchZoomOutToThumbnailsProgressed(0)
        }

        let targetX = pinchStartCenterX - pinchStartImagePointX * targetScale
        let targetY = pinchStartCenterY - pinchStartImagePointY * targetScale

        zoomFitView = false
        zoomScale = targetScale
        viewerImage.x = targetX
        viewerImage.y = targetY
        zoomAnimation.to = targetScale
        xAnimation.to = targetX
        yAnimation.to = targetY

        forceShowScrollBars = true
        forceShowScrollBars = false

        if (root.state === "viewer" && transitionProgress > 0) {
            pinchZoomOutToThumbnailsActive = true
            pinchZoomOutToThumbnailsProgress = transitionProgress
            pinchZoomOutToThumbnailsProgressed(transitionProgress)
        }
    }

    function finishPinchZoom() {
        if (pinchZoomOutToThumbnailsActive) {
            let commit = pinchZoomOutToThumbnailsProgress >= pinchZoomOutToThumbnailsCommitProgress
            pinchZoomOutToThumbnailsActive = false
            pinchZoomOutToThumbnailsFinished(commit)
            return
        }

        if (root.state !== "viewer" || effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        let fittedScale = fitZoomScale()
        if (zoomScale <= fittedScale * 1.02) {
            zoomToFit()
        }
        else {
            onControlReleased()
        }
    }

    function zoomToScale(targetScale, keepMousePosition) {
        targetScale = clampZoomScale(targetScale)
        zoomAnimation.to = targetScale
        let targetX
        let targetY
        if (keepMousePosition) {
            targetX = viewerMouse.mouseX - (imagePressedX) * targetScale
            targetY = viewerMouse.mouseY - (imagePressedY) * targetScale
        }
        else {
            if (!viewportAnimation.running) {
                imagePressedX = (flickableArea.width / 2 - viewerImage.x) / zoomScale
                imagePressedY = (flickableArea.height / 2 - viewerImage.y) / zoomScale
            }
            targetX = flickableArea.width / 2 - (imagePressedX) * targetScale
            targetY = flickableArea.height / 2 - (imagePressedY) * targetScale
        }

        xAnimation.to = fitViewerImageInViewportBoundsX(targetX, targetScale)
        yAnimation.to = fitViewerImageInViewportBoundsY(targetY, targetScale)

        xAnimation.duration = animationDuration
        yAnimation.duration = animationDuration
        zoomAnimation.duration = animationDuration

        viewportAnimation.easing = Easing.InOutQuad
        viewportAnimation.restart()

        zoomFitView = false
    }

    function zoomTo100(keepMousePosition) {
        zoomToScale(1, keepMousePosition)
    }

    function zoomToFit(skipAnimation) {
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        // Show scrollbars
        delayedScrollbarHiding.stop()
        scrollBarHidingAnimation.stop()
        hbar.opacity = 1
        vbar.opacity = 1

        zoomFitView = true

        let targetWidth = fitToHeight ? flickableArea.height * (effectiveOriginalSize.width / effectiveOriginalSize.height) :
                                                    flickableArea.width
        let targetHeight = fitToHeight ? flickableArea.height :
                                                     flickableArea.width / (effectiveOriginalSize.width / effectiveOriginalSize.height)

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

        zoomAnimation.to = fitToHeight ? flickableArea.height / effectiveOriginalSize.height : flickableArea.width / effectiveOriginalSize.width
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

    property real pinnedVx: 0
    property real pinnedVy: 0
    property bool isRotating: false

    Timer {
        id: rotationTimer
        interval: animationDuration
        onTriggered: {
            isRotating = false
            onControlReleased()
        }
    }

    function updatePinnedPosition() {
        let rad = unrotatedContent.rotation * Math.PI / 180
        let vpx = pinnedVx * Math.cos(rad) - pinnedVy * Math.sin(rad)
        let vpy = pinnedVx * Math.sin(rad) + pinnedVy * Math.cos(rad)
        
        viewerImage.x = flickableArea.width / 2 - viewerImage.width / 2 - vpx
        viewerImage.y = flickableArea.height / 2 - viewerImage.height / 2 - vpy
    }

    function rotate(direction) {
        animateRotation = true
        viewerImageCrop.source = ""

        if (viewportAnimation.running) {
            viewportAnimation.stop()
        }
        
        let currentVx = (flickableArea.width / 2 - viewerImage.x) - viewerImage.width / 2
        let currentVy = (flickableArea.height / 2 - viewerImage.y) - viewerImage.height / 2
        
        let rad = -unrotatedContent.rotation * Math.PI / 180
        pinnedVx = currentVx * Math.cos(rad) - currentVy * Math.sin(rad)
        pinnedVy = currentVx * Math.sin(rad) + currentVy * Math.cos(rad)
        
        isRotating = true
        rotationTimer.restart()
        
        rotationMode = (rotationMode + direction) % 4
        
        if (zoomFitView) {
            let targetZoom = fitToHeight ? flickableArea.height / effectiveOriginalSize.height : flickableArea.width / effectiveOriginalSize.width
            rotationZoomAnimation.to = targetZoom
            rotationZoomAnimation.duration = animationDuration
            rotationZoomAnimation.restart()
        }
        
        updatePinnedPosition()
    }

    function fitViewerImageInViewportBounds() {
        viewerImage.x = fitViewerImageInViewportBoundsX(viewerImage.x)
        viewerImage.y = fitViewerImageInViewportBoundsY(viewerImage.y)
    }

    function canPanHorizontally() {
        return !zoomFitView && viewerImage.width > flickableArea.width + 0.5
    }

    function canPanVertically() {
        return !zoomFitView && viewerImage.height > flickableArea.height + 0.5
    }

    function updateWheelPanVelocityHistory(history, velocity, size) {
        history.push(velocity)
        if (history.length > size) {
            history.shift()
        }
    }

    function averageWheelPanVelocity(history) {
        if (!history.length) {
            return 0
        }

        let sum = history.reduce(function(a, b) {
            return a + b
        }, 0)
        return sum / history.length
    }

    function beginWheelPan() {
        if (zoomFitView) {
            return
        }

        viewportAnimation.stop()
        wheelPanVelocityHistoryX = []
        wheelPanVelocityHistoryY = []
        wheelPanLastTime = 0
        wheelPanActive = true
    }

    function cancelWheelPan() {
        wheelPanVelocityHistoryX = []
        wheelPanVelocityHistoryY = []
        wheelPanLastTime = 0
        wheelPanActive = false
    }

    function recordWheelPanVelocity(consumedX, consumedY) {
        if (!wheelPanActive) {
            beginWheelPan()
        }

        let now = Date.now()
        let dt = wheelPanLastTime ? Math.max(1, now - wheelPanLastTime) : 16
        updateWheelPanVelocityHistory(wheelPanVelocityHistoryX, consumedX / dt * 1000, wheelPanHistorySize)
        updateWheelPanVelocityHistory(wheelPanVelocityHistoryY, consumedY / dt * 1000, wheelPanHistorySize)
        wheelPanLastTime = now
    }

    function finishWheelPan() {
        if (zoomFitView) {
            cancelWheelPan()
            return
        }

        let avgVelocityX = averageWheelPanVelocity(wheelPanVelocityHistoryX)
        let avgVelocityY = averageWheelPanVelocity(wheelPanVelocityHistoryY)
        let useInertia = wheelPanActive && (Math.abs(avgVelocityX) > 20 || Math.abs(avgVelocityY) > 20)
        let decelerationFactor = 0.1
        let targetX = viewerImage.x + (useInertia ? avgVelocityX * decelerationFactor : 0)
        let targetY = viewerImage.y + (useInertia ? avgVelocityY * decelerationFactor : 0)

        xAnimation.to = fitViewerImageInViewportBoundsX(targetX)
        yAnimation.to = fitViewerImageInViewportBoundsY(targetY)
        zoomAnimation.to = zoomScale
        xAnimation.duration = useInertia ? 500 : animationDuration
        yAnimation.duration = useInertia ? 500 : animationDuration
        zoomAnimation.duration = 0
        viewportAnimation.easing = useInertia ? Easing.OutCirc : Easing.OutSine
        viewportAnimation.restart()

        cancelWheelPan()
    }

    function panBy(deltaX, deltaY, recordVelocity) {
        if (zoomFitView) {
            return Qt.point(deltaX, deltaY)
        }

        viewportAnimation.stop()

        let oldX = viewerImage.x
        let oldY = viewerImage.y
        let targetX = fitViewerImageInViewportBoundsX(viewerImage.x + deltaX)
        let targetY = fitViewerImageInViewportBoundsY(viewerImage.y + deltaY)

        viewerImage.x = targetX
        viewerImage.y = targetY
        xAnimation.to = targetX
        yAnimation.to = targetY
        zoomAnimation.to = zoomScale
        if (recordVelocity) {
            recordWheelPanVelocity(targetX - oldX, targetY - oldY)
        }

        forceShowScrollBars = true
        forceShowScrollBars = false

        return Qt.point(deltaX - (targetX - oldX), deltaY - (targetY - oldY))
    }

    function settlePan() {
        if (zoomFitView) {
            return
        }

        xAnimation.to = fitViewerImageInViewportBoundsX(viewerImage.x)
        yAnimation.to = fitViewerImageInViewportBoundsY(viewerImage.y)
        zoomAnimation.to = zoomScale
        xAnimation.duration = animationDuration
        yAnimation.duration = animationDuration
        zoomAnimation.duration = 0
        viewportAnimation.easing = Easing.OutSine
        viewportAnimation.restart()
    }

    function fitViewerImageInViewportBoundsX(targetX, targetScale) {
        if (targetScale === undefined) {
            targetScale = zoomScale
        }
        let targetWidth = effectiveOriginalSize.width * targetScale
        targetX = Math.min(0, Math.max(targetX, flickableArea.width - targetWidth))
        if (targetWidth < flickableArea.width) {
            targetX = flickableArea.width / 2 - targetWidth / 2
        }
        return targetX
    }

    function fitViewerImageInViewportBoundsY(targetY, targetScale) {
        if (targetScale === undefined) {
            targetScale = zoomScale
        }
        let targetHeight = effectiveOriginalSize.height * targetScale
        targetY = Math.min(0, Math.max(targetY, flickableArea.height - targetHeight))
        if (targetHeight < flickableArea.height) {
            targetY = flickableArea.height / 2 - targetHeight / 2
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

    function resetViewerImages() {
        delayedIdSetter.stop()
        originalSize = Qt.size(0, 0)
        image.source = ""
        image.fromIndex = -1
        image.fromLevel = -1
        viewerImage2.source = ""
        viewerImage2.fromIndex = -1
        viewerImageCrop.source = ""
        viewerImageCrop.fromIndex = -1
    }

    Connections {
        target: flickableArea.viewerModel
        function onViewerReset() {
            console.log("VIEWER RESET-----------------------")
            flickableArea.resetViewerImages()
        }
    }

    function setImage(imageIdUrl, originalSize_, fromIndex, level) {
        animateRotation = false
        console.log("SET IMAGE |", imageIdUrl, "|", originalSize_, fromIndex, level)
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
        else if (level === 1 && (fromIndex !== image.fromIndex || image.fromLevel === 0 || image.source !== imageIdUrl)) {
            image.source = imageIdUrl
            image.fromIndex = fromIndex
            image.fromLevel = level
            if (viewerImage2.fromIndex !== fromIndex) {
                viewerImage2.source = ""
                viewerImageCrop.source = ""
                viewerImage2.fromIndex = -1
            }
        }
        else if (level === 2 && (fromIndex !== viewerImage2.fromIndex || viewerImage2.source !== imageIdUrl)) {
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

            if (flickableArea.width > effectiveOriginalSize.width * zoomAnimation.to || flickableArea.height > effectiveOriginalSize.height * zoomAnimation.to) {
                console.log("Not using partial decode", zoomScale, zoomAnimation.running, zoomAnimation.to)
                viewerImage2.source = ""
                viewerImageCrop.source = ""
                viewerImage2.fromIndex = -1
            }
            else {
                console.log("Requesting partial decode")
                let rx = -targetX / zoomAnimation.to
                let ry = -targetY / zoomAnimation.to
                let rw = flickableArea.width / zoomAnimation.to
                let rh = flickableArea.height / zoomAnimation.to
                let ow = originalSize_.width / dpr
                let oh = originalSize_.height / dpr

                let origCropX, origCropY, origCropWidth, origCropHeight
                if (rotationMode === 0) {
                    origCropX = rx
                    origCropY = ry
                    origCropWidth = rw
                    origCropHeight = rh
                } else if (rotationMode === 1) {
                    origCropX = ry
                    origCropY = oh - (rx + rw)
                    origCropWidth = rh
                    origCropHeight = rw
                } else if (rotationMode === 2) {
                    origCropX = ow - (rx + rw)
                    origCropY = oh - (ry + rh)
                    origCropWidth = rw
                    origCropHeight = rh
                } else if (rotationMode === 3) {
                    origCropX = ow - (ry + rh)
                    origCropY = rx
                    origCropWidth = rh
                    origCropHeight = rw
                }

                viewerImageCrop.unscaledX = origCropX
                viewerImageCrop.unscaledY = origCropY
                viewerImageCrop.unscaledWidth = origCropWidth
                viewerImageCrop.unscaledHeight = origCropHeight

                viewerImageCrop.source = imageIdUrl + "/" +
                        Math.round(origCropX * dpr) + "," +
                        Math.round(origCropY * dpr) + "," +
                        Math.round(origCropWidth * dpr) + "," +
                        Math.round(origCropHeight * dpr)
                viewerImage2.fromIndex = fromIndex
            }

            delayedIdSetter.idToSet = imageIdUrl
            delayedIdSetter.restart()
        }
        flickableArea.originalSize = Qt.size(originalSize_.width / dpr, originalSize_.height / dpr)
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
        visible: !hideVerticalScrollBar && (!zoomFitView || viewportAnimation.running) && size < 1
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

    Item {
        id: viewerImage

        opacity: flickableArea.imageTextureReady ? 1 : 0

        width: animatedEffectiveWidth * zoomScale
        height: animatedEffectiveHeight * zoomScale

        property string source: ""
        property int fromIndex: -1
        property int fromLevel: -1

        Item {
            id: unrotatedContent
            x: (parent.width - width) / 2
            y: (parent.height - height) / 2
            width: originalSize.width * zoomScale
            height: originalSize.height * zoomScale

            rotation: rotationMode * 90
            onRotationChanged: {
                if (isRotating) {
                    updatePinnedPosition()
                }
            }
            Behavior on rotation {
                enabled: animateRotation
                RotationAnimation {
                    duration: animationDuration
                    direction: RotationAnimation.Shortest
                    easing.type: Easing.InOutQuad
                }
            }

            Image {
                id: viewerImageBase
                anchors.fill: parent
                source: viewerImage.source
                cache: false
                mipmap: false
                asynchronous: false
                visible: false
            }

            Image {
                id: viewerImage2
                anchors.fill: parent
                cache: false
                mipmap: true
                asynchronous: true
                property int fromIndex: -1
                visible: false
            }

            ShaderEffect {
                id: viewerImageShader
                anchors.fill: parent
                property var source: viewerImage2.status === Image.Ready ? viewerImage2 : viewerImageBase
                property var viewportSize: Qt.size(width * dpr, height * dpr)
                property real sharpenAmount: zoomScale < 1 ? 1.5 : 0
                property bool showCheckerboard: flickableArea.sourceMasonry.view.showTransparentGrid &&
                                                flickableArea.imageTextureReady
                property int checkerboardSize: 4 * dpr
                property int borderRadius: 0

                fragmentShader: "qrc:/resources/shader.frag.qsb"

                Image {
                    id: viewerImageCrop
                    cache: false

                    property real unscaledX
                    property real unscaledY
                    property real unscaledWidth
                    property real unscaledHeight

                    x: unscaledX * zoomScale
                    y: unscaledY * zoomScale
                    width: unscaledWidth * zoomScale
                    height: unscaledHeight * zoomScale

                    property int fromIndex: -1

                    visible: viewerImage2.status !== Image.Ready
                }
            }
        }
    }

    PinchArea {
        id: pinchZoomArea
        anchors.fill: parent
        enabled: pinchZoomEnabled && root.state === "viewer"
        z: 2

        onPinchStarted: (pinch) => {
            beginPinchZoom(pinch.center.x, pinch.center.y)
            updatePinchZoom(pinch.scale)
            pinch.accepted = true
        }

        onPinchUpdated: (pinch) => {
            updatePinchZoom(pinch.scale)
            pinch.accepted = true
        }

        onPinchFinished: (pinch) => {
            finishPinchZoom()
            pinch.accepted = true
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
