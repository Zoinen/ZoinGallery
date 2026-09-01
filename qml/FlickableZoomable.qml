pragma ComponentBehavior: Bound
import QtQuick

import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: flickableArea

    property var viewerModel: null
    property var sourceMasonry: null

    // All application state is explicit, so embedders need no dynamic root.
    property bool active: true
    property real devicePixelRatio: 1.0
    property real topInset: 0
    property bool checkerboardEnabled: false
    property GalleryThemePalette scrollBarTheme: GalleryThemePalette {}
    property url simpleSource: ""
    property int simpleSourceIndex: -1
    property size simpleSourceOriginalSize: Qt.size(0, 0)
    property int appliedSimpleSourceIndex: -1
    property bool simpleSourceMetadataKnown: false
    property bool sourceSizeFallbackPending: false

    property alias image: viewerImage
    readonly property alias viewerImageBase: viewerImage.baseImage
    readonly property alias viewerImage2: viewerImage.nativeImage
    readonly property alias viewerImageCrop: viewerImage.cropImage
    readonly property alias viewerImageShader: viewerImage.shader
    readonly property alias unrotatedContent: viewerImage.unrotatedContent
    readonly property alias vbar: viewportMotion.verticalBar
    readonly property alias hbar: viewportMotion.horizontalBar
    readonly property alias viewportAnimation: viewportMotion.animation
    readonly property alias zoomAnimation: viewportMotion.zoomAnimation
    readonly property alias xAnimation: viewportMotion.xAnimation
    readonly property alias yAnimation: viewportMotion.yAnimation
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
    readonly property real targetZoomScale: zoomAnimation.to
    readonly property bool zoomScrollingAnimationRunning: frameAnimation.running
    readonly property real minZoomScale: 0.005
    readonly property real maxZoomScale: 128
    property int animationDuration

    property real imagePressedX: 0
    property real imagePressedY: 0

    property bool forceShowScrollBars: false
    property bool hideVerticalScrollBar: false
    property bool dragZoomActive: pointerLayer.pressed || viewportAnimation.running
    property bool scrollBarsVisible: (hbar.hovered || hbar.pressed ||
            (!hideVerticalScrollBar && (vbar.hovered || vbar.pressed)) ||
            dragZoomActive || frameAnimation.running || forceShowScrollBars) && !zoomFitView

    property bool fitToHeight: effectiveOriginalSize.width / effectiveOriginalSize.height <= flickableArea.width / flickableArea.height
    property real scrollBarsRightMargin: 0

    signal clicked
    signal closeRequested
    signal middleClickRequested

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

    // ViewerWheelArea sits above this item so it can normalize trackpad
    // navigation. An ignored wheel event is not guaranteed to continue to a
    // lower sibling on every Qt Quick backend, so both input paths call this
    // single copy of ViewerMode's original zoom-wheel implementation.
    function handleZoomWheel(angleDeltaY, modifiers, buttons) {
        if (modifiers !== Qt.ControlModifier && !(buttons & Qt.LeftButton))
            return

        let angleDelta = angleDeltaY * 1.1
        let delta = Math.abs(angleDelta / 100)
        if (angleDelta < 0)
            delta = 1 / delta

        zoomFitView = false

        zoomAnimation.to *= delta
        if (modifiers === Qt.ControlModifier && !(buttons & Qt.LeftButton)) {
            xAnimation.to = flickableArea.width / 2
                    - ((flickableArea.width / 2 - viewerImage.x) / zoomScale)
                    * zoomAnimation.to
            yAnimation.to = flickableArea.height / 2
                    - ((flickableArea.height / 2 - viewerImage.y) / zoomScale)
                    * zoomAnimation.to
        } else {
            xAnimation.to = pointerLayer.mouseX
                    - imagePressedX * zoomAnimation.to
            yAnimation.to = pointerLayer.mouseY
                    - imagePressedY * zoomAnimation.to
        }

        xAnimation.duration = 100
        yAnimation.duration = 100
        zoomAnimation.duration = 100

        viewportAnimation.easing = Easing.OutSine
        viewportAnimation.restart()
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

        if (active && transitionProgress > 0) {
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

        if (!active || effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
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
            targetX = pointerLayer.mouseX - (imagePressedX) * targetScale
            targetY = pointerLayer.mouseY - (imagePressedY) * targetScale
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
        // Preserve a Fit request made before metadata/base texture readiness.
        // The explicit tier handoff will apply it as soon as a usable source
        // size arrives.
        zoomFitView = true
        if (effectiveOriginalSize.width <= 1 || effectiveOriginalSize.height <= 1) {
            return
        }

        // Show scrollbars
        delayedScrollbarHiding.stop()
        scrollBarHidingAnimation.stop()
        hbar.opacity = 1
        vbar.opacity = 1

        let targetWidth = fitToHeight ? flickableArea.height * (effectiveOriginalSize.width / effectiveOriginalSize.height) :
                                                    flickableArea.width
        let targetHeight = fitToHeight ? flickableArea.height :
                                                     flickableArea.width / (effectiveOriginalSize.width / effectiveOriginalSize.height)

        const targetScale = fitToHeight
                ? flickableArea.height / effectiveOriginalSize.height
                : flickableArea.width / effectiveOriginalSize.width
        const targetX = flickableArea.width / 2 - targetWidth / 2
        const targetY = flickableArea.height / 2 - targetHeight / 2

        if (skipAnimation) {
            // A zero-duration ParallelAnimation still commits on an animation
            // tick. During a cold viewer open the viewport geometry and the
            // decoded source tier can both change in that interval, leaving
            // the image with the previous tier's scale/offset for a frame (or
            // until the viewer is reopened). Apply the fitted viewport as one
            // synchronous state change instead.
            viewportAnimation.stop()
            frameAnimation.running = false
            zoomScale = targetScale
            viewerImage.x = targetX
            viewerImage.y = targetY
            zoomAnimation.to = targetScale
            xAnimation.to = targetX
            yAnimation.to = targetY
            return
        } else {
            xAnimation.duration = animationDuration
            yAnimation.duration = animationDuration
            zoomAnimation.duration = animationDuration
        }

        zoomAnimation.to = targetScale
        xAnimation.to = targetX
        yAnimation.to = targetY

        viewportAnimation.easing = Easing.InOutQuad
        viewportAnimation.restart()
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
        sourceController.resetViewerImages()
    }

    function remapImageIndex(oldIndex, newIndex) {
        sourceController.remapImageIndex(oldIndex, newIndex)
    }

    function applyOriginalSize(nextOriginalSize) {
        sourceController.applyOriginalSize(nextOriginalSize)
    }

    function setImage(imageIdUrl, originalSize_, fromIndex, level) {
        sourceController.setImage(imageIdUrl, originalSize_, fromIndex, level)
    }

    function applySimpleSource() {
        sourceController.applySimpleSource()
    }

    onSimpleSourceChanged: applySimpleSource()
    onSimpleSourceIndexChanged: applySimpleSource()
    onSimpleSourceOriginalSizeChanged: applySimpleSource()

    ViewerSourceController {
        id: sourceController
        viewport: flickableArea
    }

    ViewerViewportMotion {
        id: viewportMotion
        anchors.fill: parent
        viewport: flickableArea
        imageItem: viewerImage
    }

    ViewerImageLayer {
        id: viewerImage
        viewport: flickableArea
    }

    ViewerViewportPointerLayer {
        id: pointerLayer
        anchors.fill: parent
        viewport: flickableArea
        imageItem: viewerImage
        viewportAnimation: viewportMotion.animation
        zoomAnimation: viewportMotion.zoomAnimation
        xAnimation: viewportMotion.xAnimation
        yAnimation: viewportMotion.yAnimation
    }

}
