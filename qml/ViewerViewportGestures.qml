pragma ComponentBehavior: Bound

import QtQuick

// Pinch and wheel gesture state operate on the same public viewport contract.
QtObject {
    required property var viewport
    required property var image
    required property var viewportFrameAnimation
    property bool pinchFinishPending: false
    property real inertiaProgress: 0
    property real inertiaScale: 1
    property point inertiaStart: Qt.point(0, 0)
    property point inertiaEnd: Qt.point(0, 0)
    property point inertiaTangent: Qt.point(0, 0)
    onInertiaProgressChanged: updatePanInertia()

    function startPanInertia(velocityX, velocityY) {
        const duration = 500
        viewport.viewportAnimation.stop()
        inertiaScale = viewport.zoomScale
        inertiaStart = Qt.point(rawPanPosition(image.x, true, inertiaScale),
                                rawPanPosition(image.y, false, inertiaScale))
        // Velocities were measured after resistance; the trajectory lives in
        // unresisted coordinates and is projected to the same 2:1 rubber band.
        const rawVelocityX = velocityX * (image.x === fitViewerImageInViewportBoundsX(image.x) ? 1 : 2)
        const rawVelocityY = velocityY * (image.y === fitViewerImageInViewportBoundsY(image.y) ? 1 : 2)
        inertiaTangent = Qt.point(rawVelocityX * duration / 1000,
                                 rawVelocityY * duration / 1000)
        inertiaEnd = Qt.point(fitViewerImageInViewportBoundsX(inertiaStart.x + inertiaTangent.x / 3),
                              fitViewerImageInViewportBoundsY(inertiaStart.y + inertiaTangent.y / 3))
        viewport.xAnimation.to = inertiaEnd.x
        viewport.yAnimation.to = inertiaEnd.y
        viewport.zoomAnimation.to = viewport.zoomScale
        viewport.viewportAnimation.startPan(duration)
    }

    function updatePanInertia() {
        if (!image) return
        // Cubic Hermite: release velocity at the start, zero velocity at the
        // bounded endpoint. An edge excursion and its return share this clock.
        const t = inertiaProgress
        const startWeight = 2 * t * t * t - 3 * t * t + 1
        const tangentWeight = t * t * t - 2 * t * t + t
        const endWeight = 1 - startWeight
        image.x = displayedPanPosition(inertiaStart.x * startWeight
                    + inertiaTangent.x * tangentWeight + inertiaEnd.x * endWeight, true, inertiaScale)
        image.y = displayedPanPosition(inertiaStart.y * startWeight
                    + inertiaTangent.y * tangentWeight + inertiaEnd.y * endWeight, false, inertiaScale)
    }

    function beginPinchZoom(centerX, centerY) {
        if (viewport.effectiveOriginalSize.width <= 1 || viewport.effectiveOriginalSize.height <= 1) {
            return
        }

        viewport.viewportAnimation.stop()
        viewportFrameAnimation.running = false
        pinchFinishPending = false
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
        pinchFinishPending = false
        viewport.pinchZoomActive = false
        if (viewport.pinchZoomOutToThumbnailsActive) {
            let commit = viewport.pinchZoomOutToThumbnailsProgress >= viewport.pinchZoomOutToThumbnailsCommitProgress
            viewport.pinchZoomOutToThumbnailsActive = false
            viewport.pinchZoomOutToThumbnailsFinished(commit)
            return
        }

        if (viewport.wheelPanActive) {
            pinchFinishPending = true
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
        pinchFinishPending = false
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
        if (viewport.zoomFitView || viewport.pinchZoomActive) {
            cancelWheelPan()
            return
        }

        if (pinchFinishPending) {
            cancelWheelPan()
            finishPinchZoom()
            return
        }

        const recentMotion = Date.now() - viewport.wheelPanLastTime <= 100
        const velocityX = recentMotion ? averageWheelPanVelocity(viewport.wheelPanVelocityHistoryX) : 0
        const velocityY = recentMotion ? averageWheelPanVelocity(viewport.wheelPanVelocityHistoryY) : 0
        if (Math.abs(velocityX) > 20 || Math.abs(velocityY) > 20) {
            startPanInertia(velocityX, velocityY)
        } else {
            viewport.xAnimation.to = fitViewerImageInViewportBoundsX(image.x)
            viewport.yAnimation.to = fitViewerImageInViewportBoundsY(image.y)
            viewport.zoomAnimation.to = viewport.zoomScale
            viewport.xAnimation.duration = viewport.animationDuration
            viewport.yAnimation.duration = viewport.animationDuration
            viewport.zoomAnimation.duration = 0
            viewport.viewportAnimation.easing = Easing.OutSine
            viewport.viewportAnimation.restart()
        }

        cancelWheelPan()
    }

    function rawPanPosition(position, horizontal, targetScale) {
        const edge = horizontal ? fitViewerImageInViewportBoundsX(position, targetScale)
                                : fitViewerImageInViewportBoundsY(position, targetScale)
        return edge + (position - edge) * 2
    }

    function displayedPanPosition(position, horizontal, targetScale) {
        const edge = horizontal ? fitViewerImageInViewportBoundsX(position, targetScale)
                                : fitViewerImageInViewportBoundsY(position, targetScale)
        return edge + (position - edge) * 0.5
    }

    // Invert resistance before adding deltas so reversal and crossings are exact.
    function resistantPanPosition(position, delta, horizontal, targetScale) {
        return displayedPanPosition(rawPanPosition(position, horizontal, targetScale) + delta,
                                    horizontal, targetScale)
    }

    function panBy(deltaX, deltaY, recordVelocity) {
        if (viewport.zoomFitView) {
            return Qt.point(deltaX, deltaY)
        }

        if (!viewport.wheelPanActive)
            beginWheelPan()
        viewport.viewportAnimation.stop()

        let oldX = image.x
        let oldY = image.y
        let targetX = resistantPanPosition(image.x, deltaX, true)
        let targetY = resistantPanPosition(image.y, deltaY, false)

        image.x = targetX
        image.y = targetY
        viewport.xAnimation.to = targetX
        viewport.yAnimation.to = targetY
        viewport.zoomAnimation.to = viewport.zoomScale
        if (viewport.pinchZoomActive) {
            viewport.pinchStartCenterX += targetX - oldX
            viewport.pinchStartCenterY += targetY - oldY
        }
        if (recordVelocity) {
            recordWheelPanVelocity(targetX - oldX, targetY - oldY)
        }

        viewport.forceShowScrollBars = true
        viewport.forceShowScrollBars = false

        // A zoomed image owns the complete pan, including resisted overshoot.
        // Handing this remainder to file navigation moves a different viewport.
        return Qt.point(0, 0)
    }

    function settlePan() {
        if (viewport.zoomFitView || viewport.pinchZoomActive || viewport.wheelPanActive
                || viewport.viewportAnimation.panRunning) {
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
