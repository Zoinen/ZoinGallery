pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: root

    required property GalleryPanel panel
    required property GalleryPanelController controller
    required property GalleryViewportItem layoutItem
    required property NumberAnimation scrollAnimation
    required property ParallelAnimation chromeAnimation
    required property Timer cursorTimer
    required property Timer cursorAfterScrollTimer

    readonly property GalleryViewportItem galleryLayout: layoutItem
    readonly property NumberAnimation panelScrollAnimation: scrollAnimation
    readonly property ParallelAnimation cursorChromeGeometryAnimation:
        chromeAnimation
    readonly property Timer cursorCommitTimer: cursorTimer
    readonly property Timer cursorCommitAfterScrollTimer:
        cursorAfterScrollTimer

    readonly property bool controllerReady: panel.controllerReady
    readonly property string presentationMode: panel.presentationMode
    readonly property bool cursorChromeTransitionActive:
        panel.cursorChromeTransitionActive
    readonly property bool suppressScrollAnimationPersistence:
        panel.suppressScrollAnimationPersistence

    function sourceIndex(viewIndex) {
        return panel.sourceIndex(viewIndex)
    }
    function openRequested(entryId, index, isImage, autoRepeat) {
        panel.openRequested(entryId, index, isImage, autoRepeat)
    }
    function coordinateVisualCursor(targetIndex, previousIndex) {
        panel.coordinateVisualCursor(targetIndex, previousIndex)
    }
    function cancelCursorChromeTransition() {
        panel.cancelCursorChromeTransition()
    }
    function moveCursorWithSelection(index, togglePrevious,
                                     preserveHorizontalAnchor,
                                     deferCursorCommit,
                                     preserveVerticalAnchor,
                                     keyboardRevealDirection) {
        panel.selectionController.moveCursorWithSelection(
                    index, togglePrevious, preserveHorizontalAnchor,
                    deferCursorCommit, preserveVerticalAnchor,
                    keyboardRevealDirection)
    }
    function animatePanelScrollTo(targetY, quickScroll, keyboardReveal,
                                  keyboardRevealDirection) {
        return panel.viewportController.animatePanelScrollTo(
                    targetY, quickScroll, keyboardReveal,
                    keyboardRevealDirection)
    }
    function scrollBy(deltaY, quickScroll, keyboardReveal,
                      keyboardRevealDirection) {
        return panel.viewportController.scrollBy(
                    deltaY, quickScroll, keyboardReveal,
                    keyboardRevealDirection)
    }
    function keyboardAlignedContentY(value, direction) {
        return panel.viewportController.keyboardAlignedContentY(
                    value, direction)
    }
    function setPanelContentY(value, persist) {
        panel.viewportController.setPanelContentY(value, persist)
    }

    property bool localCursorNavigation: false
    property bool cursorCommitPending: false
    property bool cursorCommitAfterScroll: false
    property bool navigationKeyHeld: false
    property real currentItemCenterX: -1
    property real currentItemCenterY: -1
    // Grid paging owns a row lattice independent from rendered animation
    // frames. Keep the phase captured by the first Page key so a terminal
    // contentHeight clamp can return to the same fractional row lattice.
    property real gridPageAnchorPhase: -1
    property real gridPageAnchorStride: 0
    property real gridPageAnchorPaddingTop: 0
    // Variable-height Masonry rows cannot use a numeric stride. Preserve one
    // actual row top in viewport coordinates and retain the visited page nodes
    // so PageDown followed by PageUp is exactly reversible even when adjacent
    // rows have very different heights.
    property real masonryPageRowViewportY: Number.NaN
    property int masonryPageOrdinal: 0
    property var masonryPageNodes: ({})
    // Only PageUp/PageDown destinations depend on the current native row-band
    // revision. Ordinary wheel and pointer reveal animations remain valid
    // across asynchronous metadata rewraps and must not be stopped by them.
    property bool masonryPageScrollActive: false

    function selectIndex(viewIndex, openItem, deferCursorCommit, autoRepeat) {
        if (!controllerReady || viewIndex < 0
                || viewIndex >= galleryLayout.count)
            return
        if (openItem) {
            controller.activateIndex(viewIndex)
            openRequested(controller.entryIdAt(viewIndex),
                          sourceIndex(viewIndex),
                          controller.isImageAt(viewIndex),
                          Boolean(autoRepeat))
        } else {
            // The embedding host carries activation together with this stable
            // cursor intent when the target panel is inactive. Keeping it as
            // one action prevents an intermediate authoritative frame from
            // highlighting this panel's previous cursor.
            controller.requestCursor(viewIndex, Boolean(deferCursorCommit))
            cursorCommitPending = Boolean(deferCursorCommit)
            if (cursorCommitPending) {
                cursorCommitTimer.restart()
            } else {
                cursorCommitAfterScroll = false
                cursorCommitAfterScrollTimer.stop()
                cursorCommitTimer.stop()
            }
        }
    }

    function commitPendingCursor() {
        cursorCommitAfterScroll = false
        cursorCommitAfterScrollTimer.stop()
        if (!cursorCommitPending || !controllerReady
                || controller.currentIndex < 0)
            return
        cursorCommitPending = false
        cursorCommitTimer.stop()
        controller.commitPendingCursor()
    }

    function refreshPendingCursorCommit() {
        if (!cursorCommitPending || !controllerReady
                || controller.currentIndex < 0)
            return
        // A held key can legitimately produce a no-op repeat while the
        // masonry layout is between rows or after the cursor reaches an edge.
        // Keep both the local and host-owned lost-release watchdogs alive so
        // neither one commits a full f4 scene while the key is still down.
        cursorCommitTimer.restart()
        controller.requestCursor(controller.currentIndex, true)
    }

    function resetCurrentItemCenterX(index) {
        if (!controllerReady || index < 0 || index >= galleryLayout.count) {
            currentItemCenterX = -1
            return
        }
        const geometry = galleryLayout.indexGeometry(index)
        currentItemCenterX = geometry.width > 0
                ? geometry.x + geometry.width / 2 : -1
    }

    function resetCurrentItemCenterY(index) {
        if (!controllerReady || index < 0 || index >= galleryLayout.count) {
            currentItemCenterY = -1
            return
        }
        const geometry = galleryLayout.indexGeometry(index)
        if (geometry.width <= 0 || geometry.height <= 0) {
            currentItemCenterY = -1
            return
        }
        const plannedContentY = panelScrollAnimation.running
                ? panelScrollAnimation.to : galleryLayout.contentY
        currentItemCenterY = geometry.y + geometry.height / 2
                - plannedContentY
    }

    function resetCurrentItemCenter(index) {
        resetCurrentItemCenterX(index)
        resetCurrentItemCenterY(index)
    }


    // Preserve the original MasonryMode navigation contract. Vertical moves
    // retain their X anchor and make one native indexAt() query immediately
    // above or below the current tile; the old embedded implementation made
    // one QML-to-C++ indexGeometry() call for every catalog entry per repeat.
    function verticalIndex(direction) {
        if (!controllerReady || galleryLayout.currentIndex < 0)
            return -1
        const current = galleryLayout.indexGeometry(galleryLayout.currentIndex)
        if (current.width <= 0 || current.height <= 0)
            return -1
        if (currentItemCenterX < 0)
            currentItemCenterX = current.x + current.width / 2
        const adjacentY = direction < 0
                ? current.y - 2
                : current.y + current.height + 2
        let adjacent = galleryLayout.indexAt(currentItemCenterX, adjacentY)
        if (adjacent < 0 && galleryLayout.listView)
            adjacent = galleryLayout.indexAt(0, adjacentY)
        return adjacent
    }

    function navigationDirectionForKey(key) {
        if (key === Qt.Key_Left)
            return GalleryViewportItem.NavigateLeft
        if (key === Qt.Key_Right)
            return GalleryViewportItem.NavigateRight
        if (key === Qt.Key_Up)
            return GalleryViewportItem.NavigateUp
        return GalleryViewportItem.NavigateDown
    }

    function navigationTargetForKey(key, page) {
        if (!controllerReady || controller.currentIndex < 0)
            return -1
        // Masonry's vertical navigation deliberately retains the X center
        // chosen by the last pointer/horizontal move.  A short intervening
        // row must not replace that anchor with its sole tile's center: the
        // following row is still probed at the user's original column. Fixed
        // modes own their navigation in GalleryViewportItem and bypass this branch.
        if (!page
                && galleryLayout.presentationMode === GalleryViewportItem.Masonry
                && (key === Qt.Key_Up || key === Qt.Key_Down)) {
            return root.verticalIndex(key === Qt.Key_Up ? -1 : 1)
        }
        const result = galleryLayout.navigationTarget(
                         controller.currentIndex,
                         navigationDirectionForKey(key), Boolean(page))
        if (!result)
            return -1
        const nextTop = Number(result.windowTopIndex)
        // Columns is a continuous horizontal strip. moveCursor() calls
        // ensureCurrentVisible(), which leaves an already-visible target
        // untouched and shifts by one column only when the cursor crosses a
        // viewport edge. Pre-setting windowTopIndex here made first->second
        // column navigation scroll even in a three-column viewport.
        if (galleryLayout.presentationMode !== GalleryViewportItem.Columns
                && nextTop >= 0
                && nextTop !== galleryLayout.windowTopIndex)
            galleryLayout.windowTopIndex = nextTop
        return Number(result.targetIndex)
    }

    function moveCursor(index, preserveSelectionAnchor,
                        preserveHorizontalAnchor, deferCursorCommit,
                        preserveVerticalAnchor, keyboardRevealDirection) {
        if (!controllerReady || galleryLayout.count === 0)
            return
        const bounded = Math.max(0, Math.min(galleryLayout.count - 1, index))
        const previousVisualIndex = panel.visualCursorIndex >= 0
                ? panel.visualCursorIndex : controller.currentIndex
        if (!preserveSelectionAnchor)
            panel.selectionAnchorIndex = bounded
        localCursorNavigation = true
        selectIndex(bounded, false, deferCursorCommit)
        localCursorNavigation = false
        if (!preserveHorizontalAnchor)
            resetCurrentItemCenterX(bounded)
        // Original MasonryMode keeps both viewport-relative anchors for an
        // interior PageUp/PageDown. Every ordinary move (including Up/Down,
        // which preserves only X) reveals the item and adopts its resulting Y.
        if (!preserveVerticalAnchor) {
            ensureCurrentVisible(undefined, keyboardRevealDirection)
            resetCurrentItemCenterY(bounded)
        }
        coordinateVisualCursor(bounded, previousVisualIndex)
    }


    function commitCursorAfterNavigation() {
        if (!cursorCommitPending)
            return
        if (panelScrollAnimation.running
                || cursorChromeGeometryAnimation.running) {
            cursorCommitAfterScroll = true
            cursorCommitAfterScrollTimer.restart()
        } else {
            commitPendingCursor()
        }
    }

    function resetGridPageLattice() {
        gridPageAnchorPhase = -1
        gridPageAnchorStride = 0
        gridPageAnchorPaddingTop = 0
    }

    function resetMasonryPageSequence() {
        masonryPageRowViewportY = Number.NaN
        masonryPageOrdinal = 0
        masonryPageNodes = ({})
        masonryPageScrollActive = false
    }

    function invalidateMasonryPageGeometry() {
        const stalePageScroll = masonryPageScrollActive
        resetMasonryPageSequence()
        if (galleryLayout.presentationMode !== GalleryViewportItem.Masonry
                || !stalePageScroll
                || (!panelScrollAnimation.running
                    && !cursorChromeTransitionActive))
            return
        // Never let an old target keep writing contentY after the row bands or
        // viewport extent which defined it have changed.
        panel.suppressScrollAnimationPersistence = true
        panelScrollAnimation.stop()
        panel.suppressScrollAnimationPersistence = false
        cancelCursorChromeTransition()
    }

    function masonryPageNode(ordinal) {
        if (!masonryPageNodes)
            return null
        const node = masonryPageNodes[String(ordinal)]
        return node === undefined ? null : node
    }

    function storeMasonryPageNode(ordinal, node) {
        if (!masonryPageNodes)
            masonryPageNodes = ({})
        masonryPageNodes[String(ordinal)] = node
    }

    function positiveModulo(value, divisor) {
        if (divisor <= 0)
            return 0
        return ((value % divisor) + divisor) % divisor
    }

    function ensureGridPageLattice(plannedContentY, stride, paddingTop) {
        if (gridPageAnchorPhase < 0
                || Math.abs(gridPageAnchorStride - stride) > 0.001
                || Math.abs(gridPageAnchorPaddingTop - paddingTop) > 0.001) {
            gridPageAnchorStride = stride
            gridPageAnchorPaddingTop = paddingTop
            gridPageAnchorPhase = positiveModulo(
                        plannedContentY - paddingTop, stride)
        }
    }

    function navigateGridViewportPage(direction, togglePrevious,
                                      deferCursorCommit) {
        if (!controllerReady || galleryLayout.count <= 0
                || controller.currentIndex < 0 || galleryLayout.height <= 0)
            return -1

        if (currentItemCenterX < 0 || currentItemCenterY < 0)
            resetCurrentItemCenter(controller.currentIndex)

        const stride = Math.max(1, Number(galleryLayout.density))
        const paddingTop = Number(galleryLayout.paddingTop) || 0
        const paddingBottom = Number(galleryLayout.paddingBottom) || 0
        const usableHeight = Math.max(
                    1, galleryLayout.height - paddingTop - paddingBottom)
        // Preserve the established 7/8-page pacing, but express it as a whole
        // number of native Grid rows. Round down so a fractional row never
        // makes one page direction cross an extra row boundary.
        const rowsPerPage = Math.max(
                    1, Math.floor((usableHeight * 7 / 8) / stride))
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const rawPlannedContentY = panelScrollAnimation.running
                ? Number(panelScrollAnimation.to) : galleryLayout.contentY
        const plannedContentY = Math.max(
                    0, Math.min(maximum, rawPlannedContentY))
        const currentGeometry = galleryLayout.indexGeometry(
                    controller.currentIndex)
        if (currentGeometry.width > 0 && currentGeometry.height > 0) {
            // Grid width changes can alter both column count and cell centers
            // without changing the current index. Always derive the Page
            // anchor from live native geometry and the planned viewport.
            currentItemCenterX = currentGeometry.x
                    + currentGeometry.width / 2
            currentItemCenterY = currentGeometry.y
                    + currentGeometry.height / 2 - plannedContentY
        }
        ensureGridPageLattice(plannedContentY, stride, paddingTop)

        const latticeOrigin = paddingTop + gridPageAnchorPhase
        const epsilon = 0.01
        let destination = plannedContentY
        const latticeCoordinate = (plannedContentY - latticeOrigin) / stride
        const nearestLatticeY = latticeOrigin
                + Math.round(latticeCoordinate) * stride
        const offLattice = Math.abs(plannedContentY - nearestLatticeY) > epsilon

        // Exact content bounds can be off the row lattice. On the first Page
        // away from such a terminal clamp, return to the last aligned row;
        // subsequent pages again use the full integer-row stride with no drift.
        if (offLattice && direction < 0
                && plannedContentY >= maximum - epsilon) {
            const alignedBase = latticeOrigin + Math.floor(
                        (maximum - latticeOrigin + epsilon) / stride) * stride
            destination = alignedBase - rowsPerPage * stride
        } else if (offLattice && direction > 0
                   && plannedContentY <= epsilon) {
            const alignedBase = latticeOrigin + Math.ceil(
                        (0 - latticeOrigin - epsilon) / stride) * stride
            destination = alignedBase + rowsPerPage * stride
        } else {
            destination = plannedContentY
                    + direction * rowsPerPage * stride
        }
        destination = Math.max(0, Math.min(maximum, destination))

        const canvasWidth = Math.max(
                    1, galleryLayout.width - galleryLayout.paddingLeft
                       - galleryLayout.paddingRight)
        const columns = Math.max(1, Math.floor(canvasWidth / stride))
        const currentColumn = Math.max(
                    0, controller.currentIndex % columns)
        const atStart = destination <= epsilon
        const atEnd = destination >= maximum - epsilon
        let targetIndex = -1
        if (direction > 0 && atEnd) {
            const lastRowStart = Math.floor(
                        (galleryLayout.count - 1) / columns) * columns
            targetIndex = Math.min(galleryLayout.count - 1,
                                   lastRowStart + currentColumn)
        } else if (direction < 0 && atStart) {
            targetIndex = Math.min(galleryLayout.count - 1, currentColumn)
        } else {
            targetIndex = galleryLayout.indexAt(
                        currentItemCenterX,
                        destination + currentItemCenterY)
        }
        if (targetIndex < 0) {
            targetIndex = direction < 0 ? 0 : galleryLayout.count - 1
        }

        const hitEdge = atStart || atEnd
        // Do not run a second minimal ensure-visible animation: the exact
        // quantized destination is authoritative for both the viewport and the
        // independent cursor-chrome endpoint.
        moveCursorWithSelection(targetIndex, togglePrevious,
                                !hitEdge, deferCursorCommit, true)
        animatePanelScrollTo(destination, false, true)
        if (hitEdge)
            resetCurrentItemCenter(targetIndex)
        return targetIndex
    }

    function ensureMasonryPageSequence(plannedContentY) {
        let currentNode = masonryPageNode(masonryPageOrdinal)
        if (!currentNode
                || Math.abs(Number(currentNode.contentY)
                            - plannedContentY) > 0.51) {
            resetMasonryPageSequence()
            storeMasonryPageNode(0, {
                contentY: plannedContentY,
                targetIndex: controller.currentIndex,
                anchorX: currentItemCenterX,
                anchorY: currentItemCenterY,
                hitEdge: false
            })
            currentNode = masonryPageNode(0)
        }
        return currentNode
    }

    function createMasonryPageNode(direction, plannedContentY) {
        const plan = galleryLayout.masonryPagePlan(
                       controller.currentIndex,
                       currentItemCenterX, currentItemCenterY,
                       plannedContentY, masonryPageRowViewportY,
                       direction, galleryLayout.height * 7 / 8)
        if (!plan || !plan.valid)
            return null
        if (!isFinite(masonryPageRowViewportY))
            masonryPageRowViewportY = Number(plan.rowViewportY)
        const destination = Number(plan.contentY)
        const targetIndex = Number(plan.targetIndex)
        const hitEdge = Boolean(plan.hitEdge)
        let targetAnchorX = currentItemCenterX
        let targetAnchorY = currentItemCenterY
        if (hitEdge) {
            const geometry = galleryLayout.indexGeometry(targetIndex)
            if (geometry.width > 0 && geometry.height > 0) {
                targetAnchorX = geometry.x + geometry.width / 2
                targetAnchorY = geometry.y + geometry.height / 2
                        - destination
            }
        }
        return {
            contentY: destination,
            targetIndex: targetIndex,
            anchorX: targetAnchorX,
            anchorY: targetAnchorY,
            hitEdge: hitEdge,
            terminalClamp: Boolean(plan.terminalClamp),
            sourceBandIndex: Number(plan.sourceBandIndex),
            targetBandIndex: Number(plan.targetBandIndex),
            sourceBandTop: Number(plan.sourceBandTop),
            targetBandTop: Number(plan.targetBandTop)
        }
    }

    function navigateMasonryViewportPage(direction, togglePrevious,
                                         deferCursorCommit) {
        if (!controllerReady || galleryLayout.count <= 0
                || controller.currentIndex < 0 || galleryLayout.height <= 0)
            return -1
        if (currentItemCenterX < 0 || currentItemCenterY < 0)
            resetCurrentItemCenter(controller.currentIndex)
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const rawPlannedContentY = panelScrollAnimation.running
                ? Number(panelScrollAnimation.to) : galleryLayout.contentY
        const plannedContentY = Math.max(
                    0, Math.min(maximum, rawPlannedContentY))
        ensureMasonryPageSequence(plannedContentY)
        const nextOrdinal = masonryPageOrdinal + (direction < 0 ? -1 : 1)
        let targetNode = masonryPageNode(nextOrdinal)
        if (!targetNode) {
            targetNode = createMasonryPageNode(direction, plannedContentY)
            if (!targetNode)
                return -1
            const movedViewport = Math.abs(
                        Number(targetNode.contentY) - plannedContentY) > 0.01
            if (!movedViewport
                    && Number(targetNode.targetIndex)
                        === controller.currentIndex)
                return Number(targetNode.targetIndex)
            if (movedViewport)
                storeMasonryPageNode(nextOrdinal, targetNode)
        }

        const targetIndex = Number(targetNode.targetIndex)
        const destination = Number(targetNode.contentY)
        const hitEdge = Boolean(targetNode.hitEdge)
        moveCursorWithSelection(targetIndex, togglePrevious,
                                !hitEdge, deferCursorCommit, true)
        currentItemCenterX = Number(targetNode.anchorX)
        currentItemCenterY = Number(targetNode.anchorY)
        if (Math.abs(destination - plannedContentY) > 0.01) {
            masonryPageOrdinal = nextOrdinal
            animatePanelScrollTo(destination, false, true)
            masonryPageScrollActive = panelScrollAnimation.running
        } else {
            // Cursor-only terminal movement is deliberately not part of the
            // reversible viewport history; seed the next Page action from the
            // new logical cursor and its adopted physical anchor.
            resetMasonryPageSequence()
        }
        return targetIndex
    }

    function navigateViewportPage(direction, togglePrevious,
                                  deferCursorCommit) {
        if (!controllerReady || galleryLayout.count === 0
                || controller.currentIndex < 0 || galleryLayout.height <= 0)
            return

        if (galleryLayout.presentationMode === GalleryViewportItem.Grid) {
            return navigateGridViewportPage(direction, togglePrevious,
                                            deferCursorCommit)
        }
        if (galleryLayout.presentationMode === GalleryViewportItem.Masonry) {
            return navigateMasonryViewportPage(direction, togglePrevious,
                                               deferCursorCommit)
        }

        if (currentItemCenterX < 0 || currentItemCenterY < 0)
            resetCurrentItemCenter(controller.currentIndex)

        // Keep these calculations byte-for-byte equivalent in meaning to the
        // standalone MasonryMode PageUp/PageDown path. In particular, the hit
        // test uses the animation destination and the persistent viewport Y,
        // while reaching either terminal item deliberately adopts its center.
        const deltaY = galleryLayout.height - galleryLayout.height / 8
        const futureContentY = panelScrollAnimation.running
                ? panelScrollAnimation.to : galleryLayout.contentY
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const rawPageContentY = direction < 0
                ? Math.max(0, futureContentY - deltaY)
                : Math.min(maximum, futureContentY + deltaY)
        // Details may quantize the page destination to its row lattice. Pick
        // the cursor from that same destination, otherwise a row selected at
        // the raw page probe can end up exactly outside the aligned viewport.
        const pageContentY = presentationMode === "details"
                ? keyboardAlignedContentY(rawPageContentY, direction)
                : rawPageContentY
        const pageY = pageContentY + currentItemCenterY
        const currentIndex = controller.currentIndex
        let targetIndex = currentIndex
        let hitEdge = false

        if (direction < 0) {
            targetIndex = galleryLayout.indexAt(currentItemCenterX, pageY)
            if (targetIndex === -1)
                targetIndex = 0
            hitEdge = targetIndex === 0
            if (targetIndex === currentIndex) {
                targetIndex = galleryLayout.indexAt(currentItemCenterX, 1)
                hitEdge = true
                if (targetIndex === currentIndex)
                    targetIndex = 0
            }
        } else {
            targetIndex = galleryLayout.indexAt(currentItemCenterX, pageY)
            if (targetIndex === -1)
                targetIndex = galleryLayout.count - 1
            hitEdge = targetIndex >= galleryLayout.count - 1
            if (targetIndex === currentIndex
                    && pageY >= galleryLayout.contentHeight
                                  - galleryLayout.height * 1.5) {
                targetIndex = galleryLayout.indexAt(
                        currentItemCenterX, galleryLayout.contentHeight - 1)
                hitEdge = true
                if (targetIndex === -1) {
                    targetIndex = galleryLayout.indexAt(
                            currentItemCenterX,
                            galleryLayout.contentHeight
                                - galleryLayout.targetHeight * 0.5)
                }
                if (targetIndex === currentIndex || targetIndex === -1)
                    targetIndex = galleryLayout.count - 1
            }
        }

        moveCursorWithSelection(targetIndex, togglePrevious,
                                !hitEdge, deferCursorCommit, !hitEdge)
        scrollBy(direction * deltaY, false, true, direction)
        return targetIndex
    }

    function ensureCurrentVisible(animateScroll, keyboardRevealDirection) {
        if (!controllerReady || galleryLayout.count === 0
                || controller.currentIndex < 0 || galleryLayout.height <= 0)
            return
        const shouldAnimate = animateScroll === undefined
                ? true : Boolean(animateScroll)
        let geometry = galleryLayout.indexGeometry(controller.currentIndex)
        // Columns only lays out its current virtual page.  A cursor restored
        // by stable ID (or moved with Home/End) may therefore have no geometry
        // until we first move the page window that contains it.
        if ((geometry.width <= 0 || geometry.height <= 0)
                && galleryLayout.presentationMode === GalleryViewportItem.Columns) {
            const top = galleryLayout.windowTopIndexForIndex(
                        controller.currentIndex)
            if (top >= 0 && top !== galleryLayout.windowTopIndex)
                galleryLayout.windowTopIndex = top
            geometry = galleryLayout.indexGeometry(controller.currentIndex)
        }
        if (geometry.width <= 0 || geometry.height <= 0)
            return
        let targetY = -1
        if (presentationMode === "columns") {
            if (geometry.x < galleryLayout.contentY)
                targetY = geometry.x
            else if (geometry.x + geometry.width
                     > galleryLayout.contentY + galleryLayout.width)
                targetY = geometry.x + geometry.width - galleryLayout.width
        }
        else if (geometry.y < galleryLayout.contentY)
            targetY = geometry.y
        else if (geometry.y + geometry.height
                 > galleryLayout.contentY + galleryLayout.height)
            targetY = geometry.y + geometry.height - galleryLayout.height
        if (targetY < 0) {
            // An initial cursor that is already visible still owns the saved
            // viewport identity. Record it so later Loader recreations can
            // distinguish restoration from a cursor changed in list mode.
            if (!shouldAnimate) {
                controller.panelScrollOffset = galleryLayout.contentY
                controller.panelViewportCursorEntryId =
                        controller.cursorEntryId
            }
            return
        }
        const maximum = Math.max(0, galleryLayout.contentHeight
                                 - (presentationMode === "columns"
                                    ? galleryLayout.width
                                    : galleryLayout.height))
        targetY = keyboardAlignedContentY(targetY,
                                          keyboardRevealDirection)
        targetY = Math.max(0, Math.min(maximum, targetY))
        const compactRows = presentationMode === "columns"
                || presentationMode === "details"
        if (!shouldAnimate || compactRows) {
            setPanelContentY(targetY, true)
            return
        }
        panelScrollAnimation.from = galleryLayout.contentY
        panelScrollAnimation.to = targetY
        panelScrollAnimation.duration = 150
        panelScrollAnimation.restart()
    }
}
