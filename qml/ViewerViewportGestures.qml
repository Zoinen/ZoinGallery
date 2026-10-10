pragma ComponentBehavior: Bound

import QtQuick

// Pinch and wheel gesture state operate on the same public viewport contract.
QtObject {
    required property var viewport
    required property var image
    required property var viewportFrameAnimation

    function beginPinchZoom(centerX, centerY) {
        if (viewport.effectiveOriginalSize.width <= 1 || viewport.effectiveOriginalSize.height <= 1) {
            return
        }

        viewport.viewportAnimation.stop()
        viewportFrameAnimation.running = false
        viewport.pinchZoomActive = true
        viewport.pinchZoomOutToThumbnailsActive = false
        viewport.pinchZoomOutToThumbnailsProgress = 0
        viewport.pinchStartZoomScale = viewport.zoomScale
        viewport.pinchStartCenterX = centerX
        viewport.pinchStartCenterY = centerY
        viewport.pinchStartImagePointX = (centerX - image.x) / viewport.zoomScale
        viewport.pinchStartImagePointY = (centerY - image.y) / viewport.zoomScale
    }

    function updatePinchZoom(scale) {
        if (viewport.effectiveOriginalSize.width <= 1 || viewport.effectiveOriginalSize.height <= 1) {
            return
        }

        let targetScale = viewport.clampZoomScale(viewport.pinchStartZoomScale * scale)
        let fittedScale = viewport.fitZoomScale()
        let transitionDistance = Math.max(0.001, fittedScale * viewport.pinchZoomOutToThumbnailsScaleDistanceRatio)
        let transitionStartDistance = Math.min(transitionDistance * 0.8,
                Math.max(0, fittedScale * viewport.pinchZoomOutToThumbnailsStartScaleDistanceRatio))
        let activeTransitionDistance = Math.max(0.001, transitionDistance - transitionStartDistance)
        let transitionProgress = Math.max(0,
                Math.min(1, (fittedScale - transitionStartDistance - targetScale) / activeTransitionDistance))

        if (viewport.pinchZoomOutToThumbnailsActive) {
            if (transitionProgress > 0) {
                viewport.pinchZoomOutToThumbnailsProgress = transitionProgress
                viewport.pinchZoomOutToThumbnailsProgressed(transitionProgress)
                return
            }

            viewport.pinchZoomOutToThumbnailsActive = false
            viewport.pinchZoomOutToThumbnailsProgress = 0
            viewport.pinchZoomOutToThumbnailsProgressed(0)
        }

        let targetX = viewport.pinchStartCenterX - viewport.pinchStartImagePointX * targetScale
        let targetY = viewport.pinchStartCenterY - viewport.pinchStartImagePointY * targetScale

        viewport.zoomFitView = false
        viewport.zoomScale = targetScale
        image.x = targetX
        image.y = targetY
        viewport.zoomAnimation.to = targetScale
        viewport.xAnimation.to = targetX
        viewport.yAnimation.to = targetY

        viewport.forceShowScrollBars = true
        viewport.forceShowScrollBars = false

        if (viewport.active && transitionProgress > 0) {
            viewport.pinchZoomOutToThumbnailsActive = true
            viewport.pinchZoomOutToThumbnailsProgress = transitionProgress
            viewport.pinchZoomOutToThumbnailsProgressed(transitionProgress)
        }
    }

    function finishPinchZoom() {
        viewport.pinchZoomActive = false
        if (viewport.pinchZoomOutToThumbnailsActive) {
            let commit = viewport.pinchZoomOutToThumbnailsProgress >= viewport.pinchZoomOutToThumbnailsCommitProgress
            viewport.pinchZoomOutToThumbnailsActive = false
            viewport.pinchZoomOutToThumbnailsFinished(commit)
            return
        }

        if (!viewport.active || viewport.effectiveOriginalSize.width <= 1 || viewport.effectiveOriginalSize.height <= 1) {
            return
        }

        let fittedScale = viewport.fitZoomScale()
        if (viewport.zoomScale <= fittedScale * 1.02) {
            viewport.zoomToFit()
        }
        else {
            viewport.onControlReleased()
        }
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
        if (viewport.zoomFitView) {
            return
        }

        viewport.viewportAnimation.stop()
        viewport.wheelPanVelocityHistoryX = []
        viewport.wheelPanVelocityHistoryY = []
        viewport.wheelPanLastTime = 0
        viewport.wheelPanActive = true
    }

    function cancelWheelPan() {
        viewport.wheelPanVelocityHistoryX = []
        viewport.wheelPanVelocityHistoryY = []
        viewport.wheelPanLastTime = 0
        viewport.wheelPanActive = false
    }

    function recordWheelPanVelocity(consumedX, consumedY) {
        if (!viewport.wheelPanActive) {
            beginWheelPan()
        }

        let now = Date.now()
        let dt = viewport.wheelPanLastTime ? Math.max(1, now - viewport.wheelPanLastTime) : 16
        updateWheelPanVelocityHistory(viewport.wheelPanVelocityHistoryX, consumedX / dt * 1000, viewport.wheelPanHistorySize)
        updateWheelPanVelocityHistory(viewport.wheelPanVelocityHistoryY, consumedY / dt * 1000, viewport.wheelPanHistorySize)
        viewport.wheelPanLastTime = now
    }

    function finishWheelPan() {
        // Zoom forwarding and delayed pan-end notifications can reach here
        // without a pan. They must not replace a pending zoom destination.
        if (!viewport.wheelPanActive)
            return
        if (viewport.zoomFitView) {
            cancelWheelPan()
            return
        }

        let avgVelocityX = averageWheelPanVelocity(viewport.wheelPanVelocityHistoryX)
        let avgVelocityY = averageWheelPanVelocity(viewport.wheelPanVelocityHistoryY)
        let useInertia = viewport.wheelPanActive && (Math.abs(avgVelocityX) > 20 || Math.abs(avgVelocityY) > 20)
        let decelerationFactor = 0.1
        let targetX = image.x + (useInertia ? avgVelocityX * decelerationFactor : 0)
        let targetY = image.y + (useInertia ? avgVelocityY * decelerationFactor : 0)

        viewport.xAnimation.to = fitViewerImageInViewportBoundsX(targetX)
        viewport.yAnimation.to = fitViewerImageInViewportBoundsY(targetY)
        viewport.zoomAnimation.to = viewport.zoomScale
        viewport.xAnimation.duration = useInertia ? 500 : viewport.animationDuration
        viewport.yAnimation.duration = useInertia ? 500 : viewport.animationDuration
        viewport.zoomAnimation.duration = 0
        viewport.viewportAnimation.easing = useInertia ? Easing.OutCirc : Easing.OutSine
        viewport.viewportAnimation.restart()

        cancelWheelPan()
    }

    function panBy(deltaX, deltaY, recordVelocity) {
        if (viewport.zoomFitView) {
            return Qt.point(deltaX, deltaY)
        }

        viewport.viewportAnimation.stop()

        let oldX = image.x
        let oldY = image.y
        let targetX = fitViewerImageInViewportBoundsX(image.x + deltaX)
        let targetY = fitViewerImageInViewportBoundsY(image.y + deltaY)

        image.x = targetX
        image.y = targetY
        viewport.xAnimation.to = targetX
        viewport.yAnimation.to = targetY
        viewport.zoomAnimation.to = viewport.zoomScale
        if (recordVelocity) {
            recordWheelPanVelocity(targetX - oldX, targetY - oldY)
        }

        viewport.forceShowScrollBars = true
        viewport.forceShowScrollBars = false

        return Qt.point(deltaX - (targetX - oldX), deltaY - (targetY - oldY))
    }

    function settlePan() {
        if (viewport.zoomFitView) {
            return
        }

        viewport.xAnimation.to = fitViewerImageInViewportBoundsX(image.x)
        viewport.yAnimation.to = fitViewerImageInViewportBoundsY(image.y)
        viewport.zoomAnimation.to = viewport.zoomScale
        viewport.xAnimation.duration = viewport.animationDuration
        viewport.yAnimation.duration = viewport.animationDuration
        viewport.zoomAnimation.duration = 0
        viewport.viewportAnimation.easing = Easing.OutSine
        viewport.viewportAnimation.restart()
    }

    function fitViewerImageInViewportBoundsX(targetX, targetScale) {
        if (targetScale === undefined) {
            targetScale = viewport.zoomScale
        }
        let targetWidth = viewport.effectiveOriginalSize.width * targetScale
        targetX = Math.min(0, Math.max(targetX, viewport.width - targetWidth))
        if (targetWidth < viewport.width) {
            targetX = viewport.width / 2 - targetWidth / 2
        }
        return targetX
    }

    function fitViewerImageInViewportBoundsY(targetY, targetScale) {
        if (targetScale === undefined) {
            targetScale = viewport.zoomScale
        }
        let targetHeight = viewport.effectiveOriginalSize.height * targetScale
        targetY = Math.min(0, Math.max(targetY, viewport.height - targetHeight))
        if (targetHeight < viewport.height) {
            targetY = viewport.height / 2 - targetHeight / 2
        }
        return targetY
    }
}
