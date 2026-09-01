pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    required property Item viewer
    required property FlickableZoomable viewport
    required property GalleryViewerMotion motion
    required property Item neighborImage

    function resetViewerNavigation() {
        motion.navigationFinishTimer.stop()
        motion.navigationOffsetAnimation.stop()
        viewer.viewerNavigationOffsetX = 0
        viewer.viewerNavigationOverdrag = 0
        viewer.viewerNavigationVelocityX = 0
        viewer.viewerNavigationLastTime = 0
        viewer.viewerNavigationActive = false
        viewer.viewerNavigationRevealed = false
        viewer.viewerNavigationCommitAfterAnimation = false
        viewer.viewerNavigationDirection = 0
        viewer.viewerNavigationTargetIndex = -1
        viewer.viewerNavigationTargetSource = ""
        viewer.viewerNavigationTargetSourceLevel = -1
        viewer.viewerNavigationTargetOriginalSize = Qt.size(0, 0)
    }

    function beginViewerNavigationGesture(forceNew, hasPhase) {
        if (hasPhase)
            motion.navigationGestureEndTimer.stop()
        if (forceNew || !viewer.viewerNavigationGestureActive) {
            motion.navigationResidualQuietTimer.stop()
            viewer.viewerNavigationGestureActive = true
            viewer.viewerNavigationGestureCommitted = false
            viewer.viewerNavigationSuppressMomentum = false
            viewer.viewerNavigationLastTime = 0
        }
        viewer.viewerNavigationGestureHasPhase = hasPhase
    }

    function continueViewerNavigationGesture(hasPhase) {
        beginViewerNavigationGesture(false, hasPhase)
        if (!hasPhase)
            motion.navigationGestureEndTimer.restart()
    }

    function endViewerNavigationGesture(clearCommitted) {
        motion.navigationGestureEndTimer.stop()
        viewer.viewerNavigationGestureActive = false
        viewer.viewerNavigationGestureHasPhase = false
        if (clearCommitted === undefined || clearCommitted) {
            viewer.viewerNavigationGestureCommitted = false
            viewer.viewerNavigationSuppressMomentum = false
            motion.navigationResidualQuietTimer.stop()
        }
        viewer.viewerNavigationLastTime = 0
    }

    function startViewerNavigationResidualSuppression() {
        viewer.viewerNavigationSuppressMomentum = true
        motion.navigationResidualQuietTimer.restart()
    }

    function clearViewerNavigationResidualSuppression() {
        if (!viewer.viewerNavigationSuppressMomentum)
            return
        if (motion.navigationOffsetAnimation.running
                || viewer.viewerNavigationCommitAfterAnimation) {
            motion.navigationResidualQuietTimer.restart()
            return
        }
        viewer.viewerNavigationSuppressMomentum = false
        if (!viewer.viewerNavigationGestureActive)
            viewer.viewerNavigationGestureCommitted = false
    }

    function hiddenNavigationOffset(overdrag) {
        return Math.min(overdrag * 0.35,
                        viewer.viewerNavigationOverdragThreshold * 0.5)
    }

    function beginViewerNavigation(direction) {
        const target = viewer.adjacentIndex(viewer.presentedIndex, direction)
        viewer.viewerNavigationActive = true
        viewer.viewerNavigationDirection = direction
        viewer.viewerNavigationTargetIndex = target !== viewer.presentedIndex ? target : -1
        viewer.viewerNavigationTargetSource = ""
        viewer.viewerNavigationTargetSourceLevel = -1
        viewport.cancelWheelPan()
        if (viewer.viewerNavigationTargetIndex >= 0) {
            if (!viewport.zoomFitView) {
                if (viewer.viewerNavigationTargetIndex === viewer.session.currentIndex)
                    viewer.session.requestViewer(0, 0)
                else
                    viewer.session.requestViewerAt(viewer.viewerNavigationTargetIndex, 0, 0)
            } else {
                viewer.requestIndex(viewer.viewerNavigationTargetIndex, 1)
            }
        }
        // requestViewerAt records the requested current geometry synchronously.
        // Read tiers only afterwards so an old native/resize plan can never
        // seed the transition surface.
        viewer.refreshNeighborSource()
    }

    function applyViewerNavigationDelta(deltaX) {
        if (Math.abs(deltaX) < 0.1)
            return
        const direction = deltaX < 0 ? 1 : -1
        if (!viewer.viewerNavigationActive
                || (viewer.viewerNavigationDirection !== direction
                    && !viewer.viewerNavigationRevealed)) {
            resetViewerNavigation()
            beginViewerNavigation(direction)
        }
        const signedDelta = -viewer.viewerNavigationDirection * deltaX
        viewer.viewerNavigationOverdrag = Math.max(
                    0, viewer.viewerNavigationOverdrag + signedDelta)
        if (viewer.viewerNavigationOverdrag <= 0.1) {
            resetViewerNavigation()
            return
        }
        if (viewer.viewerNavigationTargetIndex === -1) {
            viewer.viewerNavigationRevealed = false
            viewer.viewerNavigationOffsetX = -viewer.viewerNavigationDirection
                    * Math.min(viewer.viewerNavigationOverdrag * 0.25,
                               viewer.viewerNavigationOverdragThreshold * 0.6)
            return
        }
        if (viewer.viewerNavigationOverdrag < viewer.viewerNavigationOverdragThreshold) {
            viewer.viewerNavigationRevealed = false
            viewer.viewerNavigationOffsetX = viewer.viewerNavigationDirection < 0
                    ? viewer.viewerNavigationOverdrag
                    : -viewer.viewerNavigationDirection
                      * hiddenNavigationOffset(viewer.viewerNavigationOverdrag)
            return
        }
        viewer.viewerNavigationRevealed = true
        const visibleDistance = viewer.viewerNavigationDirection < 0
                ? viewer.viewerNavigationOverdrag
                : hiddenNavigationOffset(viewer.viewerNavigationOverdragThreshold)
                  + viewer.viewerNavigationOverdrag
                  - viewer.viewerNavigationOverdragThreshold
        const maximumOffset = viewer.viewerNavigationDirection < 0
                ? viewer.viewerNavigationTargetTravelDistance : viewer.width
        viewer.viewerNavigationOffsetX = -viewer.viewerNavigationDirection
                * Math.min(visibleDistance, maximumOffset)
    }

    function finishViewerNavigation() {
        motion.navigationFinishTimer.stop()
        if (!viewer.viewerNavigationActive) {
            viewport.finishWheelPan()
            return
        }
        const signedVelocity = -viewer.viewerNavigationDirection
                * viewer.viewerNavigationVelocityX
        const shouldCommit = viewer.viewerNavigationTargetIndex !== -1
                && viewer.viewerNavigationRevealed
                && (viewer.viewerNavigationOverdrag
                    >= viewer.viewerNavigationCommitThreshold
                    || signedVelocity > 900)
        if (shouldCommit)
            viewer.viewerNavigationGestureCommitted = true
        const targetOffset = shouldCommit
                ? (viewer.viewerNavigationDirection < 0
                   ? viewer.viewerNavigationTargetTravelDistance
                   : -viewer.viewerNavigationDirection * viewer.width) : 0
        const fullDistance = viewer.viewerNavigationDirection < 0
                ? viewer.viewerNavigationTargetTravelDistance : viewer.width
        const remainingRatio = Math.min(
                    1, Math.abs(targetOffset - viewer.viewerNavigationOffsetX)
                       / Math.max(1, fullDistance))
        viewer.viewerNavigationCommitAfterAnimation = shouldCommit
        motion.navigationOffsetAnimation.to = targetOffset
        motion.navigationOffsetAnimation.duration = shouldCommit
                ? viewer.animationDuration * (1 + remainingRatio)
                : viewer.animationDuration
        motion.navigationOffsetAnimation.restart()
    }

    function commitViewerNavigation() {
        const target = viewer.viewerNavigationTargetIndex
        const preserveViewport = viewer.viewerNavigationTargetKeepsZoom
        const targetScale = viewer.viewerNavigationTargetScale
        const targetX = viewer.viewerNavigationTargetFinalImageX
        const targetY = viewer.viewerNavigationTargetFinalImageY
        const targetSource = viewer.viewerNavigationTargetSource
        const targetSourceLevel = viewer.viewerNavigationTargetSourceLevel
        // QML value-type reads may retain a live wrapper for the source
        // property. resetViewerNavigation() clears that property below, so
        // take an independent size value for the prepared-frame handoff.
        const targetOriginalSize = Qt.size(
                    viewer.viewerNavigationTargetOriginalSize.width,
                    viewer.viewerNavigationTargetOriginalSize.height)
        const canAdoptFitTransition = viewport.zoomFitView
                && !viewer.sphericViewerMode && targetSourceLevel === 1
                && targetSource.toString() !== ""
                && targetOriginalSize.width > 1
                && targetOriginalSize.height > 1
                && neighborImage.status === Image.Ready
        viewer.viewerNavigationGestureCommitted = true
        startViewerNavigationResidualSuppression()
        resetViewerNavigation()
        if (target >= 0 && target !== viewer.presentedIndex) {
            if (canAdoptFitTransition) {
                // The prepared frame already visible at the end of the swipe
                // can become FlickableZoomable's base tier before the authority
                // round-trip. This prevents setPresentedIndex() from briefly
                // installing the target's masonry thumbnail at commit time.
                viewport.setImage(targetSource, targetOriginalSize,
                                       target, targetSourceLevel)
            }
            if (preserveViewport) {
                viewer.pendingCommittedViewport = {
                    index: target,
                    scale: targetScale,
                    x: targetX,
                    y: targetY
                }
                viewer.pendingCommittedViewportAttempts = 0
            }
            viewer.emitNavigation(target)
            if (preserveViewport)
                motion.committedViewportTimer.restart()
        }
    }

    function applyPendingCommittedViewport() {
        if (!viewer.pendingCommittedViewport
                || viewer.pendingCommittedViewport.index !== viewer.presentedIndex) {
            viewer.pendingCommittedViewport = null
            return
        }
        if (viewport.effectiveOriginalSize.width <= 1
                || viewport.effectiveOriginalSize.height <= 1) {
            if (++viewer.pendingCommittedViewportAttempts < 40)
                motion.committedViewportTimer.restart()
            else
                viewer.pendingCommittedViewport = null
            return
        }
        viewport.setViewport(viewer.pendingCommittedViewport.scale,
                                  viewer.pendingCommittedViewport.x,
                                  viewer.pendingCommittedViewport.y)
        viewer.pendingCommittedViewport = null
        motion.decodeRequestTimer.start()
    }

    function finishViewerNavigationAnimationNow() {
        if (!motion.navigationOffsetAnimation.running
                && !viewer.viewerNavigationCommitAfterAnimation)
            return
        motion.navigationOffsetAnimation.stop()
        if (viewer.viewerNavigationCommitAfterAnimation)
            commitViewerNavigation()
        else {
            resetViewerNavigation()
            viewport.settlePan()
        }
    }

    function wheelDeltaPixels(pixelDelta, angleDelta) {
        return pixelDelta !== 0 ? pixelDelta : angleDelta / 120 * 80
    }

    function isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY,
                                      angleDeltaX, angleDeltaY, phase,
                                      hasPixelDelta, nativeMomentum,
                                      nativePhase, nativeMomentumPhase) {
        if (angleDeltaY === 0 || angleDeltaX !== 0 || nativeMomentum)
            return false
        if (!hasPixelDelta)
            return true
        const phaseFree = phase === ViewerWheelArea.NoScrollPhase
                && nativePhase === 0 && nativeMomentumPhase === 0
        return phaseFree && Math.abs(pixelDeltaY) > 0
                && Math.abs(pixelDeltaX) < Math.abs(pixelDeltaY) * 0.2
    }

    function panZoomedImageFromWheel(deltaX, deltaY, recordVelocity) {
        if (viewport.zoomFitView)
            return Qt.point(deltaX, deltaY)
        motion.wheelPanFinishTimer.stop()
        return viewport.panBy(deltaX, deltaY, recordVelocity)
    }

    function scheduleWheelPanFallbackFinish() {
        if (!viewport.zoomFitView)
            motion.wheelPanFinishTimer.restart()
    }

    function consumeMomentumState(nativeMomentum, effectivePhase,
                                  deltaX, deltaY) {
        if (nativeMomentum && !viewport.zoomFitView
                && !viewer.viewerNavigationActive
                && !viewer.viewerNavigationCommitAfterAnimation
                && !viewer.viewerNavigationGestureCommitted
                && !viewer.viewerNavigationSuppressMomentum) {
            panZoomedImageFromWheel(deltaX, deltaY, false)
            return true
        }
        if (viewer.viewerNavigationSuppressMomentum
                && !viewer.viewerNavigationGestureActive) {
            const physicalRestart = !nativeMomentum
                    && effectivePhase !== ViewerWheelArea.ScrollMomentum
                    && effectivePhase !== ViewerWheelArea.ScrollEnd
            if (physicalRestart)
                clearViewerNavigationResidualSuppression()
            else {
                motion.navigationResidualQuietTimer.restart()
                return true
            }
        }
        if (nativeMomentum && !viewer.viewerNavigationGestureActive) {
            startViewerNavigationResidualSuppression()
            return true
        }
        if (!nativeMomentum || !viewer.viewerNavigationGestureActive)
            return false
        if (viewer.viewerNavigationActive) {
            finishViewerNavigation()
            endViewerNavigationGesture(false)
            startViewerNavigationResidualSuppression()
        } else if (!viewport.zoomFitView) {
            endViewerNavigationGesture(true)
            panZoomedImageFromWheel(deltaX, deltaY, false)
        } else {
            endViewerNavigationGesture(false)
            startViewerNavigationResidualSuppression()
        }
        return true
    }

    function consumeWheelPhase(effectivePhase) {
        if (effectivePhase === ViewerWheelArea.ScrollBegin) {
            finishViewerNavigationAnimationNow()
            beginViewerNavigationGesture(true, true)
            if (!viewport.zoomFitView)
                viewport.beginWheelPan()
            motion.navigationFinishTimer.stop()
            motion.wheelPanFinishTimer.stop()
            return true
        }
        if (effectivePhase === ViewerWheelArea.ScrollEnd) {
            if (!viewer.viewerNavigationGestureActive)
                return true
            const hadNavigation = viewer.viewerNavigationActive
            if (hadNavigation)
                finishViewerNavigation()
            else if (!viewport.zoomFitView)
                scheduleWheelPanFallbackFinish()
            endViewerNavigationGesture(false)
            if (hadNavigation || viewport.zoomFitView)
                startViewerNavigationResidualSuppression()
            return true
        }
        if (effectivePhase === ViewerWheelArea.ScrollMomentum
                && (viewer.viewerNavigationSuppressMomentum
                    || !viewer.viewerNavigationGestureActive)) {
            startViewerNavigationResidualSuppression()
            return true
        }
        return false
    }

    function consumeLegacyWheel(pixelDeltaX, pixelDeltaY,
                                angleDeltaX, angleDeltaY, phase,
                                hasPixelDelta, nativeMomentum,
                                nativePhase, nativeMomentumPhase) {
        if (!isLegacyWheelImageSwitch(
                    pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                    phase, hasPixelDelta, nativeMomentum, nativePhase,
                    nativeMomentumPhase))
            return false
        finishViewerNavigationAnimationNow()
        endViewerNavigationGesture(true)
        viewport.cancelWheelPan()
        viewer.navigate(angleDeltaY < 0 ? 1 : -1)
        return true
    }

    function applyWheelIntent(deltaX, deltaY, phaseAware) {
        const horizontalIntent = viewer.viewerNavigationActive
                || Math.abs(deltaX) >= Math.abs(deltaY) * 0.6
        if (!horizontalIntent) {
            if (!viewport.zoomFitView) {
                panZoomedImageFromWheel(0, deltaY, true)
                if (!phaseAware)
                    scheduleWheelPanFallbackFinish()
            }
            return
        }
        let remainingX = deltaX
        if (!viewer.viewerNavigationActive && !viewport.zoomFitView)
            remainingX = panZoomedImageFromWheel(
                        remainingX, deltaY, true).x
        if (Math.abs(remainingX) < 0.1) {
            if (!phaseAware)
                scheduleWheelPanFallbackFinish()
            return
        }
        const now = Date.now()
        const elapsed = viewer.viewerNavigationLastTime
                ? Math.max(1, now - viewer.viewerNavigationLastTime) : 16
        viewer.viewerNavigationVelocityX = remainingX / elapsed * 1000
        viewer.viewerNavigationLastTime = now
        applyViewerNavigationDelta(remainingX)
        if (!phaseAware)
            motion.navigationFinishTimer.restart()
    }

    function handleViewerWheel(pixelDeltaX, pixelDeltaY,
                               angleDeltaX, angleDeltaY, phase, modifiers,
                               buttons, hasPixelDelta, inverted, source,
                               deviceType, nativeMomentum, nativePhase,
                               nativeMomentumPhase) {
        const phaseAware = phase !== ViewerWheelArea.NoScrollPhase
        const effectivePhase = nativeMomentum
                && phase === ViewerWheelArea.ScrollBegin
                ? ViewerWheelArea.ScrollMomentum : phase
        const deltaX = wheelDeltaPixels(pixelDeltaX, angleDeltaX)
        const deltaY = wheelDeltaPixels(pixelDeltaY, angleDeltaY)
        if (consumeMomentumState(
                    nativeMomentum, effectivePhase, deltaX, deltaY)
                || consumeWheelPhase(effectivePhase)
                || viewer.viewerNavigationCommitAfterAnimation)
            return
        if (consumeLegacyWheel(
                    pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                    phase, hasPixelDelta, nativeMomentum, nativePhase,
                    nativeMomentumPhase))
            return
        continueViewerNavigationGesture(phaseAware)
        if (!viewer.viewerNavigationGestureCommitted)
            applyWheelIntent(deltaX, deltaY, phaseAware)
    }


}
