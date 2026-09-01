pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property Item viewer
    required property FlickableZoomable imageViewport
    required property GalleryViewerMotion motion

    function setPanelTransition(active) {
        if (!viewer.sourcePanel)
            return
        viewer.sourcePanel.viewerTransitionActive = active
        viewer.sourcePanel.viewerTransitionEntryId = active
                ? viewer.entryIdAt(viewer.presentedIndex) : ""
    }

    function captureTransitionTarget() {
        viewer.transitionSourceGeometry = Qt.rect(0, 0, 0, 0)
        viewer.transitionThumbnailSource = ""
        viewer.transitionHasGeometry = false
        // The presentation can optimistically show a committed swipe while
        // f4 validates the stable-ID cursor action.  Until that authoritative
        // cursor arrives, the panel still points at the old tile; fading is
        // safer than collapsing the new image into the wrong entry.
        if (viewer.session && viewer.presentedIndex !== viewer.session.currentIndex)
            return false
        if (!viewer.sourcePanel
                || typeof viewer.sourcePanel.currentItemImageGeometry !== "function"
                || typeof viewer.sourcePanel.currentItemImageSource !== "function")
            return false
        const geometry = viewer.sourcePanel.currentItemImageGeometry(root)
        const source = viewer.sourcePanel.currentItemImageSource()
        if (!viewer.validGeometry(geometry) || source.toString() === "")
            return false
        viewer.transitionSourceGeometry = geometry
        viewer.transitionThumbnailSource = source
        viewer.transitionHasGeometry = true
        return true
    }

    function beginOpen() {
        if (viewer.customContent)
            return
        if (viewer.session && viewer.presentedIndex < 0)
            viewer.presentedIndex = viewer.session.currentIndex
        viewer.refreshCurrentSource()
        viewer.requestImage()
        viewer.viewerContentVisible = true
        viewer.completingClose = false
        viewer.returningFromPinch = false
        viewer.pinchCloseActive = false
        viewer.pinchCloseFinishingCommit = false
        viewer.pinchCloseProgress = 0
        captureTransitionTarget()
        if (viewer.currentSourceValue.toString() === ""
                && viewer.transitionThumbnailSource.toString() !== "") {
            imageViewport.setImage(viewer.transitionThumbnailSource,
                                   viewer.currentOriginalSizeValue,
                                   viewer.presentedIndex, 0)
            viewer.currentSourceValue = viewer.transitionThumbnailSource
            viewer.currentSourceLevelValue = 0
            viewer.appliedPresentedIndex = viewer.presentedIndex
        }
        setPanelTransition(true)
        viewer.transitionProgress = 0
        motion.transitionAnimation.to = 1
        motion.transitionAnimation.duration = viewer.animationDuration
        motion.transitionFinalizeTimer.start()
        motion.transitionAnimation.restart()
        if (viewer.autoFocus)
            viewer.forceActiveFocus()
    }

    function finishOpen() {
        motion.transitionFinalizeTimer.stop()
        viewer.transitionProgress = 1
        setPanelTransition(false)
        viewer.transitionThumbnailSource = ""
        viewer.transitionHasGeometry = false
        imageViewport.zoomToFit(true)
    }

    function finishClose() {
        motion.transitionFinalizeTimer.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.transitionProgress = 0
        viewer.viewerContentVisible = false
        viewer.completingClose = false
        viewer.returningFromPinch = false
        viewer.pinchCloseActive = false
        viewer.pinchCloseFinishingCommit = false
        viewer.pinchCloseProgress = 0
        viewer.clearHeldKeys()
        setPanelTransition(false)
        if (viewer.session
                && typeof viewer.session.clearViewerPreviousState === "function")
            viewer.session.clearViewerPreviousState(false)
        viewer.closeCompleted()
        viewer.closeRequested()
    }

    function requestClose() {
        if (viewer.customContent || viewer.completingClose)
            return
        viewer.finishViewerNavigationAnimationNow()
        viewer.completingClose = true
        viewer.returningFromPinch = false
        viewer.pinchCloseActive = false
        viewer.pinchCloseFinishingCommit = false
        motion.pinchCloseProgressAnimation.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.clearHeldKeys()
        captureTransitionTarget()
        setPanelTransition(true)
        if (!imageViewport.zoomFitView)
            imageViewport.zoomToFit(true)
        motion.transitionAnimation.to = 0
        motion.transitionAnimation.duration = viewer.animationDuration
        motion.transitionFinalizeTimer.start()
        motion.transitionAnimation.restart()
    }

    function closeViewer() { requestClose() }

    function currentViewerImageGeometry() {
        const image = imageViewport.image
        if (image.width <= 1 || image.height <= 1)
            return Qt.rect(0, 0, 0, 0)
        return viewer.mapFromItem(imageViewport,
                                Qt.rect(image.x, image.y,
                                        image.width, image.height))
    }

    function beginPinchClose() {
        if (viewer.pinchCloseActive)
            return true
        if (viewer.customContent || viewer.completingClose || viewer.transitionProgress < 0.999)
            return false
        const startGeometry = currentViewerImageGeometry()
        if (!captureTransitionTarget())
            return false
        const targetGeometry = imageViewport.imageRectFittedInRect(
                    viewer.transitionSourceGeometry)
        if (!viewer.validGeometry(startGeometry) || !viewer.validGeometry(targetGeometry))
            return false
        motion.transitionFinalizeTimer.stop()
        motion.transitionAnimation.stop()
        motion.pinchCloseProgressAnimation.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.pinchCloseStartGeometry = startGeometry
        viewer.pinchCloseTargetGeometry = targetGeometry
        viewer.pinchCloseProgress = 0
        viewer.pinchCloseFinishingCommit = false
        viewer.pinchCloseActive = true
        setPanelTransition(true)
        imageViewport.setImageRect(startGeometry)
        return true
    }

    function cancelPinchCloseDuringGesture() {
        motion.pinchCloseProgressAnimation.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.pinchCloseActive = false
        viewer.pinchCloseFinishingCommit = false
        viewer.pinchCloseProgress = 0
        setPanelTransition(false)
    }

    function completePinchCloseReturn() {
        motion.pinchCloseProgressAnimation.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.pinchCloseActive = false
        viewer.pinchCloseFinishingCommit = false
        viewer.pinchCloseProgress = 0
        setPanelTransition(false)
        imageViewport.zoomToFit()
    }

    function applyPinchCloseProgress() {
        if (!viewer.pinchCloseActive || !viewer.validGeometry(viewer.pinchCloseStartGeometry)
                || !viewer.validGeometry(viewer.pinchCloseTargetGeometry))
            return
        // This is ViewerMode's original OutSine geometry interpolation.  The
        // viewport stays full-size; only the image rectangle moves.  Keeping
        // those two layers separate avoids the frame-resize jump/flicker.
        const eased = Math.sin(Math.max(0, Math.min(1, viewer.pinchCloseProgress))
                               * Math.PI / 2)
        imageViewport.setImageRect(Qt.rect(
            viewer.lerp(viewer.pinchCloseStartGeometry.x, viewer.pinchCloseTargetGeometry.x, eased),
            viewer.lerp(viewer.pinchCloseStartGeometry.y, viewer.pinchCloseTargetGeometry.y, eased),
            viewer.lerp(viewer.pinchCloseStartGeometry.width,
                 viewer.pinchCloseTargetGeometry.width, eased),
            viewer.lerp(viewer.pinchCloseStartGeometry.height,
                 viewer.pinchCloseTargetGeometry.height, eased)))
    }

    function updatePinchClose(progress) {
        if (viewer.customContent || viewer.completingClose || viewer.pinchCloseFinishingCommit)
            return
        const clamped = Math.max(0, Math.min(1, progress))
        if (clamped <= 0) {
            if (viewer.pinchCloseActive)
                cancelPinchCloseDuringGesture()
            return
        }
        if (!beginPinchClose())
            return
        motion.pinchCloseProgressAnimation.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.pinchCloseProgress = clamped
    }

    function finishPinchClose(commit) {
        if (!viewer.pinchCloseActive)
            return
        const targetGeometry = imageViewport.imageRectFittedInRect(
                    viewer.transitionSourceGeometry)
        if (commit && viewer.validGeometry(targetGeometry)) {
            viewer.pinchCloseTargetGeometry = targetGeometry
            viewer.pinchCloseFinishingCommit = true
            if (viewer.pinchCloseProgress >= 0.999) {
                viewer.pinchCloseProgress = 1
                completePinchCloseCommit()
                return
            }
            motion.pinchCloseProgressAnimation.to = 1
            motion.pinchCloseProgressAnimation.duration = viewer.animationDuration
            motion.pinchCloseProgressAnimation.restart()
            motion.pinchCloseFinalizeTimer.start()
            return
        }
        completePinchCloseReturn()
    }

    function completePinchCloseCommit() {
        motion.pinchCloseProgressAnimation.stop()
        motion.pinchCloseFinalizeTimer.stop()
        viewer.completingClose = true
        viewer.pinchCloseActive = false
        viewer.pinchCloseFinishingCommit = false
        viewer.transitionProgress = 0
        finishClose()
    }

    function completeTransition() {
        // A Qt Quick animation can be suspended with its window or interrupted
        // by a render-loop/exposure change. Never leave the reusable viewer at
        // a fractional frame: the panel would stay masked on open, and an
        // embedder would never receive viewer.closeCompleted() on close. The timer is
        // a terminal-state guard only; the normal NumberAnimation path still
        // supplies every visible frame and reaches this function first.
        motion.transitionFinalizeTimer.stop()
        motion.transitionAnimation.stop()
        if (viewer.completingClose) {
            viewer.transitionProgress = 0
            finishClose()
        } else if (viewer.returningFromPinch) {
            viewer.returningFromPinch = false
            setPanelTransition(false)
            finishOpen()
        } else {
            viewer.transitionProgress = 1
            finishOpen()
        }
    }


}
