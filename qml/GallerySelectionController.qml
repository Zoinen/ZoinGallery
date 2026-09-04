pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: selection

    required property GalleryPanel panel
    required property GalleryPanelController controller
    required property GalleryViewportItem layout
    required property GalleryPointerLayer pointerLayer
    required property NumberAnimation panelScrollAnimation
    required property Timer viewportUpdateTimer
    required property Timer cursorCommitTimer

    property int selectionAnchorIndex: -1
    property bool dragCursorActive: false
    property int dragCursorLastIndex: -1

    // Hover belongs to the stable viewport, never to recycled delegates.
    property int hoveredIndex: -1
    property bool hoverPointerInside: false
    property real hoverPointerX: Number.NaN
    property real hoverPointerY: Number.NaN

    property bool keyboardShiftSelectionActive: false
    property int keyboardShiftSelectionAnchorIndex: -1
    property bool keyboardShiftSelectionAdds: true
    property int keyboardShiftSelectionFirst: -1
    property int keyboardShiftSelectionLast: -1
    property int keyboardToggleSelectionKey: 0
    readonly property bool keyboardToggleSelectionActive:
        keyboardToggleSelectionKey !== 0
    readonly property int visualRevision:
        controller ? controller.selectionVisualRevision : 0
    readonly property bool ready:
        controller !== null && controller.backend !== null

    function updateHoveredIndexAt(panelX, panelY) {
        const x = Number(panelX)
        const y = Number(panelY)
        hoverPointerX = x
        hoverPointerY = y
        hoverPointerInside = Number.isFinite(x) && Number.isFinite(y)
        return refreshHoveredIndex()
    }

    function refreshHoveredIndex() {
        if (!hoverPointerInside || dragCursorActive
                || !layout.visible || layout.count <= 0) {
            hoveredIndex = -1
            return hoveredIndex
        }
        const point = layout.mapFromItem(panel, hoverPointerX, hoverPointerY)
        if (point.x < 0 || point.y < 0
                || point.x >= layout.width || point.y >= layout.height) {
            hoveredIndex = -1
            return hoveredIndex
        }
        const index = layout.indexAtViewport(point.x, point.y)
        hoveredIndex = index >= 0 && index < layout.count ? index : -1
        return hoveredIndex
    }

    function clearHoveredIndex() {
        hoverPointerInside = false
        hoverPointerX = Number.NaN
        hoverPointerY = Number.NaN
        hoveredIndex = -1
    }

    function selectionIds(firstIndex, lastIndex) {
        const ids = []
        if (!ready)
            return ids
        const first = Math.max(0, Math.min(firstIndex, lastIndex))
        const last = Math.min(layout.count - 1,
                              Math.max(firstIndex, lastIndex))
        for (let index = first; index <= last; ++index) {
            const entryId = controller.entryIdAt(index)
            // f4 deliberately excludes its synthetic parent entry from mouse
            // selection, so embedded Gallery mirrors that contract.
            if (entryId !== "" && controller.entryNameAt(index) !== "..")
                ids.push(entryId)
        }
        return ids
    }

    function handlePointerPress(viewIndex, button, modifiers) {
        if (!ready || viewIndex < 0 || viewIndex >= layout.count)
            return

        panel.cancelCursorChromeTransition()
        const interruptedKeyboardScroll = panelScrollAnimation.running
        if (interruptedKeyboardScroll) {
            viewportUpdateTimer.stop()
            panel.viewportUpdateEnsuresCursor = false
            panel.viewportUpdateSuppressAnimation = false
            panel.viewportUpdatePendingAfterScroll = false
            panel.setPanelContentY(layout.contentY, false)
        }
        const previousIndex = controller.currentIndex
        panel.forceActiveFocus()

        // Middle-button ownership lives on the panel-level input layer.
        if ((button & Qt.MiddleButton) !== 0)
            return

        if (keyboardShiftSelectionActive || keyboardToggleSelectionActive)
            finishKeyboardSelectionGesture()
        dragCursorActive = (button & (Qt.LeftButton | Qt.RightButton)) !== 0
        dragCursorLastIndex = dragCursorActive ? viewIndex : -1
        hoveredIndex = -1

        const previousVisualIndex = panel.visualCursorIndex
        panel.localCursorNavigation = true
        panel.selectIndex(viewIndex, false, true)
        panel.localCursorNavigation = false
        panel.ensureCurrentVisible()
        panel.resetCurrentItemCenter(viewIndex)
        panel.coordinateVisualCursor(viewIndex, previousVisualIndex)
        if (interruptedKeyboardScroll) {
            controller.panelScrollOffset = layout.contentY
            controller.panelViewportCursorEntryId = controller.cursorEntryId
        }

        const commandModifier = Boolean(modifiers
            & (Qt.ControlModifier | Qt.MetaModifier))
        const shiftModifier = Boolean(modifiers & Qt.ShiftModifier)
        if ((button & Qt.RightButton) !== 0) {
            if (panel.scrollingMode)
                pointerLayer.endAutoScroll()
            selectionAnchorIndex = viewIndex
            const ids = selectionIds(viewIndex, viewIndex)
            if (ids.length > 0)
                panel.selectionRequested("toggle", ids)
            beginKeyboardShiftSelection(viewIndex)
        } else if (shiftModifier) {
            const anchor = selectionAnchorIndex >= 0
                    ? selectionAnchorIndex
                    : (previousIndex >= 0 ? previousIndex : viewIndex)
            const ids = selectionIds(anchor, viewIndex)
            if (ids.length > 0) {
                panel.selectionRequested(commandModifier ? "add" : "replace",
                                         ids)
            }
        } else if (commandModifier) {
            selectionAnchorIndex = viewIndex
            const ids = selectionIds(viewIndex, viewIndex)
            if (ids.length > 0)
                panel.selectionRequested("toggle", ids)
        } else {
            selectionAnchorIndex = viewIndex
        }
    }

    function invertPanelSelection() {
        if (!ready || layout.count <= 0)
            return
        const ids = selectionIds(0, layout.count - 1)
        if (ids.length > 0)
            panel.selectionRequested("toggle", ids)
    }

    function handlePointerDrag(panelX, panelY) {
        if (!ready || layout.count <= 0)
            return
        if (!dragCursorActive && !keyboardShiftSelectionActive)
            return
        updateHoveredIndexAt(panelX, panelY)
        const point = layout.mapFromItem(panel, panelX, panelY)
        const clampedX = Math.max(0, Math.min(layout.width - 0.01, point.x))
        const clampedY = Math.max(0, Math.min(layout.height - 0.01, point.y))
        const index = layout.indexAtViewport(clampedX, clampedY)
        if (index < 0)
            return
        const indexChanged = index !== dragCursorLastIndex
        if (dragCursorActive && indexChanged) {
            dragCursorLastIndex = index
            const previousVisualIndex = panel.visualCursorIndex
            panel.localCursorNavigation = true
            panel.selectIndex(index, false, true)
            panel.localCursorNavigation = false
            panel.ensureCurrentVisible(false)
            panel.resetCurrentItemCenter(index)
            panel.coordinateVisualCursor(index, previousVisualIndex)
        }
        if (keyboardShiftSelectionActive && indexChanged)
            updateDragPaintSelection(index)
    }

    function endPointerDrag() {
        const commitCursor = dragCursorActive && panel.cursorCommitPending
        dragCursorActive = false
        if (keyboardShiftSelectionActive)
            finishKeyboardShiftSelection()
        if (commitCursor)
            panel.commitPendingCursor()
        refreshHoveredIndex()
    }

    function moveCursorWithSelection(index, togglePrevious,
                                     preserveHorizontalAnchor,
                                     deferCursorCommit,
                                     preserveVerticalAnchor,
                                     keyboardRevealDirection) {
        if (!ready || layout.count === 0)
            return
        const previousIndex = controller.currentIndex
        const bounded = Math.max(0, Math.min(layout.count - 1, index))
        if (togglePrevious)
            beginKeyboardShiftSelection(previousIndex, true)
        if (bounded === previousIndex) {
            if (togglePrevious)
                updateKeyboardShiftSelection(bounded)
            if (!preserveHorizontalAnchor)
                panel.resetCurrentItemCenterX(previousIndex)
            if (!preserveVerticalAnchor) {
                panel.ensureCurrentVisible(undefined, keyboardRevealDirection)
                panel.resetCurrentItemCenterY(previousIndex)
            }
            return
        }
        panel.moveCursor(bounded, togglePrevious, preserveHorizontalAnchor,
                         deferCursorCommit, preserveVerticalAnchor,
                         keyboardRevealDirection)
        if (togglePrevious)
            updateKeyboardShiftSelection(bounded)
    }

    function beginKeyboardShiftSelection(anchorIndex, selectionAdds) {
        if (keyboardShiftSelectionActive)
            return
        keyboardShiftSelectionActive = true
        keyboardShiftSelectionAnchorIndex = anchorIndex
        keyboardShiftSelectionAdds = selectionAdds === undefined
                ? !controller.isSelectedAt(anchorIndex) : Boolean(selectionAdds)
        keyboardShiftSelectionFirst = -1
        keyboardShiftSelectionLast = -1
        controller.beginSelectionGesture(keyboardShiftSelectionAdds)
    }

    function updateKeyboardShiftSelection(targetIndex) {
        if (!keyboardShiftSelectionActive || !ready)
            return
        const anchor = keyboardShiftSelectionAnchorIndex
        let first = -1
        let last = -1
        if (targetIndex > anchor) {
            first = anchor
            last = targetIndex - 1
        } else if (targetIndex < anchor) {
            first = targetIndex + 1
            last = anchor
        }
        applyShiftSelectionRange(first, last)
    }

    function updateDragPaintSelection(targetIndex) {
        if (!keyboardShiftSelectionActive || !ready)
            return
        const anchor = keyboardShiftSelectionAnchorIndex
        applyShiftSelectionRange(Math.min(anchor, targetIndex),
                                 Math.max(anchor, targetIndex))
    }

    function togglePendingKeyboardSelection(index) {
        if (!ready || index < 0 || index >= layout.count)
            return
        controller.toggleSelectionAt(index)
    }

    function applyShiftSelectionRange(first, last) {
        controller.previewSelectionRange(first, last)
        keyboardShiftSelectionFirst = first
        keyboardShiftSelectionLast = last
        cursorCommitTimer.restart()
    }

    function effectiveEntrySelected(entryId, authoritativeSelected) {
        const revision = visualRevision
        return controller.effectiveSelected(entryId,
                                            Boolean(authoritativeSelected))
    }

    function commitPendingKeyboardSelection() {
        return controller.commitSelectionGesture()
    }

    function reconcileAcknowledgedKeyboardSelection() {
        // The C++ controller reconciles its sparse awaiting set when the
        // backend's selection revision advances.
    }

    function clearPendingKeyboardSelection() {
        controller.cancelSelectionGesture()
        keyboardShiftSelectionActive = false
        keyboardShiftSelectionAnchorIndex = -1
        keyboardShiftSelectionAdds = true
        keyboardShiftSelectionFirst = -1
        keyboardShiftSelectionLast = -1
        keyboardToggleSelectionKey = 0
    }

    function finishKeyboardShiftSelection() {
        if (!keyboardShiftSelectionActive)
            return false
        const committed = commitPendingKeyboardSelection()
        keyboardShiftSelectionActive = false
        keyboardShiftSelectionAnchorIndex = -1
        keyboardShiftSelectionAdds = true
        keyboardShiftSelectionFirst = -1
        keyboardShiftSelectionLast = -1
        return committed
    }

    function beginKeyboardToggleSelection(key) {
        if (keyboardToggleSelectionKey === key)
            return
        keyboardToggleSelectionKey = key
        controller.beginSelectionGesture(true)
    }

    function finishKeyboardToggleSelection() {
        if (!keyboardToggleSelectionActive)
            return false
        const committed = commitPendingKeyboardSelection()
        keyboardToggleSelectionKey = 0
        return committed
    }

    function finishKeyboardSelectionGesture() {
        let committed = false
        if (keyboardShiftSelectionActive)
            committed = finishKeyboardShiftSelection() || committed
        if (keyboardToggleSelectionActive)
            committed = finishKeyboardToggleSelection() || committed
        return committed
    }
}
