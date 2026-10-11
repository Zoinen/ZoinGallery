pragma ComponentBehavior: Bound

import QtQuick

// Stable public interaction API, separate from panel composition and state.
FocusScope {
    id: facade
    readonly property var panelFacade: facade

    function updateHoveredIndexAt(panelX, panelY) {
        return panelFacade.selectionController.updateHoveredIndexAt(panelX, panelY)
    }

    function refreshHoveredIndex() {
        return panelFacade.selectionController.refreshHoveredIndex()
    }

    function clearHoveredIndex() {
        panelFacade.selectionController.clearHoveredIndex()
    }

    function alignViewportItemRectToDevicePixels(item, rect) {
        return panelFacade.cursorController.alignViewportItemRectToDevicePixels(item, rect)
    }

    function noteDensityChanged(finalChange) {
        panelFacade.viewportController.noteDensityChanged(finalChange)
    }

    function currentTransitionItem() {
        return panelFacade.viewerTransitionSource.currentItem()
    }

    function currentItemImageGeometry(targetItem) {
        return panelFacade.viewerTransitionSource.imageGeometry(targetItem)
    }

    function currentItemImageSource() {
        return panelFacade.viewerTransitionSource.imageSource()
    }

    function handlePointerPress(viewIndex, button, modifiers) {
        panelFacade.selectionController.handlePointerPress(viewIndex, button, modifiers)
    }

    function invertPanelSelection() {
        panelFacade.selectionController.invertPanelSelection()
    }

    function handlePointerDrag(panelX, panelY) {
        panelFacade.selectionController.handlePointerDrag(panelX, panelY)
    }

    function endPointerDrag() {
        panelFacade.selectionController.endPointerDrag()
    }

    function handlePanelMiddlePress(x, y, modifiers) {
        panelFacade.viewportController.handlePanelMiddlePress(x, y, modifiers)
    }

    function stepDensity(zoomIn) {
        panelFacade.viewportController.stepDensity(zoomIn)
    }

    function resetDensity(value) {
        panelFacade.viewportController.resetDensity(value)
    }

    function handlePanelMiddleRelease(x, y, modifiers) {
        panelFacade.viewportController.handlePanelMiddleRelease(x, y, modifiers)
    }

    function handlePanelWheel(pixelDeltaY, angleDeltaY, modifiers,
                              pixelDeltaX, angleDeltaX) {
        return panelFacade.viewportController.handlePanelWheel(
                    pixelDeltaY, angleDeltaY, modifiers,
                    pixelDeltaX, angleDeltaX)
    }

    function beginThumbnailPinch() {
        panelFacade.viewportController.beginThumbnailPinch()
    }

    function updateThumbnailPinch(scale) {
        panelFacade.viewportController.updateThumbnailPinch(scale)
    }

    function finishThumbnailPinch() {
        panelFacade.viewportController.finishThumbnailPinch()
    }

    function setPanelContentY(value, persist) {
        panelFacade.viewportController.setPanelContentY(value, persist)
    }

    function beginPresentationSwitch() {
        panelFacade.viewportController.beginPresentationSwitch()
    }

    function beginPresentationStateUpdate(switchingMode) {
        panelFacade.viewportController.beginPresentationStateUpdate(switchingMode)
    }

    function endPresentationStateUpdate(publishVisibleRange) {
        panelFacade.viewportController.endPresentationStateUpdate(publishVisibleRange)
    }

    function restoreScrollOffset() {
        return panelFacade.viewportController.restoreScrollOffset()
    }

    function restoreScrollOrEnsureCursor() {
        panelFacade.viewportController.restoreScrollOrEnsureCursor()
    }

    function centerCurrentForPathChange() {
        return panelFacade.viewportController.centerCurrentForPathChange()
    }

    function restoreRememberedViewportForPathChange() {
        return panelFacade.viewportController.restoreRememberedViewportForPathChange()
    }

    function placeViewportForPathChange() {
        return panelFacade.viewportController.placeViewportForPathChange()
    }

    function schedulePathViewportPlacement(reason) {
        panelFacade.viewportController.schedulePathViewportPlacement(reason)
    }

    function scheduleViewportUpdate(ensureCursor) {
        panelFacade.viewportController.scheduleViewportUpdate(ensureCursor)
    }

    function selectIndex(viewIndex, openItem, deferCursorCommit,
                         autoRepeat) {
        panelFacade.navigationController.selectIndex(viewIndex, openItem, deferCursorCommit,
                                    autoRepeat)
    }

    function commitPendingCursor() {
        panelFacade.navigationController.commitPendingCursor()
    }

    function refreshPendingCursorCommit() {
        panelFacade.navigationController.refreshPendingCursorCommit()
    }

    function resetCurrentItemCenterX(index) {
        panelFacade.navigationController.resetCurrentItemCenterX(index)
    }

    function resetCurrentItemCenterY(index) {
        panelFacade.navigationController.resetCurrentItemCenterY(index)
    }

    function resetCurrentItemCenter(index) {
        panelFacade.navigationController.resetCurrentItemCenter(index)
    }

    function indexIntersectsViewport(index) {
        return panelFacade.cursorController.indexIntersectsViewport(index)
    }

    function nearestVisibleCursor(targetIndex) {
        return panelFacade.cursorController.nearestVisibleCursor(targetIndex)
    }

    function cursorAtViewportAnchor() {
        return panelFacade.cursorController.cursorAtViewportAnchor()
    }

    function updateVisualCursorForViewport() {
        panelFacade.cursorController.updateVisualCursorForViewport()
    }

    function cursorChromeNavigationSnapshot() {
        return panelFacade.cursorController.cursorChromeNavigationSnapshot()
    }

    function cursorChromeRectForIndex(index, plannedContentY) {
        return panelFacade.cursorController.cursorChromeRectForIndex(index, plannedContentY)
    }

    function startCursorChromeGeometry(startRect, targetRect, targetIndex) {
        return panelFacade.cursorController.startCursorChromeGeometry(
                    startRect, targetRect, targetIndex)
    }

    function startCursorChromeForNavigation(snapshot, targetIndex) {
        return panelFacade.cursorController.startCursorChromeForNavigation(snapshot, targetIndex)
    }

    function retargetCursorChromeAfterLayoutReset() {
        panelFacade.cursorController.retargetCursorChromeAfterLayoutReset()
    }

    function cancelCursorChromeTransition() {
        panelFacade.cursorController.cancelCursorChromeTransition()
    }

    function finishCursorChromeTransition() {
        panelFacade.cursorController.finishCursorChromeTransition()
    }

    function coordinateVisualCursor(targetIndex, previousIndex) {
        panelFacade.cursorController.coordinateVisualCursor(targetIndex, previousIndex)
    }

    function navigationTargetForKey(key, page) {
        return panelFacade.navigationController.navigationTargetForKey(key, page)
    }

    function moveCursor(index, preserveSelectionAnchor,
                        preserveHorizontalAnchor, deferCursorCommit,
                        preserveVerticalAnchor, keyboardRevealDirection) {
        panelFacade.navigationController.moveCursor(
                    index, preserveSelectionAnchor,
                    preserveHorizontalAnchor, deferCursorCommit,
                    preserveVerticalAnchor, keyboardRevealDirection)
    }

    function moveCursorWithSelection(index, togglePrevious,
                                     preserveHorizontalAnchor,
                                     deferCursorCommit,
                                     preserveVerticalAnchor,
                                     keyboardRevealDirection,
                                     includeSelectionTarget) {
        panelFacade.selectionController.moveCursorWithSelection(
                    index, togglePrevious, preserveHorizontalAnchor,
                    deferCursorCommit, preserveVerticalAnchor,
                    keyboardRevealDirection, includeSelectionTarget)
    }

    function beginKeyboardShiftSelection(anchorIndex, selectionAdds) {
        panelFacade.selectionController.beginKeyboardShiftSelection(anchorIndex, selectionAdds)
    }

    function togglePendingKeyboardSelection(index) {
        panelFacade.selectionController.togglePendingKeyboardSelection(index)
    }

    function effectiveEntrySelected(entryId, authoritativeSelected) {
        return panelFacade.selectionController.effectiveEntrySelected(entryId,
                                                     authoritativeSelected)
    }

    function reconcileAcknowledgedKeyboardSelection() {
        panelFacade.selectionController.reconcileAcknowledgedKeyboardSelection()
    }

    function clearPendingKeyboardSelection() {
        panelFacade.selectionController.clearPendingKeyboardSelection()
    }

    function finishKeyboardShiftSelection() {
        return panelFacade.selectionController.finishKeyboardShiftSelection()
    }

    function beginKeyboardToggleSelection(key) {
        panelFacade.selectionController.beginKeyboardToggleSelection(key)
    }

    function finishKeyboardToggleSelection() {
        return panelFacade.selectionController.finishKeyboardToggleSelection()
    }

    function finishKeyboardSelectionGesture() {
        return panelFacade.selectionController.finishKeyboardSelectionGesture()
    }

    function commitCursorAfterNavigation() {
        panelFacade.navigationController.commitCursorAfterNavigation()
    }

    function resetGridPageLattice() {
        panelFacade.navigationController.resetGridPageLattice()
    }

    function resetMasonryPageSequence() {
        panelFacade.navigationController.resetMasonryPageSequence()
    }

    function invalidateMasonryPageGeometry() {
        panelFacade.navigationController.invalidateMasonryPageGeometry()
    }

    function navigateViewportPage(direction, togglePrevious,
                                  deferCursorCommit) {
        return panelFacade.navigationController.navigateViewportPage(
                    direction, togglePrevious, deferCursorCommit)
    }

    function ensureCurrentVisible(animateScroll, keyboardRevealDirection) {
        panelFacade.navigationController.ensureCurrentVisible(
                    animateScroll, keyboardRevealDirection)
    }

    function ownsKey(event) {
        return panelFacade.inputController.ownsKey(event)
    }

    function ensureSessionPreviews() {
        panelFacade.reconciler.ensureSessionPreviews()
    }

    function handleLocalQuickSearchKey(event) {
        return panelFacade.inputController.handleLocalQuickSearchKey(event)
    }

    function resetControllerState() {
        panelFacade.reconciler.resetControllerState()
    }

    function scheduleCursorChromeLayoutRetarget() {
        panelFacade.motionController.cursorRetargetTimer.restart()
    }

    function scheduleThumbnailResizeDecode() {
        panelFacade.motionController.thumbnailResizeTimer.restart()
    }
}
