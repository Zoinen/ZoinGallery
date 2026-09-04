pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

import ZoinGallery.Native 1.0

FocusScope {
    id: root
    // Delegates recycle beyond the viewport; the host owns trailing clipping.
    clip: false

    property GallerySession session
    // The default typed controller wraps GallerySession without catalog copies.
    property GalleryPanelController controller: GalleryPanelController {
        session: root.session
    }
    readonly property bool controllerReady:
        controller !== null && controller.backend !== null
    property GalleryIconResolver iconResolver: GalleryIconResolver {}
    property GalleryThemePalette theme: GalleryThemePalette {}
    // Embedders can replace or suppress the standalone gallery wording while
    // retaining the same authoritative zero-row condition.
    property bool emptyStateEnabled: true
    property string emptyStateText: qsTr("No previewable entries")
    // Stable entry-ID spans repaint quick-search labels without model resets.
    property var quickSearchMatches: ({})
    // Standalone/local backends may let this component own quick search.
    // f4 keeps it false because Go's command/fast-find stream is authoritative.
    property bool localQuickSearchEnabled: false
    // Geometry supplied by an embedding shell.  Keeping these values
    // explicit lets the reusable Details renderer match an existing file
    // list without reaching into that shell's context properties.
    property GalleryPresentationMetrics metrics: GalleryPresentationMetrics {}
    property var hostCapabilities: ({})
    property real devicePixelRatio: 1.0
    readonly property font iconLabelFont: galleryViewport.iconLabelFont
    property alias galleryLayout: galleryViewport.layout
    property alias detailsHeader: galleryViewport.detailsHeader
    property alias pointerLayer: galleryViewport.pointerLayer
    readonly property GalleryViewport galleryViewportItem: galleryViewport
    readonly property NumberAnimation panelScrollAnimationObject:
        motionState.scrollAnimation
    readonly property ParallelAnimation cursorChromeAnimationObject:
        motionState.cursorAnimation
    readonly property NumberAnimation cursorChromeXAnimationObject:
        motionState.cursorXAnimation
    readonly property NumberAnimation cursorChromeYAnimationObject:
        motionState.cursorYAnimation
    readonly property NumberAnimation cursorChromeWidthAnimationObject:
        motionState.cursorWidthAnimation
    readonly property NumberAnimation cursorChromeHeightAnimationObject:
        motionState.cursorHeightAnimation
    readonly property Timer cursorChromeFinalizeTimerObject:
        motionState.cursorFinalizeTimer
    readonly property Timer cursorChromeRetargetTimerObject:
        motionState.cursorRetargetTimer
    readonly property Timer pathPlacementTimerObject:
        motionState.pathPlacementTimer
    readonly property Timer viewportUpdateTimerObject: motionState.viewportTimer
    readonly property Timer cursorCommitTimerObject: motionState.cursorCommitTimer
    readonly property Timer cursorAfterScrollTimerObject:
        motionState.cursorAfterScrollTimer
    readonly property Timer densityCommitTimerObject: motionState.densityTimer
    readonly property bool panelScrollAnimationRunning:
        motionState.scrollAnimation.running

    readonly property GalleryPanelMotion motionController: motionState
    GalleryPanelMotion {
        id: motionState
        panel: root
    }
    property string presentationMode: "masonry"
    property bool applyingPresentationMode: false
    // Saved zoom is bounded by the five presentation modes.
    property var presentationDensities: ({})
    // All five presentations expose the same zoom paths. Compact text modes
    // adjust row pitch in two-pixel steps; Grid and Icons delegate their steps
    // to GalleryViewportItem so each action crosses one cell-count boundary.
    readonly property bool densityAdjustmentEnabled: true
    // GUI mode owns smooth wheel scrolling and the reusable middle-button
    // auto-scroll gesture. The host can opt into the terminal contract.
    property string mouseWheelMode: "gui"
    property alias scrollingStarted: galleryViewport.scrollingStarted
    property alias scrollingStartedAtY: galleryViewport.scrollingStartedAtY
    property alias scrollingMode: galleryViewport.scrollingMode
    property int columnCount: 2
    property var columnSchema: []
    // A host may keep its own column header outside the reusable viewport.
    // Standalone users retain the module-local header by default.
    property bool showDetailsHeader: true
    // A streaming embedder may know that the current logical count is only a
    // bounded preview. Keep scrollbar geometry hidden until that count is
    // authoritative instead of flashing a confidently wrong thumb.
    property bool scrollBarsReady: true
    // Hosts that already split displayBaseName/displayExtension may align the
    // extension to the trailing edge of the name field. Standalone keeps the
    // established combined filename by default.
    property bool separateFileExtensions: false
    property alias thumbnailHeight: galleryViewport.targetHeight
    property alias density: galleryViewport.density
    property bool listView: false
    property bool showTransparentGrid: true
    property int itemSpacing: 8
    property string quickSearchExcludedCharacters: ""
    property bool autoFocus: true
    // Embedders which retain hidden panel viewports can disable brick
    // geometry interpolation while those viewports absorb deferred metadata.
    // Standalone Gallery keeps its established animated resizing by default.
    property bool animateLayoutChanges: true
    readonly property GallerySelectionController selectionController:
        selectionState
    property alias selectionAnchorIndex: selectionState.selectionAnchorIndex
    property alias dragCursorActive: selectionState.dragCursorActive
    property alias dragCursorLastIndex: selectionState.dragCursorLastIndex
    property alias hoveredIndex: selectionState.hoveredIndex
    property alias hoverPointerInside: selectionState.hoverPointerInside
    property alias hoverPointerX: selectionState.hoverPointerX
    property alias hoverPointerY: selectionState.hoverPointerY
    readonly property int keyboardSelectionVisualRevision:
        selectionState.visualRevision
    property alias keyboardShiftSelectionActive:
        selectionState.keyboardShiftSelectionActive
    property alias keyboardShiftSelectionAnchorIndex:
        selectionState.keyboardShiftSelectionAnchorIndex
    property alias keyboardShiftSelectionAdds:
        selectionState.keyboardShiftSelectionAdds
    property alias keyboardShiftSelectionFirst:
        selectionState.keyboardShiftSelectionFirst
    property alias keyboardShiftSelectionLast:
        selectionState.keyboardShiftSelectionLast
    property alias keyboardToggleSelectionKey:
        selectionState.keyboardToggleSelectionKey
    readonly property bool keyboardToggleSelectionActive:
        selectionState.keyboardToggleSelectionActive

    GallerySelectionController {
        id: selectionState
        panel: root
        controller: root.controller
        layout: root.galleryLayout
        pointerLayer: root.pointerLayer
        panelScrollAnimation: root.panelScrollAnimationObject
        viewportUpdateTimer: root.viewportUpdateTimerObject
        cursorCommitTimer: root.cursorCommitTimerObject
    }

    function updateHoveredIndexAt(panelX, panelY) {
        return selectionState.updateHoveredIndexAt(panelX, panelY)
    }
    function refreshHoveredIndex() {
        return selectionState.refreshHoveredIndex()
    }
    function clearHoveredIndex() {
        selectionState.clearHoveredIndex()
    }

    readonly property GalleryViewportController viewportController:
        viewportState
    property alias restoringScrollOffset: viewportState.restoringScrollOffset
    property alias viewportUpdateEnsuresCursor:
        viewportState.viewportUpdateEnsuresCursor
    // Masonry/Icons/Columns take the generic ensureCursor reveal path (not
    // Details' dedicated path-placement flow) on a folder change too, since
    // that path only special-cases Details. Set alongside
    // viewportUpdateEnsuresCursor so the deferred reveal can tell "new
    // folder's initial cursor" apart from an ordinary same-folder cursor
    // move and jump instead of animating.
    property alias viewportUpdateSuppressAnimation:
        viewportState.viewportUpdateSuppressAnimation
    property alias viewportUpdatePendingAfterScroll:
        viewportState.viewportUpdatePendingAfterScroll
    // A directory replacement resets the model and its contentY before the
    // authoritative cursor for the new path arrives. Keep Details unpainted
    // for that short transaction so it never exposes the top rows first.
    property alias pathViewportPlacementPending:
        viewportState.pathViewportPlacementPending
    // applyExternalCatalog can remap a cursor with the same stable ID before
    // it announces the replacement catalog. That remap is provisional until
    // the host applies its authoritative state, so it must not complete the
    // path-placement transaction synchronously.
    property alias pathViewportCatalogReady:
        viewportState.pathViewportCatalogReady
    // Benchmark instrumentation is deliberately opt-in.  Normal Gallery
    // navigation must not allocate diagnostic maps on every cursor change.
    // Embedders can enable this and forward benchmarkStage without coupling
    // the reusable panel to a particular tracing backend.
    property bool benchmarkTracingEnabled: false
    property alias presentationSwitchPending:
        viewportState.presentationSwitchPending
    property alias presentationSwitchCursorViewportY:
        viewportState.presentationSwitchCursorViewportY
    property alias thumbnailPinchStartHeight:
        viewportState.thumbnailPinchStartHeight
    property alias densityViewportTransaction:
        viewportState.densityViewportTransaction
    property alias suppressScrollAnimationPersistence:
        viewportState.suppressScrollAnimationPersistence
    // During a host-bracketed mode switch the old and new delegate windows
    // must never observe intermediate parent geometry. The flag is raised and
    // lowered in one event stack, so no rendered frame is blank.
    property alias presentationLayoutHidden:
        viewportState.presentationLayoutHidden
    property alias presentationStateHidesLayout:
        viewportState.presentationStateHidesLayout

    GalleryViewportController {
        id: viewportState
        panel: root
        controller: root.controller
        layoutItem: root.galleryLayout
        viewportItem: root.galleryViewportItem
        pointerInput: root.pointerLayer
        scrollAnimation: root.panelScrollAnimationObject
        chromeGeometryAnimation: root.cursorChromeAnimationObject
        pathPlacementTimer: root.pathPlacementTimerObject
        viewportTimer: root.viewportUpdateTimerObject
        cursorTimer: root.cursorCommitTimerObject
        cursorAfterScrollTimer: root.cursorAfterScrollTimerObject
        densityTimer: root.densityCommitTimerObject
    }
    readonly property GalleryNavigationController navigationController:
        navigationState
    property alias localCursorNavigation:
        navigationState.localCursorNavigation
    property alias cursorCommitPending: navigationState.cursorCommitPending
    property alias cursorCommitAfterScroll:
        navigationState.cursorCommitAfterScroll
    property alias navigationKeyHeld: navigationState.navigationKeyHeld
    property alias currentItemCenterX: navigationState.currentItemCenterX
    property alias currentItemCenterY: navigationState.currentItemCenterY
    property alias gridPageAnchorPhase: navigationState.gridPageAnchorPhase
    property alias gridPageAnchorStride: navigationState.gridPageAnchorStride
    property alias gridPageAnchorPaddingTop:
        navigationState.gridPageAnchorPaddingTop
    property alias masonryPageRowViewportY:
        navigationState.masonryPageRowViewportY
    property alias masonryPageOrdinal: navigationState.masonryPageOrdinal
    property alias masonryPageNodes: navigationState.masonryPageNodes
    property alias masonryPageScrollActive:
        navigationState.masonryPageScrollActive

    GalleryNavigationController {
        id: navigationState
        panel: root
        controller: root.controller
        layoutItem: root.galleryLayout
        scrollAnimation: root.panelScrollAnimationObject
        chromeAnimation: root.cursorChromeAnimationObject
        cursorTimer: root.cursorCommitTimerObject
        cursorAfterScrollTimer: root.cursorAfterScrollTimerObject
    }

    readonly property GalleryPanelInput inputController: inputState
    GalleryPanelInput {
        id: inputState
        panel: root
    }
    readonly property GalleryPanelReconciler reconciler: reconciliationState
    GalleryPanelReconciler {
        id: reconciliationState
        panel: root
    }

    // Keep the rendered cursor coupled to the animated viewport. The session
    // cursor remains the authoritative navigation target immediately (so key
    // repeat and stable-ID commits never lose a move), while this index follows
    // the item crossing the preserved viewport anchor until the target arrives.
    // This supplies the visual half missing from MasonryMode's ensure-visible
    // contract: a cursor is never painted wholly behind the viewport clip.
    readonly property GalleryCursorController cursorController: cursorState
    property alias visualCursorIndex: cursorState.visualCursorIndex
    property alias pendingVisualCursorIndex: cursorState.pendingVisualCursorIndex
    property alias cursorChromeTransitionActive:
        cursorState.cursorChromeTransitionActive
    property alias cursorChromeTargetIndex: cursorState.cursorChromeTargetIndex
    property alias cursorChromeX: cursorState.cursorChromeX
    property alias cursorChromeY: cursorState.cursorChromeY
    property alias cursorChromeWidth: cursorState.cursorChromeWidth
    property alias cursorChromeHeight: cursorState.cursorChromeHeight
    property alias cursorChromeTargetX: cursorState.cursorChromeTargetX
    property alias cursorChromeTargetY: cursorState.cursorChromeTargetY
    property alias cursorChromeTargetWidth:
        cursorState.cursorChromeTargetWidth
    property alias cursorChromeTargetHeight:
        cursorState.cursorChromeTargetHeight
    property alias cursorChromeRadius: cursorState.cursorChromeRadius
    property alias cursorChromeBorderWidth:
        cursorState.cursorChromeBorderWidth
    property alias cursorChromeFillColor: cursorState.cursorChromeFillColor
    property alias cursorChromeBorderColor: cursorState.cursorChromeBorderColor
    readonly property int cursorChromeCoveredIndex:
        cursorState.cursorChromeCoveredIndex
    readonly property rect cursorChromeRect: cursorState.cursorChromeRect
    readonly property rect cursorChromeTargetRect:
        cursorState.cursorChromeTargetRect
    readonly property point cursorPixelGridViewportOrigin:
        cursorState.cursorPixelGridViewportOrigin
    readonly property bool cursorPixelAlignmentSuspended:
        cursorState.cursorPixelAlignmentSuspended

    GalleryCursorController {
        id: cursorState
        panel: root
        controller: root.controller
        layoutItem: root.galleryLayout
        scrollAnimation: root.panelScrollAnimationObject
        chromeAnimation: root.cursorChromeAnimationObject
        xAnimation: root.cursorChromeXAnimationObject
        yAnimation: root.cursorChromeYAnimationObject
        widthAnimation: root.cursorChromeWidthAnimationObject
        heightAnimation: root.cursorChromeHeightAnimationObject
        finalizeTimer: root.cursorChromeFinalizeTimerObject
        layoutRetargetTimer: root.cursorChromeRetargetTimerObject
    }

    function alignViewportItemRectToDevicePixels(item, rect) {
        return cursorState.alignViewportItemRectToDevicePixels(item, rect)
    }

    // The full-area viewer sets these while its image is animating to or from
    // the active tile.  Only the tile image is suppressed; panel chrome,
    // selection, and labels remain stable underneath the transition.
    property bool viewerTransitionActive: false
    property string viewerTransitionEntryId: ""
    // Embedded multi-panel hosts hide the navigation cursor on inactive
    // panels while keeping persistent multi-selection markers visible.
    property bool showCursor: true
    signal activateRequested()
    // Auto-repeat navigation remains optimistic and local. The host receives
    // the desired cursor so it can mask older authoritative scenes, while the
    // expensive semantic round-trip may wait until repeat stops.
    signal cursorRequested(string entryId, int index, bool deferCommit)
    signal openRequested(string entryId, int index, bool isImage,
                         bool autoRepeat)
    signal selectionRequested(string mode, var entryIds)
    signal selectionTransactionRequested(var changes, string cursorEntryId,
                                         int cursorIndex)
    signal densityChangeRequested(string mode, real density, bool finalChange)

    signal sortRequested(string sortMode, bool contextMenu)
    signal benchmarkStage(string stage, var metadata)
    signal metadataVisibleRangeChanged(int firstRow, int lastRow)
    signal consoleWheelRequested(real x, real y, int angleDeltaY,
                                 int modifiers)
    signal consoleMouseButtonRequested(real x, real y, int button, bool down,
                                       int modifiers)

    readonly property color backgroundColor: theme.panelBackground
    readonly property color foregroundColor: theme.text
    property color quickSearchMatchColor: theme.quickSearchMatch
    readonly property color mutedColor: theme.mutedText
    readonly property color cursorColor: theme.cursor
    readonly property color selectionColor: theme.selection
    readonly property bool showSelectionBorders: theme.showSelectionBorders
    readonly property color cursorBackgroundColor: theme.cursorBackground
    readonly property color cursorBorderColor: theme.cursorBorder
    readonly property color cardCursorBorderColor: theme.cardCursorBorder
    readonly property color markedBackgroundColor: theme.markedBackground
    readonly property color markedTextColor: theme.markedText
    readonly property color directoryTextColor: theme.directoryText
    readonly property bool neutralFileTextColors: theme.neutralFileTextColors
    readonly property color fileTextColor: theme.fileText
    readonly property color folderTextColor: theme.folderText
    readonly property color folderIconColor: theme.folderIcon
    readonly property color itemBackgroundColor: theme.itemBackground
    readonly property color directoryBackgroundColor: theme.directoryBackground
    readonly property color itemHoverColor: theme.itemHover
    readonly property color labelBackgroundColor: theme.labelBackground
    readonly property color previewBackdropColor: theme.previewBackdrop
    readonly property color overlayBackgroundColor: theme.dialogBackground
    readonly property color separatorColor: theme.separator
    readonly property color headerTextColor: theme.headerText
    readonly property color headerHoverColor: theme.controlHover

    readonly property GalleryQuickSearchFormatter quickSearchFormatter:
        quickSearchTextFormatter

    GalleryQuickSearchFormatter {
        id: quickSearchTextFormatter
        controller: root.controller
        controllerReady: root.controllerReady
        localQuickSearchEnabled: root.localQuickSearchEnabled
        matchColor: root.quickSearchMatchColor
        externalMatches: root.quickSearchMatches
    }

    // Keep the formatter reachable through the panel facade. Embedders and
    // focused interaction tests should not need to know which private helper
    // owns quick-search markup after the panel was decomposed.
    function quickSearchStyledText(value, entryId, sourceRuneOffset) {
        return quickSearchTextFormatter.styledText(
                    value, entryId, sourceRuneOffset)
    }

    readonly property GalleryPanelDiagnostics diagnostics: diagnosticsState
    GalleryPanelDiagnostics {
        id: diagnosticsState
        panel: root
    }
    readonly property GalleryDetailsSchema detailsSchema: detailsSchemaState
    GalleryDetailsSchema {
        id: detailsSchemaState
        panel: root
    }
    readonly property GalleryViewerTransitionSource viewerTransitionSource:
        viewerTransitionState
    GalleryViewerTransitionSource {
        id: viewerTransitionState
        panel: root
    }

    function singleItemDragRequested(modifiers) {
        const modifier = Qt.platform.os === "osx"
                ? Qt.MetaModifier : Qt.AltModifier
        return Boolean(modifiers & modifier)
    }

    function publishMetadataVisibleRange() {
        diagnosticsState.publishMetadataVisibleRange()
    }

    function benchmarkState(extra) {
        return diagnosticsState.state(extra)
    }

    function traceBenchmarkStage(stage, extra) {
        diagnosticsState.trace(stage, extra)
    }

    readonly property real detailsRowInset: metrics.detailsRowInset
    readonly property real detailsRowSpacing: metrics.detailsRowSpacing
    readonly property real detailsIconSlotSize: metrics.detailsIconSlotSize
    readonly property real detailsIconSize: metrics.detailsIconSize
    readonly property real detailsNameFontPixelSize:
        metrics.detailsNameFontPixelSize
    readonly property real detailsSecondaryFontPixelSize:
        metrics.detailsSecondaryFontPixelSize
    readonly property real detailsExtensionMinimumWidth:
        metrics.detailsExtensionMinimumWidth
    readonly property real detailsExtensionMaximumWidth:
        metrics.detailsExtensionMaximumWidth
    readonly property real detailsSizeColumnWidth:
        metrics.detailsSizeColumnWidth
    readonly property real detailsHeaderHeight:
        metrics.detailsHeaderHeight > 0
            ? metrics.detailsHeaderHeight : Math.max(30, density + 8)
    readonly property real detailsHeaderCellInset:
        metrics.detailsHeaderCellInset
    readonly property real detailsHeaderFontPixelSize:
        metrics.detailsHeaderFontPixelSize
    readonly property real detailsSeparatorVerticalMargin:
        metrics.detailsSeparatorVerticalMargin
    readonly property real detailsSeparatorWidth: metrics.detailsSeparatorWidth
    readonly property real detailsScrollBarWidth: metrics.detailsScrollBarWidth

    function nativePresentationMode() {
        return viewportState.nativePresentationMode()
    }
    function noteDensityChanged(finalChange) {
        viewportState.noteDensityChanged(finalChange)
    }

    function detailsColumn(role, fallbackTitle) {
        return detailsSchemaState.column(role, fallbackTitle)
    }

    function detailsColumns() {
        return detailsSchemaState.columns()
    }

    function sourceIndex(viewIndex) {
        return controller ? controller.sourceIndexAt(viewIndex) : -1
    }

    function currentTransitionItem() {
        return viewerTransitionState.currentItem()
    }

    function currentItemImageGeometry(targetItem) {
        return viewerTransitionState.imageGeometry(targetItem)
    }

    function currentItemImageSource() {
        return viewerTransitionState.imageSource()
    }

    function handlePointerPress(viewIndex, button, modifiers) {
        selectionState.handlePointerPress(viewIndex, button, modifiers)
    }

    function invertPanelSelection() {
        selectionState.invertPanelSelection()
    }

    function handlePointerDrag(panelX, panelY) {
        selectionState.handlePointerDrag(panelX, panelY)
    }

    function endPointerDrag() {
        selectionState.endPointerDrag()
    }

    // Fixed compact modes use two different coordinate systems for their
    // content. Columns delegates are pixel-snapped by BrickItem, while
    // Details deliberately keeps its fractional row pitch. A keyboard reveal
    // therefore has to land on the corresponding visual lattice; a raw
    // minimal reveal leaves a half-pixel phase behind after every boundary.
    function handlePanelMiddlePress(x, y, modifiers) {
        viewportState.handlePanelMiddlePress(x, y, modifiers)
    }
    function stepDensity(zoomIn) {
        viewportState.stepDensity(zoomIn)
    }
    function resetDensity(value) {
        viewportState.resetDensity(value)
    }
    function handlePanelMiddleRelease(x, y, modifiers) {
        viewportState.handlePanelMiddleRelease(x, y, modifiers)
    }
    function handlePanelWheel(pixelDeltaY, angleDeltaY, modifiers,
                              pixelDeltaX, angleDeltaX) {
        return viewportState.handlePanelWheel(
                    pixelDeltaY, angleDeltaY, modifiers,
                    pixelDeltaX, angleDeltaX)
    }
    function beginThumbnailPinch() {
        viewportState.beginThumbnailPinch()
    }
    function updateThumbnailPinch(scale) {
        viewportState.updateThumbnailPinch(scale)
    }
    function finishThumbnailPinch() {
        viewportState.finishThumbnailPinch()
    }
    function setPanelContentY(value, persist) {
        viewportState.setPanelContentY(value, persist)
    }
    function beginPresentationSwitch() {
        viewportState.beginPresentationSwitch()
    }
    function beginPresentationStateUpdate(switchingMode) {
        viewportState.beginPresentationStateUpdate(switchingMode)
    }

    function applyPresentationMode(requestedMode) {
        const value = String(requestedMode || "masonry")
        const normalized = value === "columns" || value === "details"
                || value === "grid" || value === "icons"
                ? value : "masonry"
        applyingPresentationMode = true
        try {
            if (presentationMode !== normalized)
                presentationMode = normalized
        } finally {
            applyingPresentationMode = false
        }
        const nativeMode = galleryViewport.nativePresentationMode(normalized)
        galleryViewport.applyPresentationState(
                    galleryLayout.presentationMode !== nativeMode,
                    normalized)
    }
    function endPresentationStateUpdate(publishVisibleRange) {
        viewportState.endPresentationStateUpdate(publishVisibleRange)
    }
    function restoreScrollOffset() {
        return viewportState.restoreScrollOffset()
    }
    function restoreScrollOrEnsureCursor() {
        viewportState.restoreScrollOrEnsureCursor()
    }
    function centerCurrentForPathChange() {
        return viewportState.centerCurrentForPathChange()
    }
    function restoreRememberedViewportForPathChange() {
        return viewportState.restoreRememberedViewportForPathChange()
    }
    function placeViewportForPathChange() {
        return viewportState.placeViewportForPathChange()
    }
    function schedulePathViewportPlacement(reason) {
        viewportState.schedulePathViewportPlacement(reason)
    }

    function scheduleViewportUpdate(ensureCursor) {
        viewportState.scheduleViewportUpdate(ensureCursor)
    }

    function selectIndex(viewIndex, openItem, deferCursorCommit,
                         autoRepeat) {
        navigationState.selectIndex(viewIndex, openItem, deferCursorCommit,
                                    autoRepeat)
    }
    function commitPendingCursor() {
        navigationState.commitPendingCursor()
    }
    function refreshPendingCursorCommit() {
        navigationState.refreshPendingCursorCommit()
    }
    function resetCurrentItemCenterX(index) {
        navigationState.resetCurrentItemCenterX(index)
    }
    function resetCurrentItemCenterY(index) {
        navigationState.resetCurrentItemCenterY(index)
    }
    function resetCurrentItemCenter(index) {
        navigationState.resetCurrentItemCenter(index)
    }

    function indexIntersectsViewport(index) {
        return cursorState.indexIntersectsViewport(index)
    }
    function nearestVisibleCursor(targetIndex) {
        return cursorState.nearestVisibleCursor(targetIndex)
    }
    function cursorAtViewportAnchor() {
        return cursorState.cursorAtViewportAnchor()
    }
    function updateVisualCursorForViewport() {
        cursorState.updateVisualCursorForViewport()
    }
    function cursorChromeNavigationSnapshot() {
        return cursorState.cursorChromeNavigationSnapshot()
    }
    function cursorChromeRectForIndex(index, plannedContentY) {
        return cursorState.cursorChromeRectForIndex(index, plannedContentY)
    }
    function startCursorChromeGeometry(startRect, targetRect, targetIndex) {
        return cursorState.startCursorChromeGeometry(
                    startRect, targetRect, targetIndex)
    }
    function startCursorChromeForNavigation(snapshot, targetIndex) {
        return cursorState.startCursorChromeForNavigation(snapshot, targetIndex)
    }
    function retargetCursorChromeAfterLayoutReset() {
        cursorState.retargetCursorChromeAfterLayoutReset()
    }
    function cancelCursorChromeTransition() {
        cursorState.cancelCursorChromeTransition()
    }
    function finishCursorChromeTransition() {
        cursorState.finishCursorChromeTransition()
    }
    function coordinateVisualCursor(targetIndex, previousIndex) {
        cursorState.coordinateVisualCursor(targetIndex, previousIndex)
    }

    function navigationTargetForKey(key, page) {
        return navigationState.navigationTargetForKey(key, page)
    }
    function moveCursor(index, preserveSelectionAnchor,
                        preserveHorizontalAnchor, deferCursorCommit,
                        preserveVerticalAnchor, keyboardRevealDirection) {
        navigationState.moveCursor(
                    index, preserveSelectionAnchor,
                    preserveHorizontalAnchor, deferCursorCommit,
                    preserveVerticalAnchor, keyboardRevealDirection)
    }

    function moveCursorWithSelection(index, togglePrevious,
                                     preserveHorizontalAnchor,
                                     deferCursorCommit,
                                     preserveVerticalAnchor,
                                     keyboardRevealDirection) {
        selectionState.moveCursorWithSelection(
                    index, togglePrevious, preserveHorizontalAnchor,
                    deferCursorCommit, preserveVerticalAnchor,
                    keyboardRevealDirection)
    }

    function beginKeyboardShiftSelection(anchorIndex, selectionAdds) {
        selectionState.beginKeyboardShiftSelection(anchorIndex, selectionAdds)
    }

    function togglePendingKeyboardSelection(index) {
        selectionState.togglePendingKeyboardSelection(index)
    }

    function effectiveEntrySelected(entryId, authoritativeSelected) {
        return selectionState.effectiveEntrySelected(entryId,
                                                     authoritativeSelected)
    }

    function reconcileAcknowledgedKeyboardSelection() {
        selectionState.reconcileAcknowledgedKeyboardSelection()
    }

    function clearPendingKeyboardSelection() {
        selectionState.clearPendingKeyboardSelection()
    }

    function finishKeyboardShiftSelection() {
        return selectionState.finishKeyboardShiftSelection()
    }

    function beginKeyboardToggleSelection(key) {
        selectionState.beginKeyboardToggleSelection(key)
    }

    function finishKeyboardToggleSelection() {
        return selectionState.finishKeyboardToggleSelection()
    }

    function finishKeyboardSelectionGesture() {
        return selectionState.finishKeyboardSelectionGesture()
    }

    function commitCursorAfterNavigation() {
        navigationState.commitCursorAfterNavigation()
    }

    function resetGridPageLattice() {
        navigationState.resetGridPageLattice()
    }

    function resetMasonryPageSequence() {
        navigationState.resetMasonryPageSequence()
    }

    function invalidateMasonryPageGeometry() {
        navigationState.invalidateMasonryPageGeometry()
    }

    function navigateViewportPage(direction, togglePrevious,
                                  deferCursorCommit) {
        return navigationState.navigateViewportPage(
                    direction, togglePrevious, deferCursorCommit)
    }

    function ensureCurrentVisible(animateScroll, keyboardRevealDirection) {
        navigationState.ensureCurrentVisible(
                    animateScroll, keyboardRevealDirection)
    }

    function ownsKey(event) {
        return inputState.ownsKey(event)
    }

    function ensureSessionPreviews() {
        reconciliationState.ensureSessionPreviews()
    }

    function handleLocalQuickSearchKey(event) {
        return inputState.handleLocalQuickSearchKey(event)
    }

    function resetControllerState() {
        reconciliationState.resetControllerState()
    }

    function scheduleCursorChromeLayoutRetarget() {
        motionState.cursorRetargetTimer.restart()
    }

    function scheduleThumbnailResizeDecode() {
        motionState.thumbnailResizeTimer.restart()
    }

    GalleryViewport {
        id: galleryViewport
        anchors.fill: parent
        panelRoot: root
        controller: root.controller
    }

    GalleryPanelOverlays {
        anchors.fill: parent
        controller: root.controller
        theme: root.theme
        layout: root.galleryLayout
        localQuickSearchEnabled: root.localQuickSearchEnabled
        scrollBarsReady: root.scrollBarsReady
        presentationMode: root.presentationMode
        detailsScrollBarWidth: root.detailsScrollBarWidth
        devicePixelRatio: root.devicePixelRatio
        onSeekRequested: contentOffset =>
            root.setPanelContentY(contentOffset, true)
    }
    Keys.onPressed: event => inputState.handlePressed(event)
    Keys.onReleased: event => inputState.handleReleased(event)

    onPresentationModeChanged: {
        // The requested mode is passed explicitly, so no dependent binding
        // has to settle before the native renderer can commit the complete
        // presentation state. This compatibility path therefore uses the
        // same synchronous transaction as typed host adapters.
        if (!applyingPresentationMode)
            applyPresentationMode(presentationMode)
    }
    Component.onCompleted: reconciliationState.initialize()
}
