pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: root

    required property GalleryPanel panel
    required property GalleryPanelController controller
    required property GalleryViewportItem layoutItem
    required property GalleryViewport viewportItem
    required property GalleryPointerLayer pointerInput
    required property NumberAnimation scrollAnimation
    required property ParallelAnimation chromeGeometryAnimation
    required property Timer pathPlacementTimer
    required property Timer viewportTimer
    required property Timer cursorTimer
    required property Timer cursorAfterScrollTimer
    required property Timer densityTimer

    readonly property GalleryViewportItem galleryLayout: layoutItem
    readonly property GalleryViewport galleryViewport: viewportItem
    readonly property GalleryPointerLayer pointerLayer: pointerInput
    readonly property NumberAnimation panelScrollAnimation: scrollAnimation
    readonly property ParallelAnimation cursorChromeGeometryAnimation:
        chromeGeometryAnimation
    readonly property Timer pathViewportPlacementTimer: pathPlacementTimer
    readonly property Timer viewportUpdateTimer: viewportTimer
    readonly property Timer cursorCommitTimer: cursorTimer
    readonly property Timer cursorCommitAfterScrollTimer:
        cursorAfterScrollTimer
    readonly property Timer densityCommitTimer: densityTimer

    readonly property bool controllerReady: panel.controllerReady
    readonly property string presentationMode: panel.presentationMode
    readonly property var presentationDensities: panel.presentationDensities
    readonly property string mouseWheelMode: panel.mouseWheelMode
    readonly property var hostCapabilities: panel.hostCapabilities
    readonly property real devicePixelRatio: panel.devicePixelRatio
    readonly property bool scrollingMode: panel.scrollingMode
    readonly property bool scrollingStarted: panel.scrollingStarted
    readonly property bool densityAdjustmentEnabled:
        panel.densityAdjustmentEnabled

    property bool restoringScrollOffset: false
    property bool viewportUpdateEnsuresCursor: false
    property bool viewportUpdateSuppressAnimation: false
    property bool viewportUpdatePendingAfterScroll: false
    property bool pathViewportPlacementPending: false
    property bool pathViewportCatalogReady: true
    property bool presentationSwitchPending: false
    property int presentationStateUpdateDepth: 0
    property real presentationSwitchCursorViewportY: Number.NaN
    property int thumbnailPinchStartHeight: 0
    property bool densityViewportTransaction: false
    property bool suppressScrollAnimationPersistence: false
    property bool presentationLayoutHidden: false
    property bool presentationStateHidesLayout: false

    function densityChangeRequested(mode, density, finalChange) {
        panel.densityChangeRequested(mode, density, finalChange)
    }
    function consoleWheelRequested(x, y, angleDeltaY, modifiers) {
        panel.consoleWheelRequested(x, y, angleDeltaY, modifiers)
    }
    function consoleMouseButtonRequested(x, y, button, down, modifiers) {
        panel.consoleMouseButtonRequested(x, y, button, down, modifiers)
    }
    function traceBenchmarkStage(stage, metadata) {
        panel.traceBenchmarkStage(stage, metadata)
    }
    function publishMetadataVisibleRange() {
        panel.publishMetadataVisibleRange()
    }
    function resetGridPageLattice() {
        panel.resetGridPageLattice()
    }
    function resetMasonryPageSequence() {
        panel.resetMasonryPageSequence()
    }
    function cancelCursorChromeTransition() {
        panel.cancelCursorChromeTransition()
    }
    function ensureCurrentVisible(animate, direction) {
        panel.ensureCurrentVisible(animate, direction)
    }
    function resetCurrentItemCenter(index) {
        panel.resetCurrentItemCenter(index)
    }

    function nativePresentationMode() {
        switch (presentationMode) {
        case "columns": return GalleryViewportItem.Columns
        case "details": return GalleryViewportItem.Details
        case "grid": return GalleryViewportItem.Grid
        case "icons": return GalleryViewportItem.Icons
        default: return GalleryViewportItem.Masonry
        }
    }

    function minimumDensity() {
        if (presentationMode === "columns" || presentationMode === "details")
            return 22
        if (presentationMode === "grid")
            return 96
        if (presentationMode === "icons")
            return 18
        return 30
    }

    function maximumDensity() {
        if (presentationMode === "columns" || presentationMode === "details")
            return 72
        if (presentationMode === "grid")
            return 320
        if (presentationMode === "icons")
            return 256
        return 500
    }

    function noteDensityChanged(finalChange) {
        resetGridPageLattice()
        resetMasonryPageSequence()
        densityChangeRequested(presentationMode, galleryLayout.density,
                               Boolean(finalChange))
        if (finalChange)
            densityCommitTimer.stop()
        else
            densityCommitTimer.restart()
    }

    function changeDensity(change) {
        viewportUpdateTimer.stop()
        viewportUpdatePendingAfterScroll = false
        viewportUpdateEnsuresCursor = false
        viewportUpdateSuppressAnimation = false
        densityViewportTransaction = true
        try {
            change()
        } finally {
            densityViewportTransaction = false
        }
        if (controller) {
            controller.panelScrollOffset = galleryLayout.contentY
            controller.panelViewportCursorEntryId =
                    controller.cursorEntryId
        }
    }


    function keyboardAlignedContentY(value, direction) {
        const revealDirection = Number(direction)
        const target = Number(value)
        if (!Number.isFinite(target)
                || !Number.isFinite(revealDirection)
                || revealDirection === 0)
            return value

        const epsilon = 0.000001
        if (presentationMode === "columns") {
            const columnWidth = Number(galleryLayout.columnStride())
            if (!(columnWidth > 0))
                return value
            const coordinate = target / columnWidth
            const column = revealDirection < 0
                    ? Math.floor(coordinate + epsilon)
                    : Math.ceil(coordinate - epsilon)
            return column * columnWidth
        }

        if (presentationMode === "details") {
            const rowExtent = Math.max(1, Number(galleryLayout.density))
            const coordinate = target / rowExtent
            const row = revealDirection < 0
                    ? Math.floor(coordinate + epsilon)
                    : Math.ceil(coordinate - epsilon)
            // Details rows start at paddingTop, so a row's screen Y matches
            // row zero precisely when contentY is an integer row pitch.
            return row * rowExtent
        }
        return value
    }

    function animatePanelScrollTo(targetY, quickScroll, keyboardReveal,
                                  keyboardRevealDirection) {
        if (!keyboardReveal)
            cancelCursorChromeTransition()
        // An embedded session can queue a zero-delay offset restoration while
        // its initial catalog/layout settles.  Once the user scrolls, that
        // stale restore must not stop the original MasonryMode animation and
        // snap back to the pre-gesture offset.
        viewportUpdateTimer.stop()
        viewportUpdateEnsuresCursor = false
        viewportUpdateSuppressAnimation = false
        viewportUpdatePendingAfterScroll = false
        const viewportExtent = presentationMode === "columns"
                ? galleryLayout.width : galleryLayout.height
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - viewportExtent)
        let target = Number(targetY)
        if (keyboardReveal)
            target = keyboardAlignedContentY(target,
                                              keyboardRevealDirection)
        target = Math.max(0, Math.min(maximum, target))
        const compactRows = presentationMode === "columns"
                || presentationMode === "details"
        if (compactRows && keyboardReveal) {
            // Keyboard/path placement in compact rows remains an immediate
            // reveal. Wheel input, however, must use the same animation as
            // Masonry so every presentation has one GUI scrolling contract.
            setPanelContentY(target, true)
            return target
        }
        panelScrollAnimation.from = galleryLayout.contentY
        panelScrollAnimation.to = target
        panelScrollAnimation.duration = quickScroll ? 15 : 150
        panelScrollAnimation.restart()
        return target
    }

    function scrollBy(deltaY, quickScroll, keyboardReveal,
                      keyboardRevealDirection) {
        const plannedContentY = panelScrollAnimation.running
                ? panelScrollAnimation.to : galleryLayout.contentY
        return animatePanelScrollTo(plannedContentY + deltaY,
                                    quickScroll, keyboardReveal,
                                    keyboardRevealDirection)
    }

    function handlePanelMiddlePress(x, y, modifiers) {
        if (mouseWheelMode === "console") {
            consoleMouseButtonRequested(x, y, Qt.MiddleButton, true,
                                        modifiers)
            return
        }
        if (scrollingMode) {
            pointerLayer.endAutoScroll()
        } else {
            resetGridPageLattice()
            resetMasonryPageSequence()
            cancelCursorChromeTransition()
            // A wheel step may still be easing toward its endpoint. The
            // middle gesture owns contentY exclusively once it starts.
            panelScrollAnimation.stop()
            pointerLayer.startAutoScroll()
        }
    }

    function stepDensity(zoomIn) {
        if (!densityAdjustmentEnabled)
            return false
        const previousDensity = galleryLayout.density
        cancelCursorChromeTransition()
        changeDensity(function() {
            if (zoomIn)
                galleryLayout.zoomIn()
            else
                galleryLayout.zoomOut()
        })
        if (previousDensity === galleryLayout.density)
            return false
        noteDensityChanged(false)
        return true
    }

    function resetDensity(value) {
        if (!densityAdjustmentEnabled)
            return false
        const target = Math.min(maximumDensity(), Math.max(minimumDensity(),
            Number(value)))
        if (!Number.isFinite(target) || target === galleryLayout.density)
            return false
        cancelCursorChromeTransition()
        changeDensity(function() { galleryLayout.density = target })
        galleryLayout.reReadAndDecodeThumbnails()
        noteDensityChanged(true)
        return true
    }

    function handlePanelMiddleRelease(x, y, modifiers) {
        if (mouseWheelMode === "console") {
            consoleMouseButtonRequested(x, y, Qt.MiddleButton, false,
                                        modifiers)
            return
        }
        // A release after the pointer actually moved ends the gesture. A
        // stationary middle click deliberately leaves auto-scroll armed,
        // exactly like the original MasonryMode toggle.
        if (scrollingStarted)
            pointerLayer.endAutoScroll()
    }

    function handlePanelWheel(pixelDeltaY, angleDeltaY, modifiers,
                              pixelDeltaX, angleDeltaX) {
        const macPlatform = Qt.platform.os === "osx"
        const verticalDelta = macPlatform
                ? Number(pixelDeltaY || 0) : Number(angleDeltaY || 0)
        const horizontalDelta = macPlatform
                ? Number(pixelDeltaX || 0) : Number(angleDeltaX || 0)
        // Trackpads report a real horizontal axis. In Columns it is the
        // authoritative gesture; Y remains a fallback for mouse wheels that
        // have no horizontal wheel. Do not add both axes for diagonal input.
        const delta = presentationMode === "columns" && horizontalDelta !== 0
                ? horizontalDelta : verticalDelta
        if (delta === 0)
            return false
        if (modifiers & Qt.ControlModifier) {
            if (densityAdjustmentEnabled)
                stepDensity(delta > 0)
        } else {
            cancelCursorChromeTransition()
            // Columns consumes the vertical wheel/trackpad gesture as
            // horizontal movement through its column-major strip.
            scrollBy(-delta, macPlatform)
        }
        return true
    }

    onMouseWheelModeChanged: {
        if (mouseWheelMode !== "gui" && scrollingMode)
            pointerLayer.endAutoScroll()
    }

    onScrollingModeChanged: {
        if (!scrollingMode && controller) {
            controller.panelScrollOffset = galleryLayout.contentY
            controller.panelViewportCursorEntryId = controller.cursorEntryId
        }
    }

    function beginThumbnailPinch() {
        if (!densityAdjustmentEnabled)
            return false
        cancelCursorChromeTransition()
        thumbnailPinchStartHeight = galleryLayout.density
        return true
    }

    function updateThumbnailPinch(scale) {
        if (!densityAdjustmentEnabled)
            return false
        if (thumbnailPinchStartHeight <= 0)
            beginThumbnailPinch()
        changeDensity(function() {
            galleryLayout.density = Math.min(
                        maximumDensity(), Math.max(minimumDensity(),
                        thumbnailPinchStartHeight * scale))
        })
        noteDensityChanged(false)
        return true
    }

    function finishThumbnailPinch() {
        if (!densityAdjustmentEnabled) {
            thumbnailPinchStartHeight = 0
            return false
        }
        if (thumbnailPinchStartHeight > 0
                && thumbnailPinchStartHeight !== galleryLayout.density) {
            galleryLayout.reReadAndDecodeThumbnails()
            noteDensityChanged(true)
        }
        thumbnailPinchStartHeight = 0
        return true
    }

    function setPanelContentY(value, persist) {
        resetGridPageLattice()
        resetMasonryPageSequence()
        cancelCursorChromeTransition()
        suppressScrollAnimationPersistence = true
        panelScrollAnimation.stop()
        suppressScrollAnimationPersistence = false
        restoringScrollOffset = true
        galleryLayout.contentY = value
        restoringScrollOffset = false
        if (persist && controller) {
            controller.panelScrollOffset = galleryLayout.contentY
            controller.panelViewportCursorEntryId = controller.cursorEntryId
        }
    }

    function beginPresentationSwitch() {
        // Set this before the native presentationMode binding changes so the
        // very first rewrap cannot inherit an old BrickItem animation.
        if (!presentationSwitchPending && controllerReady
                && controller.currentIndex >= 0) {
            const geometry = galleryLayout.indexGeometry(
                        controller.currentIndex)
            presentationSwitchCursorViewportY =
                    geometry.width > 0 && geometry.height > 0
                    ? geometry.y - galleryLayout.contentY : Number.NaN
        }
        presentationSwitchPending = true
        viewportUpdateTimer.stop()
        viewportUpdatePendingAfterScroll = false
        viewportUpdateEnsuresCursor = false
        viewportUpdateSuppressAnimation = false
        suppressScrollAnimationPersistence = true
        panelScrollAnimation.stop()
        suppressScrollAnimationPersistence = false
        cursorChromeGeometryAnimation.stop()
        cancelCursorChromeTransition()
    }

    // Embedders may own presentation chrome (for example f4's Details
    // header) outside this component. Bracket the complete semantic update so
    // native mode, density, insets and the resulting anchored viewport size
    // produce one layout revision and one paintable state.
    function beginPresentationStateUpdate(switchingMode) {
        if (switchingMode) {
            beginPresentationSwitch()
            presentationStateHidesLayout = true
            presentationLayoutHidden = true
        }
        presentationStateUpdateDepth += 1
        galleryLayout.beginLayoutUpdate()
    }

    function persistCommittedViewport() {
        if (presentationStateUpdateDepth > 0 || presentationSwitchPending
                || pathViewportPlacementPending || !controllerReady)
            return false
        const geometry = galleryLayout.indexGeometry(controller.currentIndex)
        const horizontal = presentationMode === "columns"
        const leading = horizontal ? geometry.x : geometry.y
        const trailing = leading + (horizontal ? geometry.width
                                               : geometry.height)
        const viewportLeading = galleryLayout.contentY
        const viewportTrailing = viewportLeading
                + (horizontal ? galleryLayout.width : galleryLayout.height)
        const cursorVisible = geometry.width > 0 && geometry.height > 0
                && leading < viewportTrailing && trailing > viewportLeading
        // Component construction can finish before the first authoritative
        // cursor placement. Do not turn contentY=0 into a saved viewport for
        // an off-screen cursor: reconciliation must still reveal that cursor.
        if (!controller.panelViewportStateAvailable && !cursorVisible)
            return false
        // contentYChanged is emitted before densityChanged at the end of a
        // native fixed-layout commit. Cancel the restore queued by that first
        // signal and publish the final semantic viewport instead of letting
        // it snap back to the pre-zoom offset on the next event turn.
        viewportUpdateTimer.stop()
        viewportUpdatePendingAfterScroll = false
        controller.panelScrollOffset = galleryLayout.contentY
        controller.panelViewportCursorEntryId = controller.cursorEntryId
        return true
    }

    function densityLayoutCommitted() {
        persistCommittedViewport()
    }

    function completePresentationSwitch() {
        // This is called only from the closing edge of the same synchronous
        // layout transaction, so there is no stale timer invocation to guard
        // against. Enum bindings can settle after the native setter returns;
        // requiring that redundant comparison left the transaction latched.
        if (!presentationSwitchPending)
            return false
        presentationSwitchPending = false
        viewportUpdateTimer.stop()
        viewportUpdatePendingAfterScroll = false
        viewportUpdateEnsuresCursor = false
        viewportUpdateSuppressAnimation = false
        presentationSwitchCursorViewportY = Number.NaN
        panel.visualCursorIndex = controllerReady
                ? controller.currentIndex : -1
        panel.pendingVisualCursorIndex = -1
        resetCurrentItemCenter(panel.visualCursorIndex)
        return true
    }

    function endPresentationStateUpdate(publishVisibleRange) {
        galleryLayout.endLayoutUpdate()
        if (presentationStateUpdateDepth > 0)
            presentationStateUpdateDepth -= 1
        if (presentationStateUpdateDepth > 0)
            return
        // GalleryViewportItem computes target geometry and the final semantic
        // cursor-aligned offset while its delegate commit is deferred. Finish
        // the transaction synchronously; an event-loop timer here used to
        // expose and materialize an obsolete intermediate viewport.
        completePresentationSwitch()
        if (presentationStateHidesLayout) {
            presentationStateHidesLayout = false
            presentationLayoutHidden = false
        }
        persistCommittedViewport()
        // updateViewportIndexSets() emits visibleIndexesChanged whenever the
        // published range really changes. Its handler above is the one
        // authoritative metadata notification; sending it again here doubled
        // the synchronous QML/C++ work for every presentation switch.
    }

    function restoreScrollOffset() {
        if (!controllerReady || galleryLayout.contentHeight <= 0)
            return false
        const maximum = Math.max(0, galleryLayout.contentHeight
                                 - (presentationMode === "columns"
                                    ? galleryLayout.width
                                    : galleryLayout.height))
        const target = Math.max(
                    0, Math.min(maximum, controller.panelScrollOffset))
        setPanelContentY(target, false)
        return target > 0
    }

    function restoreScrollOrEnsureCursor() {
        if (!controllerReady)
            return
        const viewportCursor = controller.panelViewportCursorEntryId || ""
        if (viewportCursor !== "") {
            if (viewportCursor !== controller.cursorEntryId) {
                // The Gallery Loader is being recreated after the cursor moved
                // in another f4 presentation. Mirror standalone
                // loadSavedState(): reveal the authoritative cursor in the
                // first rendered frame, without making the old viewport
                // animate down to it.
                ensureCurrentVisible(false)
            } else {
                // Zero is a real saved viewport, not a sentinel for "missing".
                // In particular, a user may scroll to the top while keeping a
                // cursor farther down, then temporarily hide the panel. Do not
                // replace that deliberate viewport with a cursor reveal when
                // the host makes the same live panel visible again (or an
                // embedder has to recreate it).
                restoreScrollOffset()
            }
            return
        }
        if (!restoreScrollOffset()) {
            // First entry into Gallery has no saved viewport either. Initial
            // positioning is restoration, not user navigation, and therefore
            // must be instantaneous.
            ensureCurrentVisible(false)
        } else if (viewportCursor === "") {
            // Adopt legacy/programmatically supplied offsets so a later
            // cursor change while this Loader is absent can be detected.
            controller.panelViewportCursorEntryId = controller.cursorEntryId
        }
    }

    function centerCurrentForPathChange() {
        traceBenchmarkStage("placement.center.attempt", {})
        if (!pathViewportPlacementPending || !controllerReady) {
            traceBenchmarkStage("placement.center.result", {
                "success": false,
                "reason": !controllerReady
                          ? "missing-controller-backend" : "not-pending"
            })
            return false
        }
        if (galleryLayout.count === 0) {
            pathViewportPlacementTimer.stop()
            pathViewportPlacementPending = false
            traceBenchmarkStage("placement.center.result", {
                "success": true,
                "reason": "empty-catalog"
            })
            return true
        }
        const index = controller.currentIndex
        if (index < 0 || index >= galleryLayout.count
                || galleryLayout.height <= 0) {
            traceBenchmarkStage("placement.center.result", {
                "success": false,
                "reason": "waiting-for-index-or-viewport"
            })
            return false
        }
        const geometry = galleryLayout.indexGeometry(index)
        if (geometry.width <= 0 || geometry.height <= 0) {
            traceBenchmarkStage("placement.center.result", {
                "success": false,
                "reason": "waiting-for-geometry"
            })
            return false
        }
        const horizontal = presentationMode === "columns"
        const viewportExtent = horizontal
                ? galleryLayout.width : galleryLayout.height
        const itemCenter = horizontal
                ? geometry.x + geometry.width / 2
                : geometry.y + geometry.height / 2
        const contentExtent = galleryLayout.contentHeight
        const maximum = Math.max(0, contentExtent - viewportExtent)
        const target = Math.max(0, Math.min(
                    maximum, itemCenter - viewportExtent / 2))
        setPanelContentY(target, true)
        pathViewportPlacementTimer.stop()
        pathViewportPlacementPending = false
        panel.visualCursorIndex = index
        panel.pendingVisualCursorIndex = -1
        resetCurrentItemCenter(index)
        traceBenchmarkStage("placement.center.result", {
            "success": Math.abs(galleryLayout.contentY - target) <= 0.51,
            "reason": "centered",
            "requestedContentY": target,
            "appliedContentY": galleryLayout.contentY
        })
        return true
    }

    function restoreRememberedViewportForPathChange() {
        if (!pathViewportPlacementPending || !controllerReady
                || !pathViewportCatalogReady
                || !controller.panelViewportStateAvailable)
            return false
        if (galleryLayout.height <= 0)
            return false
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const target = Math.max(
                    0, Math.min(maximum, controller.panelScrollOffset))
        setPanelContentY(target, false)
        pathViewportPlacementTimer.stop()
        pathViewportPlacementPending = false
        panel.visualCursorIndex = controller.currentIndex
        panel.pendingVisualCursorIndex = -1
        resetCurrentItemCenter(panel.visualCursorIndex)
        traceBenchmarkStage("placement.center.result", {
            "success": true,
            "reason": "restored-path-viewport",
            "requestedContentY": target,
            "appliedContentY": galleryLayout.contentY
        })
        return true
    }

    function placeViewportForPathChange() {
        // Catalog/path signals may arrive inside the host's atomic
        // presentation transaction. Geometry is intentionally not committed
        // until endPresentationStateUpdate(), so placing against it here would
        // materialize an obsolete delegate window and then do the work again.
        if (presentationStateUpdateDepth > 0)
            return false
        if (!pathViewportCatalogReady)
            return false
        if (restoreRememberedViewportForPathChange())
            return true
        return centerCurrentForPathChange()
    }

    function schedulePathViewportPlacement(reason) {
        if (pathViewportPlacementPending) {
            traceBenchmarkStage("placement.timer.scheduled", {
                "reason": String(reason || "unspecified")
            })
            pathViewportPlacementTimer.restart()
        } else {
            traceBenchmarkStage("placement.timer.skipped", {
                "reason": String(reason || "not-pending")
            })
        }
    }


    function scheduleViewportUpdate(ensureCursor) {
        if (ensureCursor)
            viewportUpdateEnsuresCursor = true
        // Catalog metadata may settle while a wheel animation is in flight.
        // Defer the resulting restore/ensure pass until the animation has
        // persisted its destination; running it now would call
        // setPanelContentY(), stop the animation, and snap to the old offset.
        if (panelScrollAnimation.running) {
            viewportUpdatePendingAfterScroll = true
            return
        }
        viewportUpdateTimer.restart()
    }
}
