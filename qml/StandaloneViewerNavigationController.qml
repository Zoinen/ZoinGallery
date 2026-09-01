pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    required property Item viewer
    required property FlickableZoomable imageViewport
    required property Item neighborImage
    required property NumberAnimation navigationAnimation
    required property Timer navigationFinishTimer
    required property Timer wheelPanFinishTimer
    required property Timer gestureEndTimer
    required property Timer residualQuietTimer
    function viewerNavigationOriginalSize(index) {
        if (index === -1) {
            return Qt.size(0, 0)
        }
        const layoutSize = viewer.sourceMasonry.view.indexOriginalSize(index)
        if (layoutSize.width > 1 && layoutSize.height > 1) {
            return layoutSize
        }
        // Off-screen masonry bricks can still carry their provisional 0x0
        // geometry even though FileListModel already has metadata and an
        // prepared viewer frame. Read that authoritative metadata so
        // the swipe layer uses the same fitted rectangle as the main viewer.
        if (viewer.decodeModel &&
                typeof viewer.decodeModel.viewerImageOriginalSizeForIndex ===
                    "function") {
            return viewer.decodeModel.viewerImageOriginalSizeForIndex(
                        viewer.sourceIndexForViewIndex(index))
        }
        return Qt.size(0, 0)
    }
    readonly property size viewerNavigationTargetOriginalSize:
            viewerNavigationOriginalSize(viewer.viewerNavigationTargetIndex)
    readonly property bool viewerNavigationTargetHasSize: viewerNavigationTargetOriginalSize.width > 1 &&
            viewerNavigationTargetOriginalSize.height > 1 && viewer.width > 0 && viewer.height > 0
    readonly property size viewerNavigationTargetDisplayOriginalSize: viewerNavigationTargetHasSize ?
            Qt.size(viewerNavigationTargetOriginalSize.width / viewer.devicePixelRatio, viewerNavigationTargetOriginalSize.height / viewer.devicePixelRatio) :
            Qt.size(0, 0)
    readonly property size viewerNavigationTargetEffectiveOriginalSize: viewerNavigationTargetHasSize ?
            (imageViewport.rotationMode % 2 === 1 ?
                 Qt.size(viewerNavigationTargetDisplayOriginalSize.height, viewerNavigationTargetDisplayOriginalSize.width) :
                 viewerNavigationTargetDisplayOriginalSize) : Qt.size(0, 0)
    readonly property bool viewerNavigationTargetKeepsZoom: viewerNavigationTargetHasSize && !imageViewport.zoomFitView
    readonly property real viewerNavigationTargetAspect: viewerNavigationTargetHasSize ?
            viewerNavigationTargetEffectiveOriginalSize.width / viewerNavigationTargetEffectiveOriginalSize.height : 1
    readonly property bool viewerNavigationTargetFitToHeight: viewerNavigationTargetHasSize ?
            viewerNavigationTargetAspect <= viewer.width / viewer.height : false
    readonly property real viewerNavigationTargetScale: !viewerNavigationTargetHasSize ? 1 : viewerNavigationTargetKeepsZoom ?
            imageViewport.zoomScale :
            (viewerNavigationTargetFitToHeight ?
                 viewer.height / viewerNavigationTargetEffectiveOriginalSize.height :
                 viewer.width / viewerNavigationTargetEffectiveOriginalSize.width)
    readonly property real viewerNavigationTargetDisplayWidth: viewerNavigationTargetHasSize ?
            viewerNavigationTargetEffectiveOriginalSize.width * viewerNavigationTargetScale :
            viewer.width
    readonly property real viewerNavigationTargetDisplayHeight: viewerNavigationTargetHasSize ?
            viewerNavigationTargetEffectiveOriginalSize.height * viewerNavigationTargetScale :
            viewer.height
    readonly property real viewerNavigationTargetPreservedImageX: viewerNavigationTargetDisplayWidth < viewer.width ?
            (viewer.width - viewerNavigationTargetDisplayWidth) * 0.5 :
            Math.min(0, Math.max(imageViewport.image.x, viewer.width - viewerNavigationTargetDisplayWidth))
    readonly property real viewerNavigationTargetLeftAlignedImageX: viewerNavigationTargetDisplayWidth < viewer.width ?
            (viewer.width - viewerNavigationTargetDisplayWidth) * 0.5 :
            0
    readonly property real viewerNavigationTargetRightAlignedImageX: viewerNavigationTargetDisplayWidth < viewer.width ?
            (viewer.width - viewerNavigationTargetDisplayWidth) * 0.5 :
            viewer.width - viewerNavigationTargetDisplayWidth
    readonly property real viewerNavigationTargetFinalImageX: viewerNavigationTargetKeepsZoom ?
            (viewer.viewerNavigationDirection < 0 ? viewerNavigationTargetRightAlignedImageX :
                 viewer.viewerNavigationDirection > 0 ? viewerNavigationTargetLeftAlignedImageX :
                     viewerNavigationTargetPreservedImageX) : viewerNavigationTargetPreservedImageX
    readonly property real viewerNavigationTargetFinalImageY: viewerNavigationTargetDisplayHeight < viewer.height ?
            (viewer.height - viewerNavigationTargetDisplayHeight) * 0.5 :
            Math.min(0, Math.max(imageViewport.image.y, viewer.height - viewerNavigationTargetDisplayHeight))
    readonly property real viewerNavigationTargetTravelDistance: viewer.viewerNavigationDirection < 0 ?
            Math.max(1, viewerNavigationTargetDisplayWidth + viewerNavigationTargetFinalImageX) : Math.max(1, viewer.width)
    readonly property real viewerNavigationProgress: Math.min(1, Math.abs(viewer.viewerNavigationOffsetX) / Math.max(1, viewer.width * 0.5))
    readonly property real viewerNavigationCoverProgress: Math.min(1, Math.abs(viewer.viewerNavigationOffsetX) / viewerNavigationTargetTravelDistance)
    readonly property real viewerNavigationTargetOpacity: viewer.viewerNavigationDirection < 0 ? 1 : viewerNavigationProgress
    readonly property real viewerNavigationCurrentOpacity: viewer.viewerNavigationDirection < 0 && viewer.viewerNavigationTargetIndex !== -1 ? 1 - viewerNavigationCoverProgress : 1
    readonly property real viewerNavigationCurrentOffsetX: viewer.viewerNavigationDirection < 0 && viewer.viewerNavigationTargetIndex !== -1 ? 0 : viewer.viewerNavigationOffsetX
    readonly property real viewerNavigationTargetImageX: viewer.viewerNavigationDirection < 0 ?
            -viewerNavigationTargetDisplayWidth + Math.min(Math.max(viewer.viewerNavigationOffsetX, 0), viewerNavigationTargetTravelDistance) :
            viewerNavigationTargetFinalImageX
    readonly property real viewerNavigationTargetImageY: viewerNavigationTargetFinalImageY
    readonly property real viewerNavigationOverdragThreshold: Math.min(48, viewer.width * 0.08)
    readonly property real viewerNavigationCommitThreshold: Math.min(120,
            Math.max(viewerNavigationOverdragThreshold * 1.35, viewer.width * 0.12))

    function isTildeKey(event) {
        return event.key === Qt.Key_QuoteLeft || event.key === Qt.Key_AsciiTilde || event.key === 1025
    }

    function viewerGestureNumber(value) {
        return Number(value).toFixed(2)
    }

    function viewerGesturePhaseName(phase) {
        if (phase === ViewerWheelArea.ScrollBegin) {
            return "begin"
        }
        if (phase === ViewerWheelArea.ScrollUpdate) {
            return "update"
        }
        if (phase === ViewerWheelArea.ScrollEnd) {
            return "end"
        }
        if (phase === ViewerWheelArea.ScrollMomentum) {
            return "momentum"
        }
        return "none"
    }

    function viewerGestureDirectionName(direction) {
        if (direction > 0) {
            return "next"
        }
        if (direction < 0) {
            return "previous"
        }
        return "none"
    }

    function viewerGestureSnapshot() {
        return "gesture=" + viewer.viewerNavigationGestureSerial +
                " gestureActive=" + viewer.viewerNavigationGestureActive +
                " committed=" + viewer.viewerNavigationGestureCommitted +
                " phaseAware=" + viewer.viewerNavigationGestureHasPhase +
                " suppressMomentum=" + viewer.viewerNavigationSuppressMomentum +
                " navActive=" + viewer.viewerNavigationActive +
                " revealed=" + viewer.viewerNavigationRevealed +
                " dir=" + viewerGestureDirectionName(viewer.viewerNavigationDirection) +
                " offset=" + viewerGestureNumber(viewer.viewerNavigationOffsetX) +
                " overdrag=" + viewerGestureNumber(viewer.viewerNavigationOverdrag) +
                " velocity=" + viewerGestureNumber(viewer.viewerNavigationVelocityX) +
                " current=" + viewer.sourceMasonry.view.currentIndex +
                " target=" + viewer.viewerNavigationTargetIndex +
                " zoomFit=" + imageViewport.zoomFitView
    }

    function logViewerGesture(message) {
        if (!viewer.viewerGestureLogging) {
            return
        }
        console.log("[ViewerGesture] " + message + " | " + viewerGestureSnapshot())
    }

    function resetViewerNavigation(reason) {
        if (reason !== undefined && (viewer.viewerNavigationActive || Math.abs(viewer.viewerNavigationOffsetX) > 0.1 ||
                viewer.viewerNavigationOverdrag > 0.1 || viewer.viewerNavigationTargetIndex !== -1)) {
            logViewerGesture("reset reason=" + reason)
        }
        navigationFinishTimer.stop()
        navigationAnimation.stop()
        viewer.viewerNavigationOffsetX = 0
        viewer.viewerNavigationOverdrag = 0
        viewer.viewerNavigationVelocityX = 0
        viewer.viewerNavigationLastTime = 0
        viewer.viewerNavigationActive = false
        viewer.viewerNavigationRevealed = false
        viewer.viewerNavigationCommitAfterAnimation = false
        viewer.viewerNavigationDirection = 0
        viewer.viewerNavigationTargetIndex = -1
        viewer.viewerNavigationTargetPath = ""
        viewer.viewerNavigationTargetSource = ""
        viewer.viewerNavigationTargetSourceLevel = -1
        viewer.viewerNavigationTargetRequestWidth = -1
        viewer.viewerNavigationTargetRequestHeight = -1
    }

    function beginViewerNavigationGesture(forceNew, hasPhase) {
        if (hasPhase) {
            gestureEndTimer.stop()
        }
        if (forceNew || !viewer.viewerNavigationGestureActive) {
            residualQuietTimer.stop()
            viewer.viewerNavigationGestureSerial += 1
            viewer.viewerNavigationGestureActive = true
            viewer.viewerNavigationGestureCommitted = false
            viewer.viewerNavigationSuppressMomentum = false
            viewer.viewerNavigationLastTime = 0
            logViewerGesture("gesture begin forceNew=" + forceNew + " phaseAware=" + hasPhase)
        }
        viewer.viewerNavigationGestureHasPhase = hasPhase
    }

    function continueViewerNavigationGesture(hasPhase) {
        beginViewerNavigationGesture(false, hasPhase)
        if (!hasPhase) {
            gestureEndTimer.restart()
        }
    }

    function endViewerNavigationGesture(clearCommitted) {
        logViewerGesture("gesture end clearCommitted=" + (clearCommitted === undefined ? true : clearCommitted))
        gestureEndTimer.stop()
        viewer.viewerNavigationGestureActive = false
        viewer.viewerNavigationGestureHasPhase = false
        if (clearCommitted === undefined || clearCommitted) {
            viewer.viewerNavigationGestureCommitted = false
            viewer.viewerNavigationSuppressMomentum = false
            residualQuietTimer.stop()
        }
        viewer.viewerNavigationLastTime = 0
    }

    function startViewerNavigationResidualSuppression(reason) {
        if (!viewer.viewerNavigationSuppressMomentum) {
            logViewerGesture("residual suppression begin reason=" + reason)
        }
        viewer.viewerNavigationSuppressMomentum = true
        residualQuietTimer.restart()
    }

    function clearViewerNavigationResidualSuppression(reason) {
        if (!viewer.viewerNavigationSuppressMomentum) {
            return
        }

        if (navigationAnimation.running || viewer.viewerNavigationCommitAfterAnimation) {
            residualQuietTimer.restart()
            return
        }

        logViewerGesture("residual suppression clear reason=" + reason)
        viewer.viewerNavigationSuppressMomentum = false
        if (!viewer.viewerNavigationGestureActive) {
            viewer.viewerNavigationGestureCommitted = false
        }
    }

    function hiddenNavigationOffset(overdrag) {
        return Math.min(overdrag * 0.35, viewerNavigationOverdragThreshold * 0.5)
    }

    function updateViewerNavigationTargetSource() {
        if (viewer.viewerNavigationTargetIndex === -1 || !viewer.decodeModel) {
            viewer.viewerNavigationTargetSource = ""
            viewer.viewerNavigationTargetSourceLevel = -1
            return
        }

        const sourceIndex = viewer.sourceIndexForViewIndex(
                              viewer.viewerNavigationTargetIndex)
        if (typeof viewer.decodeModel.preparedViewerImageUrlForIndex === "function") {
            viewer.viewerNavigationTargetSource =
                    viewer.decodeModel.preparedViewerImageUrlForIndex(
                        sourceIndex,
                        viewer.viewerNavigationTargetRequestWidth,
                        viewer.viewerNavigationTargetRequestHeight)
            viewer.viewerNavigationTargetSourceLevel =
                    viewer.viewerNavigationTargetSource !== ""
                    ? (viewer.viewerNavigationTargetRequestWidth > 0 &&
                       viewer.viewerNavigationTargetRequestHeight > 0 ? 1 : 2)
                    : -1
        }
        else {
            viewer.viewerNavigationTargetSource =
                    viewer.decodeModel.bestViewerImageUrlForIndex(sourceIndex)
            viewer.viewerNavigationTargetSourceLevel =
                    viewer.viewerNavigationTargetSource !== "" ? 0 : -1
        }
    }

    function prepareViewerNavigationTarget() {
        if (viewer.viewerNavigationTargetIndex === -1 || !viewer.decodeModel) {
            return
        }

        const fitRequest = imageViewport.zoomFitView && !viewer.sphericViewerMode
        viewer.viewerNavigationTargetRequestWidth = fitRequest
                ? Math.max(1, Math.ceil(viewer.width * viewer.devicePixelRatio)) : -1
        viewer.viewerNavigationTargetRequestHeight = fitRequest
                ? Math.max(1, Math.ceil(viewer.height * viewer.devicePixelRatio)) : -1
        const sourceIndex = viewer.sourceIndexForViewIndex(
                              viewer.viewerNavigationTargetIndex)
        if (typeof viewer.decodeModel.requestViewerAt === "function") {
            viewer.decodeModel.requestViewerAt(
                        sourceIndex,
                        viewer.viewerNavigationTargetRequestWidth,
                        viewer.viewerNavigationTargetRequestHeight)
        }
        updateViewerNavigationTargetSource()
    }

    function beginViewerNavigation(direction) {
        let currentIndex = viewer.sourceMasonry.view.currentIndex
        let targetIndex = viewer.sourceMasonry.view.nextImageIndex(direction > 0, false)

        viewer.viewerNavigationLastAdoptedOriginalSize = Qt.size(0, 0)
        viewer.viewerNavigationActive = true
        viewer.viewerNavigationDirection = direction
        viewer.viewerNavigationTargetIndex = targetIndex !== currentIndex ? targetIndex : -1
        viewer.viewerNavigationTargetPath = viewer.pathForIndex(viewer.viewerNavigationTargetIndex)
        viewer.viewerNavigationTargetSource = ""
        viewer.viewerNavigationTargetSourceLevel = -1
        imageViewport.cancelWheelPan()
        prepareViewerNavigationTarget()
        logViewerGesture("navigation begin direction=" + viewerGestureDirectionName(direction) +
                         " current=" + currentIndex + " target=" + viewer.viewerNavigationTargetIndex +
                         " sourceReady=" + (viewer.viewerNavigationTargetSource !== ""))
    }

    function applyViewerNavigationDelta(deltaX) {
        if (Math.abs(deltaX) < 0.1) {
            return
        }

        let direction = deltaX < 0 ? 1 : -1
        if (!viewer.viewerNavigationActive || viewer.viewerNavigationDirection !== direction && !viewer.viewerNavigationRevealed) {
            resetViewerNavigation("new-direction direction=" + viewerGestureDirectionName(direction))
            beginViewerNavigation(direction)
        }

        let signedDelta = -viewer.viewerNavigationDirection * deltaX
        viewer.viewerNavigationOverdrag = Math.max(0, viewer.viewerNavigationOverdrag + signedDelta)

        if (viewer.viewerNavigationOverdrag <= 0.1) {
            resetViewerNavigation("overdrag-cleared deltaX=" + viewerGestureNumber(deltaX))
            return
        }

        if (viewer.viewerNavigationTargetIndex === -1) {
            viewer.viewerNavigationRevealed = false
            viewer.viewerNavigationOffsetX = -viewer.viewerNavigationDirection *
                    Math.min(viewer.viewerNavigationOverdrag * 0.25, viewerNavigationOverdragThreshold * 0.6)
            logViewerGesture("edge resistance noTarget deltaX=" + viewerGestureNumber(deltaX))
            return
        }

        if (viewer.viewerNavigationOverdrag < viewerNavigationOverdragThreshold) {
            viewer.viewerNavigationRevealed = false
            viewer.viewerNavigationOffsetX = viewer.viewerNavigationDirection < 0 ?
                    viewer.viewerNavigationOverdrag : -viewer.viewerNavigationDirection * hiddenNavigationOffset(viewer.viewerNavigationOverdrag)
            logViewerGesture("hidden overdrag deltaX=" + viewerGestureNumber(deltaX) +
                             " threshold=" + viewerGestureNumber(viewerNavigationOverdragThreshold))
            return
        }

        if (!viewer.viewerNavigationRevealed) {
            logViewerGesture("neighbor reveal")
        }
        viewer.viewerNavigationRevealed = true
        let visibleDistance = viewer.viewerNavigationDirection < 0 ?
                viewer.viewerNavigationOverdrag :
                hiddenNavigationOffset(viewerNavigationOverdragThreshold) +
                viewer.viewerNavigationOverdrag - viewerNavigationOverdragThreshold
        let maxOffset = viewer.viewerNavigationDirection < 0 ? viewerNavigationTargetTravelDistance : viewer.width
        viewer.viewerNavigationOffsetX = -viewer.viewerNavigationDirection * Math.min(visibleDistance, maxOffset)
        logViewerGesture("drag progress deltaX=" + viewerGestureNumber(deltaX) +
                         " visibleDistance=" + viewerGestureNumber(visibleDistance))
    }

    function viewerNavigationFinishAnimationDuration(targetOffset, shouldCommit) {
        if (!shouldCommit) {
            return viewer.animationDuration
        }

        let fullDistance = viewer.viewerNavigationDirection < 0 ? viewerNavigationTargetTravelDistance : viewer.width
        let remainingDistance = Math.abs(targetOffset - viewer.viewerNavigationOffsetX)
        let remainingRatio = Math.min(1, remainingDistance / Math.max(1, fullDistance))
        return viewer.animationDuration * (1 + remainingRatio)
    }

    function finishViewerNavigation() {
        navigationFinishTimer.stop()
        if (!viewer.viewerNavigationActive) {
            logViewerGesture("finish without active navigation")
            imageViewport.finishWheelPan()
            return
        }

        let signedVelocity = -viewer.viewerNavigationDirection * viewer.viewerNavigationVelocityX
        let signedOffset = -viewer.viewerNavigationDirection * viewer.viewerNavigationOffsetX
        let gestureDistance = viewer.viewerNavigationOverdrag
        let shouldCommit = viewer.viewerNavigationTargetIndex !== -1 && viewer.viewerNavigationRevealed &&
                (gestureDistance >= viewerNavigationCommitThreshold || signedVelocity > 900)

        if (shouldCommit) {
            viewer.viewerNavigationGestureCommitted = true
        }
        let targetOffset = shouldCommit ?
                (viewer.viewerNavigationDirection < 0 ? viewerNavigationTargetTravelDistance : -viewer.viewerNavigationDirection * viewer.width) : 0
        let finishDuration = viewerNavigationFinishAnimationDuration(targetOffset, shouldCommit)
        logViewerGesture("finish shouldCommit=" + shouldCommit +
                         " signedOffset=" + viewerGestureNumber(signedOffset) +
                         " gestureDistance=" + viewerGestureNumber(gestureDistance) +
                         " signedVelocity=" + viewerGestureNumber(signedVelocity) +
                         " threshold=" + viewerGestureNumber(viewerNavigationCommitThreshold) +
                         " duration=" + viewerGestureNumber(finishDuration))
        viewer.viewerNavigationCommitAfterAnimation = shouldCommit
        navigationAnimation.to = targetOffset
        navigationAnimation.duration = finishDuration
        navigationAnimation.restart()
    }

    function commitViewerNavigation() {
        const targetIndex = viewer.viewerNavigationTargetIndex
        const targetImageX = viewerNavigationTargetFinalImageX
        const targetImageY = viewerNavigationTargetFinalImageY
        const targetSource = viewer.viewerNavigationTargetSource
        const targetSourceLevel = viewer.viewerNavigationTargetSourceLevel
        // QML value types read from a bound property can continue to reference
        // that property. resetViewerNavigation() changes its target index and
        // therefore its size to 0x0, so make an explicit value copy first.
        const targetOriginalSize = Qt.size(
                    viewerNavigationTargetOriginalSize.width,
                    viewerNavigationTargetOriginalSize.height)
        const targetIsValid = targetIndex !== -1
                && targetIndex !== viewer.sourceMasonry.view.currentIndex
        const canAdoptFitTransition = targetIsValid
                && imageViewport.zoomFitView
                && !viewer.sphericViewerMode
                && targetSourceLevel === 1
                && targetSource !== ""
                && targetOriginalSize.width > 1
                && targetOriginalSize.height > 1
                && neighborImage.source.toString()
                   === targetSource
                && neighborImage.status === Image.Ready
        logViewerGesture("commit target=" + targetIndex)
        viewer.viewerNavigationGestureCommitted = true
        startViewerNavigationResidualSuppression("commit")
        viewer.selectionHighlightAnimationSuppressed = true
        resetViewerNavigation("commit")
        if (!targetIsValid) {
            Qt.callLater(() => viewer.selectionHighlightAnimationSuppressed = false)
            return
        }

        // The swipe overlay is already showing a prepared Fit tier. Adopt it
        // before changing the model row so the normal current-index handler
        // cannot replace it with the masonry thumbnail during the handoff.
        // Native/free-zoom navigation retains the original current-index flow.
        if (canAdoptFitTransition) {
            viewer.viewerNavigationLastAdoptedOriginalSize = Qt.size(
                        targetOriginalSize.width, targetOriginalSize.height)
            imageViewport.setImage(targetSource, targetOriginalSize,
                                   targetIndex, targetSourceLevel)
        }
        if (!imageViewport.zoomFitView) {
            imageViewport.image.x = targetImageX
            imageViewport.image.y = targetImageY
        }
        viewer.sourceMasonry.setCurrentIndex(targetIndex)
        Qt.callLater(() => viewer.selectionHighlightAnimationSuppressed = false)
    }

    function finishViewerNavigationAnimationNow(reason) {
        if (!navigationAnimation.running && !viewer.viewerNavigationCommitAfterAnimation) {
            return
        }

        logViewerGesture("finish viewer.animation now reason=" + reason)
        navigationAnimation.stop()
        if (viewer.viewerNavigationCommitAfterAnimation) {
            commitViewerNavigation()
        }
        else {
            resetViewerNavigation("interrupted viewer.animation: " + reason)
            imageViewport.settlePan()
        }
    }

    function wheelDeltaPixels(pixelDelta, angleDelta) {
        if (pixelDelta !== 0) {
            return pixelDelta
        }

        return angleDelta / 120 * 80
    }

    function switchImageForLegacyWheel(angleDeltaY) {
        let nextIndex = -1
        let currentIndex = viewer.sourceMasonry.view.currentIndex
        if (angleDeltaY < 0) {
            nextIndex = viewer.sourceMasonry.moveInImageList(true, false)
        }
        else if (angleDeltaY > 0) {
            nextIndex = viewer.sourceMasonry.moveInImageList(false, false)
        }

        if (nextIndex !== -1 && nextIndex !== currentIndex) {
            logViewerGesture("legacy wheel switch angleDeltaY=" + angleDeltaY + " target=" + nextIndex)
        }
    }

    function isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                                      phase, hasPixelDelta, nativeMomentum,
                                      nativePhase, nativeMomentumPhase) {
        if (angleDeltaY === 0 || angleDeltaX !== 0 || nativeMomentum) {
            return false
        }

        if (!hasPixelDelta) {
            return true
        }

        let phaseFree = phase === ViewerWheelArea.NoScrollPhase &&
                nativePhase === 0 && nativeMomentumPhase === 0
        if (!phaseFree) {
            return false
        }

        return Math.abs(pixelDeltaY) > 0 && Math.abs(pixelDeltaX) < Math.abs(pixelDeltaY) * 0.2
    }

    function panZoomedImageFromWheel(deltaX, deltaY, recordVelocity) {
        if (imageViewport.zoomFitView) {
            return Qt.point(deltaX, deltaY)
        }

        wheelPanFinishTimer.stop()
        return imageViewport.panBy(deltaX, deltaY, recordVelocity)
    }

    function scheduleWheelPanFallbackFinish() {
        if (!imageViewport.zoomFitView) {
            wheelPanFinishTimer.restart()
        }
    }

    function logWheelEvent(pixelDeltaX, pixelDeltaY,
                           angleDeltaX, angleDeltaY, phase, modifiers,
                           buttons, hasPixelDelta, inverted, source,
                           deviceType, nativeMomentum, nativePhase,
                           nativeMomentumPhase) {
        logViewerGesture(
                    "wheel phase=" + viewerGesturePhaseName(phase)
                    + " px=(" + viewerGestureNumber(pixelDeltaX)
                    + "," + viewerGestureNumber(pixelDeltaY) + ")"
                    + " angle=(" + viewerGestureNumber(angleDeltaX)
                    + "," + viewerGestureNumber(angleDeltaY) + ")"
                    + " hasPixel=" + hasPixelDelta
                    + " inverted=" + inverted
                    + " source=" + source
                    + " device=" + deviceType
                    + " nativeMomentum=" + nativeMomentum
                    + " nativePhase=" + nativePhase
                    + " nativeMomentumPhase=" + nativeMomentumPhase
                    + " modifiers=" + modifiers
                    + " buttons=" + buttons)
    }

    function consumeMomentumState(nativeMomentum, effectivePhase,
                                  deltaX, deltaY) {
        if (nativeMomentum && !imageViewport.zoomFitView
                && !viewer.viewerNavigationActive
                && !viewer.viewerNavigationCommitAfterAnimation
                && !viewer.viewerNavigationGestureCommitted
                && !viewer.viewerNavigationSuppressMomentum) {
            panZoomedImageFromWheel(deltaX, deltaY, false)
            logViewerGesture("native momentum panned zoomed image")
            return true
        }
        if (viewer.viewerNavigationSuppressMomentum
                && !viewer.viewerNavigationGestureActive) {
            const physicalRestart = !nativeMomentum
                    && effectivePhase !== ViewerWheelArea.ScrollMomentum
                    && effectivePhase !== ViewerWheelArea.ScrollEnd
            if (physicalRestart) {
                clearViewerNavigationResidualSuppression(
                            "new physical scroll "
                            + viewerGesturePhaseName(effectivePhase))
            } else {
                residualQuietTimer.restart()
                logViewerGesture("wheel suppressed as residual tail")
                return true
            }
        }
        if (nativeMomentum && !viewer.viewerNavigationGestureActive) {
            startViewerNavigationResidualSuppression(
                        "stray native momentum")
            logViewerGesture("native momentum suppressed")
            return true
        }
        if (!nativeMomentum || !viewer.viewerNavigationGestureActive)
            return false
        if (viewer.viewerNavigationActive) {
            finishViewerNavigation()
            endViewerNavigationGesture(false)
            startViewerNavigationResidualSuppression("native momentum")
        } else if (!imageViewport.zoomFitView) {
            endViewerNavigationGesture(true)
            panZoomedImageFromWheel(deltaX, deltaY, false)
        } else {
            endViewerNavigationGesture(false)
            startViewerNavigationResidualSuppression("native momentum")
        }
        return true
    }

    function consumeWheelPhase(effectivePhase) {
        if (effectivePhase === ViewerWheelArea.ScrollBegin) {
            if (viewer.viewerNavigationCommitAfterAnimation
                    || navigationAnimation.running)
                finishViewerNavigationAnimationNow("new gesture begin")
            beginViewerNavigationGesture(true, true)
            if (!imageViewport.zoomFitView)
                imageViewport.beginWheelPan()
            navigationFinishTimer.stop()
            wheelPanFinishTimer.stop()
            return true
        }
        if (effectivePhase === ViewerWheelArea.ScrollEnd) {
            if (!viewer.viewerNavigationGestureActive) {
                logViewerGesture("duplicate end ignored")
                return true
            }
            const hadNavigation = viewer.viewerNavigationActive
            if (hadNavigation)
                finishViewerNavigation()
            else if (!imageViewport.zoomFitView)
                scheduleWheelPanFallbackFinish()
            endViewerNavigationGesture(false)
            if (hadNavigation || imageViewport.zoomFitView) {
                startViewerNavigationResidualSuppression("phase end")
                logViewerGesture("phase end suppressing following momentum")
            }
            return true
        }
        if (effectivePhase === ViewerWheelArea.ScrollMomentum
                && (viewer.viewerNavigationSuppressMomentum
                    || !viewer.viewerNavigationGestureActive)) {
            startViewerNavigationResidualSuppression("stray momentum")
            logViewerGesture("momentum suppressed")
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
        logViewerGesture("legacy vertical wheel path")
        if (navigationAnimation.running)
            finishViewerNavigationAnimationNow("legacy wheel")
        endViewerNavigationGesture(true)
        imageViewport.cancelWheelPan()
        switchImageForLegacyWheel(angleDeltaY)
        return true
    }

    function applyWheelIntent(deltaX, deltaY, phaseAware) {
        const horizontalIntent = viewer.viewerNavigationActive
                || Math.abs(deltaX) >= Math.abs(deltaY) * 0.6
        if (!horizontalIntent) {
            logViewerGesture("vertical wheel intent")
            if (!imageViewport.zoomFitView) {
                panZoomedImageFromWheel(0, deltaY, true)
                if (!phaseAware)
                    scheduleWheelPanFallbackFinish()
            }
            return
        }
        let remainingX = deltaX
        if (!viewer.viewerNavigationActive && !imageViewport.zoomFitView) {
            const leftover =
                    panZoomedImageFromWheel(remainingX, deltaY, true)
            remainingX = leftover.x
        }
        if (Math.abs(remainingX) < 0.1) {
            logViewerGesture("horizontal delta consumed")
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
            navigationFinishTimer.restart()
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
        logWheelEvent(
                    pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                    phase, modifiers, buttons, hasPixelDelta, inverted,
                    source, deviceType, nativeMomentum, nativePhase,
                    nativeMomentumPhase)
        const deltaX = wheelDeltaPixels(pixelDeltaX, angleDeltaX)
        const deltaY = wheelDeltaPixels(pixelDeltaY, angleDeltaY)
        if (consumeMomentumState(
                    nativeMomentum, effectivePhase, deltaX, deltaY)
                || consumeWheelPhase(effectivePhase))
            return
        if (viewer.viewerNavigationCommitAfterAnimation) {
            logViewerGesture(
                        "wheel ignored while commit animation is running")
            return
        }
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
