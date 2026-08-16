import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

import ZoinGallery.Native 1.0

FocusScope {
    id: root
    // Masonry delegates are intentionally recycled beyond the viewport.
    // Embedded hosts place status/chrome immediately beside this item, so
    // keep both painting and pointer delivery inside the panel boundary.
    clip: true

    property var session
    property var theme: ({})
    // Geometry supplied by an embedding shell.  Keeping these values
    // explicit lets the reusable Details renderer match an existing file
    // list without reaching into that shell's context properties.
    property var metrics: ({})
    property var hostCapabilities: ({})
    property real devicePixelRatio: 1.0
    readonly property font iconLabelFont: iconLabelFontProbe.font
    // The public string API keeps embedders independent from the native enum
    // while the underlying renderer remains a single reusable C++ type.
    property string presentationMode: "masonry"
    property int columnCount: 2
    property var columnSchema: []
    // A host may keep its own column header outside the reusable viewport.
    // Standalone users retain the module-local header by default.
    property bool showDetailsHeader: true
    // Hosts that already split displayBaseName/displayExtension may align the
    // extension to the trailing edge of the name field. Standalone keeps the
    // established combined filename by default.
    property bool separateFileExtensions: false
    property alias thumbnailHeight: galleryLayout.targetHeight
    property alias density: galleryLayout.density
    property bool autoFocus: true
    property int selectionAnchorIndex: -1
    // Shift+navigation is a local transaction. Sending selection and cursor
    // actions for every autorepeat makes the semantic host trail the painted
    // cursor, so preview one add/remove range here and batch it on physical
    // Shift release.
    property var pendingKeyboardSelectionToggles: ({})
    property var awaitingKeyboardSelectionToggles: ({})
    property int keyboardSelectionVisualRevision: 0
    property bool keyboardShiftSelectionActive: false
    property int keyboardShiftSelectionAnchorIndex: -1
    property bool keyboardShiftSelectionAdds: true
    property int keyboardShiftSelectionFirst: -1
    property int keyboardShiftSelectionLast: -1
    property bool restoringScrollOffset: false
    property bool viewportUpdateEnsuresCursor: false
    property bool viewportUpdatePendingAfterScroll: false
    // A directory replacement resets the model and its contentY before the
    // authoritative cursor for the new path arrives. Keep Details unpainted
    // for that short transaction so it never exposes the top rows first.
    property bool pathViewportPlacementPending: false
    // applyExternalCatalog can remap a cursor with the same stable ID before
    // it announces the replacement catalog. That remap is provisional until
    // the host applies its authoritative state, so it must not complete the
    // path-placement transaction synchronously.
    property bool pathViewportCatalogReady: true
    // Benchmark instrumentation is deliberately opt-in.  Normal Gallery
    // navigation must not allocate diagnostic maps on every cursor change.
    // Embedders can enable this and forward benchmarkStage without coupling
    // the reusable panel to a particular tracing backend.
    property bool benchmarkTracingEnabled: false
    property bool presentationSwitchPending: false
    property real presentationSwitchCursorViewportY: Number.NaN
    property int thumbnailPinchStartHeight: 0
    property bool densityViewportTransaction: false
    property bool suppressScrollAnimationPersistence: false
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
    // Keep the rendered cursor coupled to the animated viewport. The session
    // cursor remains the authoritative navigation target immediately (so key
    // repeat and stable-ID commits never lose a move), while this index follows
    // the item crossing the preserved viewport anchor until the target arrives.
    // This supplies the visual half missing from MasonryMode's ensure-visible
    // contract: a cursor is never painted wholly behind the viewport clip.
    property int visualCursorIndex: -1
    property int pendingVisualCursorIndex: -1
    // Cursor chrome has its own viewport-space animation. The session cursor
    // and visual row identity remain authoritative immediately, while one
    // independent rectangle moves between their painted geometries without
    // being carried or recycled by a delegate.
    property bool cursorChromeTransitionActive: false
    property int cursorChromeTargetIndex: -1
    property real cursorChromeX: 0
    property real cursorChromeY: 0
    property real cursorChromeWidth: 0
    property real cursorChromeHeight: 0
    property real cursorChromeTargetX: 0
    property real cursorChromeTargetY: 0
    property real cursorChromeTargetWidth: 0
    property real cursorChromeTargetHeight: 0
    property real cursorChromeRadius: 0
    property real cursorChromeBorderWidth: 0
    property color cursorChromeFillColor: "transparent"
    property color cursorChromeBorderColor: "transparent"
    readonly property int cursorChromeCoveredIndex:
        cursorChromeTransitionActive
        ? galleryLayout.indexAtViewport(cursorChromeX + cursorChromeWidth / 2,
                                        cursorChromeY + cursorChromeHeight / 2)
        : -1
    readonly property rect cursorChromeRect:
        Qt.rect(cursorChromeX, cursorChromeY,
                cursorChromeWidth, cursorChromeHeight)
    readonly property rect cursorChromeTargetRect:
        Qt.rect(cursorChromeTargetX, cursorChromeTargetY,
                cursorChromeTargetWidth, cursorChromeTargetHeight)
    // The full-area viewer sets these while its image is animating to or from
    // the active tile.  Only the tile image is suppressed; panel chrome,
    // selection, and labels remain stable underneath the transition.
    property bool viewerTransitionActive: false
    property string viewerTransitionEntryId: ""
    // Embedded multi-panel hosts hide the navigation cursor on inactive
    // panels while keeping persistent multi-selection markers visible.
    property bool showCursor: true
    // The standalone shell supplies its established interaction surface here.
    // Embedded hosts leave this null and use the windowless built-in panel.
    property Item customContent: null

    signal activateRequested()
    // Auto-repeat navigation remains optimistic and local. The host receives
    // the desired cursor so it can mask older authoritative scenes, while the
    // expensive semantic round-trip may wait until repeat stops.
    signal cursorRequested(string entryId, int index, bool deferCommit)
    signal openRequested(string entryId, int index, bool isImage)
    signal selectionRequested(string mode, var entryIds)
    signal densityChangeRequested(string mode, real density, bool finalChange)
    signal sortRequested(string sortMode, bool contextMenu)
    signal benchmarkStage(string stage, var metadata)

    readonly property color backgroundColor:
        theme && theme.panelBackground !== undefined
            ? theme.panelBackground : "#17191d"
    readonly property color foregroundColor:
        theme && theme.text !== undefined ? theme.text : "#e8eaed"
    readonly property color mutedColor:
        theme && theme.mutedText !== undefined ? theme.mutedText : "#aeb4bc"
    readonly property color cursorColor:
        theme && theme.cursor !== undefined ? theme.cursor : "#4c8bf5"
    readonly property color selectionColor:
        theme && theme.selection !== undefined ? theme.selection : "#d8a31a"
    readonly property color cursorBackgroundColor:
        theme && theme.cursorBackground !== undefined
            ? theme.cursorBackground : cursorColor
    readonly property color cursorBorderColor:
        theme && theme.cursorBorder !== undefined
            ? theme.cursorBorder : Qt.lighter(cursorColor, 1.35)
    readonly property color markedBackgroundColor:
        theme && theme.markedBackground !== undefined
            ? theme.markedBackground : selectionColor
    readonly property color markedTextColor:
        theme && theme.markedText !== undefined
            ? theme.markedText : "#ffd43b"
    readonly property color directoryTextColor:
        theme && theme.directoryText !== undefined
            ? theme.directoryText : foregroundColor
    readonly property color folderIconColor:
        theme && theme.folderIcon !== undefined
            ? theme.folderIcon : mutedColor
    readonly property color separatorColor:
        theme && theme.separator !== undefined
            ? theme.separator : Qt.rgba(1, 1, 1, 0.12)
    readonly property color headerTextColor:
        theme && theme.headerText !== undefined
            ? theme.headerText : foregroundColor
    readonly property color headerHoverColor:
        theme && theme.controlHover !== undefined
            ? theme.controlHover : Qt.lighter(backgroundColor, 1.25)

    function metric(name, fallback) {
        if (!metrics || metrics[name] === undefined)
            return fallback
        const value = Number(metrics[name])
        return isFinite(value) ? value : fallback
    }

    // Return one self-contained placement snapshot so a real-application
    // benchmark can correlate model/path changes with the exact viewport
    // geometry that was visible to the reusable panel at that boundary.
    function benchmarkState(extra) {
        const count = galleryLayout ? galleryLayout.count : 0
        const index = session ? session.currentIndex : -1
        const geometry = galleryLayout && index >= 0 && index < count
                ? galleryLayout.indexGeometry(index)
                : Qt.rect(0, 0, 0, 0)
        const geometryValid = geometry.width > 0 && geometry.height > 0
        const horizontal = presentationMode === "columns"
        const viewportExtent = galleryLayout
                ? (horizontal ? galleryLayout.width : galleryLayout.height) : 0
        const contentExtent = galleryLayout ? galleryLayout.contentHeight : 0
        const maximum = Math.max(0, contentExtent - viewportExtent)
        const itemCenter = geometryValid
                ? (horizontal ? geometry.x + geometry.width / 2
                              : geometry.y + geometry.height / 2) : -1
        const target = geometryValid && viewportExtent > 0
                ? Math.max(0, Math.min(maximum,
                                      itemCenter - viewportExtent / 2)) : -1
        const contentY = galleryLayout ? galleryLayout.contentY : 0
        const placementMatchesTarget = count === 0
                || (target >= 0 && Math.abs(contentY - target) <= 0.51)
        var result = {
            "currentPath": session ? String(session.currentPath || "") : "",
            "catalogRevision": session
                    ? Number(session.catalogRevision || 0) : 0,
            "currentIndex": index,
            "cursorEntryId": session
                    ? String(session.cursorEntryId || "") : "",
            "presentationMode": String(presentationMode || ""),
            "nativePresentationMode": galleryLayout
                    ? Number(galleryLayout.presentationMode) : -1,
            "count": count,
            "contentY": contentY,
            "contentHeight": contentExtent,
            "viewportWidth": galleryLayout ? galleryLayout.width : 0,
            "viewportHeight": galleryLayout ? galleryLayout.height : 0,
            "viewportExtent": viewportExtent,
            "maximumContentY": maximum,
            "targetContentY": target,
            "placementMatchesTarget": placementMatchesTarget,
            "pathViewportPlacementPending": pathViewportPlacementPending,
            "placementTimerRunning": pathViewportPlacementTimer
                    ? pathViewportPlacementTimer.running : false,
            "geometryValid": geometryValid,
            "cursorX": geometry.x,
            "cursorY": geometry.y,
            "cursorWidth": geometry.width,
            "cursorHeight": geometry.height,
            "cursorViewportCenterDelta": geometryValid
                    ? itemCenter - contentY - viewportExtent / 2 : 0
        }
        if (extra) {
            const keys = Object.keys(extra)
            for (let keyIndex = 0; keyIndex < keys.length; ++keyIndex)
                result[keys[keyIndex]] = extra[keys[keyIndex]]
        }
        return result
    }

    function traceBenchmarkStage(stage, extra) {
        if (!benchmarkTracingEnabled)
            return
        benchmarkStage(stage, benchmarkState(extra || ({})))
    }

    readonly property real detailsRowInset:
        metric("detailsRowInset", 8)
    readonly property real detailsRowSpacing:
        metric("detailsRowSpacing", 8)
    readonly property real detailsIconSlotSize:
        metric("detailsIconSlotSize", 18)
    readonly property real detailsIconSize:
        metric("detailsIconSize", 16)
    readonly property real detailsNameFontPixelSize:
        metric("detailsNameFontPixelSize", 13)
    readonly property real detailsSecondaryFontPixelSize:
        metric("detailsSecondaryFontPixelSize", 12)
    readonly property real detailsExtensionMinimumWidth:
        metric("detailsExtensionMinimumWidth", 40)
    readonly property real detailsExtensionMaximumWidth:
        metric("detailsExtensionMaximumWidth", 80)
    readonly property real detailsSizeColumnWidth:
        metric("detailsSizeColumnWidth", 96)
    readonly property real detailsHeaderHeight:
        metric("detailsHeaderHeight", Math.max(30, density + 8))
    readonly property real detailsHeaderCellInset:
        metric("detailsHeaderCellInset", 8)
    readonly property real detailsHeaderFontPixelSize:
        metric("detailsHeaderFontPixelSize", 12)
    readonly property real detailsSeparatorVerticalMargin:
        metric("detailsSeparatorVerticalMargin", 6)
    readonly property real detailsSeparatorWidth:
        metric("detailsSeparatorWidth", 1)
    readonly property real detailsScrollBarWidth:
        metric("detailsScrollBarWidth", 16)

    function nativePresentationMode() {
        switch (presentationMode) {
        case "columns": return MasonryLayout.Columns
        case "details": return MasonryLayout.Details
        case "grid": return MasonryLayout.Grid
        case "icons": return MasonryLayout.Icons
        default: return MasonryLayout.Masonry
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
        densityViewportTransaction = true
        try {
            change()
        } finally {
            densityViewportTransaction = false
        }
        if (session) {
            session.panelScrollOffset = galleryLayout.contentY
            session.panelViewportCursorEntryId = session.cursorEntryId
        }
    }

    function detailsColumn(role, fallbackTitle) {
        const columns = columnSchema || []
        for (let index = 0; index < columns.length; ++index) {
            if ((columns[index].role || columns[index].id) === role)
                return columns[index]
        }
        return { role: role, title: fallbackTitle, sortMode: role }
    }

    function detailsColumns() {
        if (columnSchema && columnSchema.length > 0)
            return columnSchema
        return [
            { id: "name", role: "name", title: qsTr("Name"),
              width: 50, alignment: "left", sortMode: "name",
              sortable: true },
            { id: "size", role: "size", title: qsTr("Size"),
              width: 14, alignment: "right", sortMode: "size",
              sortable: true }
        ]
    }

    function sourceIndex(viewIndex) {
        return session ? session.sourceIndexAt(viewIndex) : -1
    }

    function currentTransitionItem() {
        if (!session || session.currentIndex < 0)
            return null
        const item = galleryLayout.currentItem
        if (!item || item.viewIndex !== session.currentIndex)
            return null
        return item
    }

    function currentItemImageGeometry(targetItem) {
        const item = currentTransitionItem()
        if (!item || !item.thumbnailItem || !targetItem)
            return Qt.rect(0, 0, 0, 0)
        const imageItem = item.thumbnailItem
        const topLeft = targetItem.mapFromItem(imageItem, 0, 0)
        const bottomRight = targetItem.mapFromItem(
                    imageItem, imageItem.width, imageItem.height)
        const geometry = Qt.rect(
                    Math.min(topLeft.x, bottomRight.x),
                    Math.min(topLeft.y, bottomRight.y),
                    Math.abs(bottomRight.x - topLeft.x),
                    Math.abs(bottomRight.y - topLeft.y))
        const panelTopLeft = targetItem.mapFromItem(root, 0, 0)
        const panelBottomRight = targetItem.mapFromItem(root, root.width,
                                                       root.height)
        const panelLeft = Math.min(panelTopLeft.x, panelBottomRight.x)
        const panelTop = Math.min(panelTopLeft.y, panelBottomRight.y)
        const panelRight = Math.max(panelTopLeft.x, panelBottomRight.x)
        const panelBottom = Math.max(panelTopLeft.y, panelBottomRight.y)
        // A recycled delegate can still exist just outside the viewport.  Do
        // not fly the viewer to an invisible tile in that case; the viewer
        // will use its fade fallback instead.
        if (geometry.width <= 1 || geometry.height <= 1
                || geometry.x + geometry.width <= panelLeft
                || geometry.y + geometry.height <= panelTop
                || geometry.x >= panelRight || geometry.y >= panelBottom) {
            return Qt.rect(0, 0, 0, 0)
        }
        return geometry
    }

    function currentItemImageSource() {
        const item = currentTransitionItem()
        if (!item || !item.thumbnailItem)
            return ""
        return item.thumbnailItem.source.toString()
    }

    function selectionIds(firstIndex, lastIndex) {
        const ids = []
        if (!session)
            return ids
        const first = Math.max(0, Math.min(firstIndex, lastIndex))
        const last = Math.min(galleryLayout.count - 1,
                              Math.max(firstIndex, lastIndex))
        for (let index = first; index <= last; ++index) {
            const id = session.entryIdAt(index)
            // f4 deliberately excludes its synthetic parent entry from
            // mouse selection, so embedded Gallery mirrors that contract.
            if (id !== "" && session.entryNameAt(index) !== "..")
                ids.push(id)
        }
        return ids
    }

    function handlePointerPress(viewIndex, button, modifiers) {
        if (!session || viewIndex < 0 || viewIndex >= galleryLayout.count)
            return
        // Direct manipulation always supersedes keyboard reveal chrome, even
        // during the zero-delay animation-settle hand-off.
        cancelCursorChromeTransition()
        const interruptedKeyboardScroll = panelScrollAnimation.running
        if (interruptedKeyboardScroll) {
            // A direct click supersedes the in-flight keyboard destination.
            // Freeze at the currently painted frame before choosing the
            // clicked item; otherwise the old animation can carry the new
            // cursor back out of view after coordinateVisualCursor() clears
            // the pending keyboard hand-off.
            viewportUpdateTimer.stop()
            viewportUpdateEnsuresCursor = false
            viewportUpdatePendingAfterScroll = false
            setPanelContentY(galleryLayout.contentY, false)
        }
        const previousIndex = session.currentIndex
        forceActiveFocus()

        if ((button & Qt.MiddleButton) !== 0) {
            const previousVisualIndex = visualCursorIndex
            selectionAnchorIndex = viewIndex
            localCursorNavigation = true
            selectIndex(viewIndex, true)
            localCursorNavigation = false
            ensureCurrentVisible()
            resetCurrentItemCenter(viewIndex)
            coordinateVisualCursor(viewIndex, previousVisualIndex)
            if (interruptedKeyboardScroll) {
                session.panelScrollOffset = galleryLayout.contentY
                session.panelViewportCursorEntryId = session.cursorEntryId
            }
            return
        }

        const previousVisualIndex = visualCursorIndex
        localCursorNavigation = true
        selectIndex(viewIndex, false)
        localCursorNavigation = false
        // Match MasonryMode.handleItemPressed(): selecting a partially visible
        // tile by mouse reveals the whole tile. The authoritative f4 cursor
        // acknowledgement is deliberately suppressed as local navigation, so
        // this must happen on the pointer path itself rather than waiting for
        // Session::currentIndexChanged.
        ensureCurrentVisible()
        resetCurrentItemCenter(viewIndex)
        coordinateVisualCursor(viewIndex, previousVisualIndex)
        if (interruptedKeyboardScroll) {
            session.panelScrollOffset = galleryLayout.contentY
            session.panelViewportCursorEntryId = session.cursorEntryId
        }
        const commandModifier = Boolean(modifiers &
            (Qt.ControlModifier | Qt.MetaModifier))
        const shiftModifier = Boolean(modifiers & Qt.ShiftModifier)

        if ((button & Qt.RightButton) !== 0) {
            selectionAnchorIndex = viewIndex
            const ids = selectionIds(viewIndex, viewIndex)
            if (ids.length > 0)
                selectionRequested("toggle", ids)
        } else if (shiftModifier) {
            const anchor = selectionAnchorIndex >= 0
                    ? selectionAnchorIndex
                    : (previousIndex >= 0 ? previousIndex : viewIndex)
            const ids = selectionIds(anchor, viewIndex)
            if (ids.length > 0)
                selectionRequested(commandModifier ? "add" : "replace", ids)
        } else if (commandModifier) {
            selectionAnchorIndex = viewIndex
            const ids = selectionIds(viewIndex, viewIndex)
            if (ids.length > 0)
                selectionRequested("toggle", ids)
        } else {
            selectionAnchorIndex = viewIndex
        }
    }

    function animatePanelScrollTo(targetY, quickScroll, keyboardReveal) {
        if (!keyboardReveal)
            cancelCursorChromeTransition()
        // An embedded session can queue a zero-delay offset restoration while
        // its initial catalog/layout settles.  Once the user scrolls, that
        // stale restore must not stop the original MasonryMode animation and
        // snap back to the pre-gesture offset.
        viewportUpdateTimer.stop()
        viewportUpdateEnsuresCursor = false
        viewportUpdatePendingAfterScroll = false
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const target = Math.max(
                    0, Math.min(maximum, targetY))
        const compactRows = presentationMode === "columns"
                || presentationMode === "details"
        if (compactRows) {
            // Compact file-list presentations intentionally follow the
            // classic panel contract: viewport changes are immediate.  Do
            // not run (or leave behind) the large-preview reveal animation.
            setPanelContentY(target, true)
            return target
        }
        panelScrollAnimation.from = galleryLayout.contentY
        panelScrollAnimation.to = target
        panelScrollAnimation.duration = quickScroll ? 15 : 150
        panelScrollAnimation.restart()
        return target
    }

    function scrollBy(deltaY, quickScroll, keyboardReveal) {
        const plannedContentY = panelScrollAnimation.running
                ? panelScrollAnimation.to : galleryLayout.contentY
        return animatePanelScrollTo(plannedContentY + deltaY,
                                    quickScroll, keyboardReveal)
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
        cancelCursorChromeTransition()
        if (modifiers & Qt.ControlModifier) {
            changeDensity(function() {
                if (delta < 0)
                    galleryLayout.zoomOut()
                else
                    galleryLayout.zoomIn()
            })
            noteDensityChanged(false)
        } else {
            // Columns consumes the vertical wheel/trackpad gesture as
            // horizontal movement through its column-major strip.
            scrollBy(-delta, macPlatform)
        }
        return true
    }

    function beginThumbnailPinch() {
        cancelCursorChromeTransition()
        thumbnailPinchStartHeight = galleryLayout.density
    }

    function updateThumbnailPinch(scale) {
        if (thumbnailPinchStartHeight <= 0)
            beginThumbnailPinch()
        changeDensity(function() {
            galleryLayout.density = Math.min(
                        maximumDensity(), Math.max(minimumDensity(),
                        thumbnailPinchStartHeight * scale))
        })
        noteDensityChanged(false)
    }

    function finishThumbnailPinch() {
        if (thumbnailPinchStartHeight > 0
                && thumbnailPinchStartHeight !== galleryLayout.density) {
            galleryLayout.reReadAndDecodeThumbnails()
            noteDensityChanged(true)
        }
        thumbnailPinchStartHeight = 0
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
        if (persist && session) {
            session.panelScrollOffset = galleryLayout.contentY
            session.panelViewportCursorEntryId = session.cursorEntryId
        }
    }

    function beginPresentationSwitch() {
        // Set this before the native presentationMode binding changes so the
        // very first rewrap cannot inherit an old BrickItem animation.
        if (!presentationSwitchPending && session
                && session.currentIndex >= 0) {
            const geometry = galleryLayout.indexGeometry(session.currentIndex)
            presentationSwitchCursorViewportY =
                    geometry.width > 0 && geometry.height > 0
                    ? geometry.y - galleryLayout.contentY : Number.NaN
        }
        presentationSwitchPending = true
        viewportUpdateTimer.stop()
        viewportUpdatePendingAfterScroll = false
        viewportUpdateEnsuresCursor = false
        suppressScrollAnimationPersistence = true
        panelScrollAnimation.stop()
        suppressScrollAnimationPersistence = false
        cursorChromeGeometryAnimation.stop()
        cancelCursorChromeTransition()
        presentationSwitchTimer.restart()
    }

    function restoreScrollOffset() {
        if (!session || galleryLayout.contentHeight <= 0)
            return false
        const maximum = Math.max(0, galleryLayout.contentHeight
                                 - (presentationMode === "columns"
                                    ? galleryLayout.width
                                    : galleryLayout.height))
        const target = Math.max(0, Math.min(maximum, session.panelScrollOffset))
        setPanelContentY(target, false)
        return target > 0
    }

    function restoreScrollOrEnsureCursor() {
        if (!session)
            return
        const viewportCursor = session.panelViewportCursorEntryId || ""
        if (viewportCursor !== "") {
            if (viewportCursor !== session.cursorEntryId) {
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
            session.panelViewportCursorEntryId = session.cursorEntryId
        }
    }

    function centerCurrentForPathChange() {
        traceBenchmarkStage("placement.center.attempt", {})
        if (!pathViewportPlacementPending || !session) {
            traceBenchmarkStage("placement.center.result", {
                "success": false,
                "reason": !session ? "missing-session" : "not-pending"
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
        const index = session.currentIndex
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
        visualCursorIndex = index
        pendingVisualCursorIndex = -1
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
        if (!pathViewportPlacementPending || !session
                || !pathViewportCatalogReady
                || typeof session.panelViewportStateAvailable === "undefined"
                || !session.panelViewportStateAvailable)
            return false
        if (galleryLayout.height <= 0)
            return false
        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const target = Math.max(
                    0, Math.min(maximum, session.panelScrollOffset))
        setPanelContentY(target, false)
        pathViewportPlacementTimer.stop()
        pathViewportPlacementPending = false
        visualCursorIndex = session.currentIndex
        pendingVisualCursorIndex = -1
        resetCurrentItemCenter(visualCursorIndex)
        traceBenchmarkStage("placement.center.result", {
            "success": true,
            "reason": "restored-path-viewport",
            "requestedContentY": target,
            "appliedContentY": galleryLayout.contentY
        })
        return true
    }

    function placeViewportForPathChange() {
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

    Timer {
        id: pathViewportPlacementTimer
        objectName: "galleryPathViewportPlacementTimer"
        interval: 0
        repeat: false
        onTriggered: {
            root.traceBenchmarkStage("placement.timer.triggered", {})
            root.placeViewportForPathChange()
        }
    }

    Timer {
        id: viewportUpdateTimer
        interval: 0
        onTriggered: {
            const ensureCursor = root.viewportUpdateEnsuresCursor
            root.viewportUpdateEnsuresCursor = false
            if (ensureCursor)
                root.ensureCurrentVisible()
            else
                root.restoreScrollOrEnsureCursor()
        }
    }

    Timer {
        id: presentationSwitchTimer
        objectName: "galleryPresentationSwitchTimer"
        interval: 0
        repeat: false
        onTriggered: {
            if (!root.presentationSwitchPending
                    || galleryLayout.presentationMode
                       !== root.nativePresentationMode())
                return
            root.presentationSwitchPending = false
            viewportUpdateTimer.stop()
            root.viewportUpdatePendingAfterScroll = false
            root.viewportUpdateEnsuresCursor = false
            // MasonryLayout already reveals the stable current index during
            // its synchronous rewrap. Re-run the public reveal contract after
            // all declarative geometry bindings have settled, still without
            // animation, and persist this mode's meaningful offset rather
            // than restoring the previous mode's numeric contentY.
            const currentIndex = root.session
                    ? root.session.currentIndex : -1
            const geometry = currentIndex >= 0
                    ? galleryLayout.indexGeometry(currentIndex)
                    : Qt.rect(0, 0, 0, 0)
            if (galleryLayout.presentationMode !== MasonryLayout.Columns
                    && geometry.width > 0 && geometry.height > 0
                    && Number.isFinite(
                        root.presentationSwitchCursorViewportY)) {
                const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
                const desired = geometry.y
                        - root.presentationSwitchCursorViewportY
                const firstVisible = Math.min(
                    geometry.y, geometry.y + geometry.height
                                - galleryLayout.height)
                const lastVisible = Math.max(
                    geometry.y, geometry.y + geometry.height
                                - galleryLayout.height)
                root.setPanelContentY(Math.max(0, Math.min(maximum,
                    Math.max(firstVisible, Math.min(lastVisible, desired)))),
                    true)
            } else {
                root.ensureCurrentVisible(false)
            }
            root.presentationSwitchCursorViewportY = Number.NaN
            root.visualCursorIndex = root.session
                    ? root.session.currentIndex : -1
            root.pendingVisualCursorIndex = -1
            root.resetCurrentItemCenter(root.visualCursorIndex)
        }
    }

    // Key auto-repeat can outpace a full f4 semantic-scene round-trip for a
    // large catalog. Keep movement local while repeat is active and commit
    // the latest stable ID on key release (or shortly after repeat stops if a
    // platform loses the release event).
    Timer {
        id: cursorCommitTimer
        // Key release is the normal commit path. This comparatively long
        // watchdog only covers a platform/window-focus transition that drops
        // the release event; native repeat events continuously restart it.
        interval: 5000
        repeat: false
        onTriggered: {
            root.finishKeyboardShiftSelection()
            root.commitPendingCursor()
        }
    }

    // A final stable-ID cursor commit asks an embedded host to rebuild and
    // serialize its semantic scene. Do that after the last scroll frame, not
    // in the middle of the 150 ms reveal. A zero-delay recheck also filters
    // the transient running=false emitted by NumberAnimation.restart().
    Timer {
        id: cursorCommitAfterScrollTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (root.cursorCommitAfterScroll
                    && !root.navigationKeyHeld
                    && !panelScrollAnimation.running
                    && !cursorChromeGeometryAnimation.running) {
                root.commitPendingCursor()
            }
        }
    }

    // Preview rectangles can change when the f4 splitter moves (masonry rows,
    // uniform grid cells and icon tiles all depend on the available width).
    // Wait until the drag settles, then replace only tiles whose existing
    // decode no longer covers the exact DPR-scaled rect. Fixed layouts plan a
    // bounded visible/overscan window, so this remains cheap for large folders.
    Timer {
        id: thumbnailResizeDecodeTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (root.session && !root.customContent
                    && galleryLayout.width > 0 && galleryLayout.count > 0) {
                galleryLayout.reReadAndDecodeThumbnails()
            }
        }
    }

    Timer {
        id: densityCommitTimer
        interval: 180
        repeat: false
        onTriggered: root.noteDensityChanged(true)
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

    function selectIndex(viewIndex, openItem, deferCursorCommit) {
        if (!session || viewIndex < 0 || viewIndex >= galleryLayout.count)
            return
        session.activateIndex(viewIndex)
        if (openItem) {
            openRequested(session.entryIdAt(viewIndex), sourceIndex(viewIndex),
                          session.isImageAt(viewIndex))
        } else {
            activateRequested()
            cursorRequested(session.entryIdAt(viewIndex), sourceIndex(viewIndex),
                            Boolean(deferCursorCommit))
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
        if (!cursorCommitPending || !session || session.currentIndex < 0)
            return
        cursorCommitPending = false
        cursorCommitTimer.stop()
        cursorRequested(session.entryIdAt(session.currentIndex),
                        sourceIndex(session.currentIndex), false)
    }

    function refreshPendingCursorCommit() {
        if (!cursorCommitPending || !session || session.currentIndex < 0)
            return
        // A held key can legitimately produce a no-op repeat while the
        // masonry layout is between rows or after the cursor reaches an edge.
        // Keep both the local and host-owned lost-release watchdogs alive so
        // neither one commits a full f4 scene while the key is still down.
        cursorCommitTimer.restart()
        cursorRequested(session.entryIdAt(session.currentIndex),
                        sourceIndex(session.currentIndex), true)
    }

    function resetCurrentItemCenterX(index) {
        if (!session || index < 0 || index >= galleryLayout.count) {
            currentItemCenterX = -1
            return
        }
        const geometry = galleryLayout.indexGeometry(index)
        currentItemCenterX = geometry.width > 0
                ? geometry.x + geometry.width / 2 : -1
    }

    function resetCurrentItemCenterY(index) {
        if (!session || index < 0 || index >= galleryLayout.count) {
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

    function indexIntersectsViewport(index) {
        if (index < 0 || index >= galleryLayout.count
                || galleryLayout.height <= 0)
            return false
        const geometry = galleryLayout.indexGeometry(index)
        if (geometry.width <= 0 || geometry.height <= 0)
            return false
        if (presentationMode === "columns") {
            const left = galleryLayout.contentY
            const right = left + galleryLayout.width
            return geometry.x < right
                    && geometry.x + geometry.width > left
        }
        const top = galleryLayout.contentY
        const bottom = top + galleryLayout.height
        // Edge contact alone is still completely clipped. Require a positive
        // painted area, matching what the user can actually see.
        return geometry.y < bottom
                && geometry.y + geometry.height > top
    }

    function nearestVisibleCursor(targetIndex) {
        const visible = galleryLayout.visibleIndexes || []
        const target = galleryLayout.indexGeometry(targetIndex)
        const targetY = target.height > 0
                ? target.y + target.height / 2 : galleryLayout.contentY
        const anchorX = currentItemCenterX >= 0
                ? currentItemCenterX : galleryLayout.width / 2
        let best = -1
        let bestScore = Number.MAX_VALUE
        for (let offset = 0; offset < visible.length; ++offset) {
            const index = Number(visible[offset])
            if (!indexIntersectsViewport(index))
                continue
            const geometry = galleryLayout.indexGeometry(index)
            // Prefer the row nearest the authoritative target, then retain the
            // user's horizontal navigation anchor within that row.
            const score = Math.abs(geometry.y + geometry.height / 2 - targetY)
                    * Math.max(1, galleryLayout.width * 2)
                    + Math.abs(geometry.x + geometry.width / 2 - anchorX)
            if (score < bestScore) {
                bestScore = score
                best = index
            }
        }
        return best
    }

    function cursorAtViewportAnchor() {
        if (currentItemCenterY < 0 || galleryLayout.count <= 0)
            return -1
        const anchorX = currentItemCenterX >= 0
                ? currentItemCenterX : galleryLayout.width / 2
        const viewportY = Math.max(
                    0, Math.min(galleryLayout.height - 0.01,
                                currentItemCenterY))
        let index = presentationMode === "columns"
                ? galleryLayout.indexAtViewport(anchorX, viewportY)
                : galleryLayout.indexAt(anchorX,
                        galleryLayout.contentY + viewportY)
        if (index < 0 && galleryLayout.listView)
            index = galleryLayout.indexAt(0, anchorY)
        return index
    }

    function updateVisualCursorForViewport() {
        if (!session || galleryLayout.count <= 0) {
            visualCursorIndex = -1
            pendingVisualCursorIndex = -1
            return
        }
        const target = pendingVisualCursorIndex >= 0
                ? pendingVisualCursorIndex : session.currentIndex
        if (pendingVisualCursorIndex >= 0) {
            // At the settled endpoint the authoritative target wins even if
            // pixel rounding makes indexAt(anchor) resolve an adjacent cell.
            if (!panelScrollAnimation.running
                    && indexIntersectsViewport(target)) {
                visualCursorIndex = target
                pendingVisualCursorIndex = -1
                return
            }
            // Preserve the same viewport-relative cursor anchor used by
            // MasonryMode navigation. As contentY advances, the highlight is
            // handed to the item currently crossing that anchor. This keeps a
            // visible cursor throughout Page navigation and repeated reveals,
            // even when old and final target visibility intervals do not
            // overlap.
            const anchored = cursorAtViewportAnchor()
            if (anchored >= 0 && indexIntersectsViewport(anchored)) {
                visualCursorIndex = anchored
                if (anchored === target)
                    pendingVisualCursorIndex = -1
                return
            }
        } else {
            if (indexIntersectsViewport(target))
                visualCursorIndex = target
            // Ordinary wheel/scrollbar movement is allowed to move the
            // authoritative cursor out of view. Only an active keyboard
            // reveal may paint intermediate cursor identities.
            return
        }
        if (indexIntersectsViewport(visualCursorIndex))
            return
        const replacement = nearestVisibleCursor(target)
        if (replacement >= 0)
            visualCursorIndex = replacement
    }

    function cursorChromeMargin() {
        if (presentationMode === "details")
            return 0
        if (presentationMode === "columns")
            return 1
        return 2
    }

    function cursorChromeModeRadius() {
        return presentationMode === "details"
                || presentationMode === "columns" ? 4 : 6
    }

    function cursorChromeRectForIndex(index, plannedContentY) {
        if (index < 0 || index >= galleryLayout.count)
            return Qt.rect(0, 0, 0, 0)
        const geometry = galleryLayout.indexGeometry(index)
        if (geometry.width <= 0 || geometry.height <= 0)
            return Qt.rect(0, 0, 0, 0)
        const margin = cursorChromeMargin()
        return Qt.rect(galleryLayout.paddingLeft + geometry.x
                       - (presentationMode === "columns"
                          ? plannedContentY : 0) + margin,
                       geometry.y - (presentationMode === "columns"
                                     ? 0 : plannedContentY) + margin,
                       Math.max(0, geometry.width - margin * 2),
                       Math.max(0, geometry.height - margin * 2))
    }

    function cursorChromeRectIsValid(rect) {
        return rect && rect.width > 0 && rect.height > 0
    }

    function cursorChromeNavigationSnapshot() {
        const plannedContentY = panelScrollAnimation.running
                ? Number(panelScrollAnimation.to) : galleryLayout.contentY
        if (cursorChromeTransitionActive) {
            return {
                valid: true,
                active: true,
                plannedContentY: plannedContentY,
                windowTopIndex: galleryLayout.windowTopIndex,
                rect: Qt.rect(cursorChromeX, cursorChromeY,
                              cursorChromeWidth, cursorChromeHeight)
            }
        }
        const index = visualCursorIndex >= 0
                ? visualCursorIndex : (session ? session.currentIndex : -1)
        const rect = cursorChromeRectForIndex(index, galleryLayout.contentY)
        return {
            valid: cursorChromeRectIsValid(rect),
            active: false,
            plannedContentY: plannedContentY,
            windowTopIndex: galleryLayout.windowTopIndex,
            rect: rect
        }
    }

    function cursorChromeStyleForIndex(index) {
        if (presentationMode !== "details") {
            return {
                fill: cursorColor,
                border: Qt.lighter(cursorColor, 1.35),
                borderWidth: 1,
                radius: cursorChromeModeRadius()
            }
        }
        const selected = session && session.isSelectedAt(index)
        const style = session ? session.highlightStyleAt(index) : ({})
        const patch = selected
                ? (style.selectedCursor || ({}))
                : (style.cursor || ({}))
        return {
            fill: patch.background || cursorBackgroundColor,
            border: cursorBorderColor,
            borderWidth: 1,
            radius: 4
        }
    }

    function startCursorChromeGeometry(startRect, targetRect, targetIndex) {
        if (!cursorChromeRectIsValid(startRect)
                || !cursorChromeRectIsValid(targetRect) || !showCursor)
            return false
        cursorChromeFinalizeTimer.stop()
        cursorChromeGeometryAnimation.stop()
        cursorChromeX = startRect.x
        cursorChromeY = startRect.y
        cursorChromeWidth = startRect.width
        cursorChromeHeight = startRect.height
        cursorChromeTargetX = targetRect.x
        cursorChromeTargetY = targetRect.y
        cursorChromeTargetWidth = targetRect.width
        cursorChromeTargetHeight = targetRect.height
        cursorChromeTargetIndex = targetIndex
        const style = cursorChromeStyleForIndex(targetIndex)
        cursorChromeFillColor = style.fill
        cursorChromeBorderColor = style.border
        cursorChromeBorderWidth = style.borderWidth
        cursorChromeRadius = style.radius
        cursorChromeTransitionActive = true

        cursorChromeXAnimation.from = startRect.x
        cursorChromeXAnimation.to = targetRect.x
        cursorChromeYAnimation.from = startRect.y
        cursorChromeYAnimation.to = targetRect.y
        cursorChromeWidthAnimation.from = startRect.width
        cursorChromeWidthAnimation.to = targetRect.width
        cursorChromeHeightAnimation.from = startRect.height
        cursorChromeHeightAnimation.to = targetRect.height
        cursorChromeGeometryAnimation.restart()
        return true
    }

    function startCursorChromeForNavigation(snapshot, targetIndex) {
        if (!snapshot || !snapshot.valid || !showCursor || !session
                || targetIndex < 0 || targetIndex >= galleryLayout.count)
            return false
        if (presentationMode === "columns"
                || presentationMode === "details") {
            // Compact row presentations intentionally make both viewport and
            // cursor movement atomic.  In particular, PageUp/PageDown must
            // not leave the independent cursor-chrome layer gliding over an
            // already-settled page.
            cancelCursorChromeTransition()
            pendingVisualCursorIndex = -1
            visualCursorIndex = targetIndex
            return false
        }
        const plannedContentY = panelScrollAnimation.running
                ? Number(panelScrollAnimation.to) : galleryLayout.contentY
        const viewportChanged = Math.abs(
                    plannedContentY - Number(snapshot.plannedContentY)) > 0.01
                || galleryLayout.windowTopIndex
                   !== Number(snapshot.windowTopIndex)
        if (!snapshot.active && !viewportChanged)
            return false
        const targetRect = cursorChromeRectForIndex(
                    targetIndex, plannedContentY)
        const startRect = snapshot.active
                ? Qt.rect(cursorChromeX, cursorChromeY,
                          cursorChromeWidth, cursorChromeHeight)
                : snapshot.rect
        return startCursorChromeGeometry(startRect, targetRect, targetIndex)
    }

    function retargetCursorChromeAfterLayoutReset() {
        if (!cursorChromeTransitionActive || !session
                || cursorChromeTargetIndex < 0)
            return
        const plannedContentY = panelScrollAnimation.running
                ? Number(panelScrollAnimation.to) : galleryLayout.contentY
        const targetRect = cursorChromeRectForIndex(
                    cursorChromeTargetIndex, plannedContentY)
        if (!cursorChromeRectIsValid(targetRect)) {
            cancelCursorChromeTransition()
            return
        }
        const sameTarget = Math.abs(Number(cursorChromeXAnimation.to)
                                    - targetRect.x) < 0.01
                && Math.abs(Number(cursorChromeYAnimation.to)
                            - targetRect.y) < 0.01
                && Math.abs(Number(cursorChromeWidthAnimation.to)
                            - targetRect.width) < 0.01
                && Math.abs(Number(cursorChromeHeightAnimation.to)
                            - targetRect.height) < 0.01
        if (!sameTarget) {
            startCursorChromeGeometry(
                Qt.rect(cursorChromeX, cursorChromeY,
                        cursorChromeWidth, cursorChromeHeight),
                targetRect, cursorChromeTargetIndex)
        }
    }

    function cancelCursorChromeTransition() {
        const wasActive = cursorChromeTransitionActive
        resetGridPageLattice()
        resetMasonryPageSequence()
        cursorChromeFinalizeTimer.stop()
        cursorChromeLayoutRetargetTimer.stop()
        cursorChromeGeometryAnimation.stop()
        cursorChromeTransitionActive = false
        cursorChromeTargetIndex = -1
        if (wasActive && session) {
            // Once the independent keyboard transition is abandoned, no
            // intermediate anchor identity may leak into ordinary wheel,
            // scrollbar or focus semantics. The logical cursor owns its row
            // again (and is allowed to be outside the manually moved viewport).
            pendingVisualCursorIndex = -1
            visualCursorIndex = session.currentIndex
        }
    }

    function finishCursorChromeTransition() {
        if (!cursorChromeTransitionActive)
            return
        // Restore the settled delegate cursor before withdrawing both chrome
        // layers. The scene graph observes these changes atomically.
        updateVisualCursorForViewport()
        cursorChromeTransitionActive = false
        cursorChromeTargetIndex = -1
    }

    function coordinateVisualCursor(targetIndex, previousIndex) {
        if (!session || targetIndex < 0
                || targetIndex >= galleryLayout.count) {
            visualCursorIndex = -1
            pendingVisualCursorIndex = -1
            return
        }
        if (indexIntersectsViewport(targetIndex)) {
            visualCursorIndex = targetIndex
            pendingVisualCursorIndex = -1
            return
        }
        pendingVisualCursorIndex = targetIndex
        if (!indexIntersectsViewport(visualCursorIndex)
                && indexIntersectsViewport(previousIndex)) {
            visualCursorIndex = previousIndex
        }
        updateVisualCursorForViewport()
    }

    // Preserve the original MasonryMode navigation contract. Vertical moves
    // retain their X anchor and make one native indexAt() query immediately
    // above or below the current tile; the old embedded implementation made
    // one QML-to-C++ indexGeometry() call for every catalog entry per repeat.
    function verticalIndex(direction) {
        if (!session || galleryLayout.currentIndex < 0)
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
            return MasonryLayout.NavigateLeft
        if (key === Qt.Key_Right)
            return MasonryLayout.NavigateRight
        if (key === Qt.Key_Up)
            return MasonryLayout.NavigateUp
        return MasonryLayout.NavigateDown
    }

    function navigationTargetForKey(key, page) {
        if (!session || session.currentIndex < 0)
            return -1
        // Masonry's vertical navigation deliberately retains the X center
        // chosen by the last pointer/horizontal move.  A short intervening
        // row must not replace that anchor with its sole tile's center: the
        // following row is still probed at the user's original column. Fixed
        // modes own their navigation in MasonryLayout and bypass this branch.
        if (!page
                && galleryLayout.presentationMode === MasonryLayout.Masonry
                && (key === Qt.Key_Up || key === Qt.Key_Down)) {
            return root.verticalIndex(key === Qt.Key_Up ? -1 : 1)
        }
        const result = galleryLayout.navigationTarget(
                         session.currentIndex,
                         navigationDirectionForKey(key), Boolean(page))
        if (!result)
            return -1
        const nextTop = Number(result.windowTopIndex)
        // Columns is a continuous horizontal strip. moveCursor() calls
        // ensureCurrentVisible(), which leaves an already-visible target
        // untouched and shifts by one column only when the cursor crosses a
        // viewport edge. Pre-setting windowTopIndex here made first->second
        // column navigation scroll even in a three-column viewport.
        if (galleryLayout.presentationMode !== MasonryLayout.Columns
                && nextTop >= 0
                && nextTop !== galleryLayout.windowTopIndex)
            galleryLayout.windowTopIndex = nextTop
        return Number(result.targetIndex)
    }

    function moveCursor(index, preserveSelectionAnchor,
                        preserveHorizontalAnchor, deferCursorCommit,
                        preserveVerticalAnchor) {
        if (!session || galleryLayout.count === 0)
            return
        const bounded = Math.max(0, Math.min(galleryLayout.count - 1, index))
        const previousVisualIndex = visualCursorIndex >= 0
                ? visualCursorIndex : session.currentIndex
        if (!preserveSelectionAnchor)
            selectionAnchorIndex = bounded
        localCursorNavigation = true
        selectIndex(bounded, false, deferCursorCommit)
        localCursorNavigation = false
        if (!preserveHorizontalAnchor)
            resetCurrentItemCenterX(bounded)
        // Original MasonryMode keeps both viewport-relative anchors for an
        // interior PageUp/PageDown. Every ordinary move (including Up/Down,
        // which preserves only X) reveals the item and adopts its resulting Y.
        if (!preserveVerticalAnchor) {
            ensureCurrentVisible()
            resetCurrentItemCenterY(bounded)
        }
        coordinateVisualCursor(bounded, previousVisualIndex)
    }

    function moveCursorWithSelection(index, togglePrevious,
                                     preserveHorizontalAnchor,
                                     deferCursorCommit,
                                     preserveVerticalAnchor) {
        if (!session || galleryLayout.count === 0)
            return
        const previousIndex = session.currentIndex
        const bounded = Math.max(0, Math.min(galleryLayout.count - 1, index))
        if (togglePrevious)
            beginKeyboardShiftSelection(previousIndex)
        if (bounded === previousIndex) {
            if (togglePrevious)
                updateKeyboardShiftSelection(bounded)
            if (!preserveHorizontalAnchor)
                resetCurrentItemCenterX(previousIndex)
            if (!preserveVerticalAnchor) {
                ensureCurrentVisible()
                resetCurrentItemCenterY(previousIndex)
            }
            return
        }
        moveCursor(bounded, togglePrevious, preserveHorizontalAnchor,
                   deferCursorCommit, preserveVerticalAnchor)
        if (togglePrevious)
            updateKeyboardShiftSelection(bounded)
    }

    function toggleMapContains(map, entryId) {
        return Boolean(map && map["$" + entryId] !== undefined)
    }

    function beginKeyboardShiftSelection(anchorIndex) {
        if (keyboardShiftSelectionActive)
            return
        keyboardShiftSelectionActive = true
        keyboardShiftSelectionAnchorIndex = anchorIndex
        // Match the original range-preview contract: the anchor's state fixes
        // the operation for the entire physical Shift hold. Starting on an
        // unselected item adds the range; starting on a selected item removes
        // it, even if the cursor later reverses direction.
        keyboardShiftSelectionAdds = !session.isSelectedAt(anchorIndex)
        keyboardShiftSelectionFirst = -1
        keyboardShiftSelectionLast = -1
        pendingKeyboardSelectionToggles = ({})
    }

    function updateKeyboardShiftSelection(targetIndex) {
        if (!keyboardShiftSelectionActive || !session)
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

        const pending = pendingKeyboardSelectionToggles
        const oldFirst = keyboardShiftSelectionFirst
        const oldLast = keyboardShiftSelectionLast
        if (oldFirst >= 0) {
            for (let index = oldFirst; index <= oldLast; ++index) {
                if (first >= 0 && index >= first && index <= last)
                    continue
                const id = session.entryIdAt(index)
                delete pending["$" + id]
            }
        }
        if (first >= 0) {
            for (let index = first; index <= last; ++index) {
                if (oldFirst >= 0 && index >= oldFirst && index <= oldLast)
                    continue
                const id = session.entryIdAt(index)
                if (id !== "" && session.entryNameAt(index) !== "..")
                    pending["$" + id] = {
                        entryId: id,
                        desired: keyboardShiftSelectionAdds
                    }
            }
        }
        keyboardShiftSelectionFirst = first
        keyboardShiftSelectionLast = last
        keyboardSelectionVisualRevision++
        cursorCommitTimer.restart()
    }

    function effectiveEntrySelected(entryId, authoritativeSelected) {
        // Referencing both maps makes delegate bindings react immediately to
        // local toggles and to acknowledgement cleanup.
        const visualRevision = keyboardSelectionVisualRevision
        const key = "$" + entryId
        const pending = pendingKeyboardSelectionToggles[key]
        if (pending !== undefined)
            return Boolean(pending.desired)
        const awaiting = awaitingKeyboardSelectionToggles[key]
        if (awaiting !== undefined)
            return Boolean(awaiting.desired)
        return Boolean(authoritativeSelected)
    }

    function commitPendingKeyboardSelection() {
        const keys = Object.keys(pendingKeyboardSelectionToggles)
        if (keys.length === 0)
            return
        const ids = []
        const awaiting = Object.assign({}, awaitingKeyboardSelectionToggles)
        for (let i = 0; i < keys.length; ++i) {
            const intent = pendingKeyboardSelectionToggles[keys[i]]
            ids.push(intent.entryId)
            awaiting[keys[i]] = intent
        }
        pendingKeyboardSelectionToggles = ({})
        awaitingKeyboardSelectionToggles = awaiting
        keyboardSelectionVisualRevision++
        selectionRequested(keyboardShiftSelectionAdds ? "add" : "remove", ids)
    }

    function reconcileAcknowledgedKeyboardSelection() {
        const keys = Object.keys(awaitingKeyboardSelectionToggles)
        const remaining = ({})
        for (let i = 0; i < keys.length; ++i) {
            const intent = awaitingKeyboardSelectionToggles[keys[i]]
            const index = session ? session.indexForEntryId(intent.entryId) : -1
            if (index >= 0
                    && session.isSelectedAt(index) !== Boolean(intent.desired))
                remaining[keys[i]] = intent
        }
        awaitingKeyboardSelectionToggles = remaining
        keyboardSelectionVisualRevision++
    }

    function clearPendingKeyboardSelection() {
        pendingKeyboardSelectionToggles = ({})
        awaitingKeyboardSelectionToggles = ({})
        keyboardSelectionVisualRevision++
        keyboardShiftSelectionActive = false
        keyboardShiftSelectionAnchorIndex = -1
        keyboardShiftSelectionAdds = true
        keyboardShiftSelectionFirst = -1
        keyboardShiftSelectionLast = -1
    }

    function finishKeyboardShiftSelection() {
        if (!keyboardShiftSelectionActive)
            return
        commitPendingKeyboardSelection()
        keyboardShiftSelectionActive = false
        keyboardShiftSelectionAnchorIndex = -1
        keyboardShiftSelectionAdds = true
        keyboardShiftSelectionFirst = -1
        keyboardShiftSelectionLast = -1
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
        if (galleryLayout.presentationMode !== MasonryLayout.Masonry
                || !stalePageScroll
                || (!panelScrollAnimation.running
                    && !cursorChromeTransitionActive))
            return
        // Never let an old target keep writing contentY after the row bands or
        // viewport extent which defined it have changed.
        suppressScrollAnimationPersistence = true
        panelScrollAnimation.stop()
        suppressScrollAnimationPersistence = false
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
        if (!session || galleryLayout.count <= 0
                || session.currentIndex < 0 || galleryLayout.height <= 0)
            return -1

        if (currentItemCenterX < 0 || currentItemCenterY < 0)
            resetCurrentItemCenter(session.currentIndex)

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
                    session.currentIndex)
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
                    0, session.currentIndex % columns)
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

    function navigateMasonryViewportPage(direction, togglePrevious,
                                         deferCursorCommit) {
        if (!session || galleryLayout.count <= 0
                || session.currentIndex < 0 || galleryLayout.height <= 0)
            return -1

        if (currentItemCenterX < 0 || currentItemCenterY < 0)
            resetCurrentItemCenter(session.currentIndex)

        const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
        const rawPlannedContentY = panelScrollAnimation.running
                ? Number(panelScrollAnimation.to) : galleryLayout.contentY
        const plannedContentY = Math.max(
                    0, Math.min(maximum, rawPlannedContentY))

        // Any externally changed viewport starts a fresh reversible sequence.
        // Native key repeats use animation.to, so an in-flight page still
        // matches its stored node and is deliberately retained.
        let currentNode = masonryPageNode(masonryPageOrdinal)
        if (!currentNode
                || Math.abs(Number(currentNode.contentY)
                            - plannedContentY) > 0.51) {
            resetMasonryPageSequence()
            storeMasonryPageNode(0, {
                contentY: plannedContentY,
                targetIndex: session.currentIndex,
                anchorX: currentItemCenterX,
                anchorY: currentItemCenterY,
                hitEdge: false
            })
            currentNode = masonryPageNode(0)
        }

        const nextOrdinal = masonryPageOrdinal + (direction < 0 ? -1 : 1)
        let targetNode = masonryPageNode(nextOrdinal)
        if (!targetNode) {
            const plan = galleryLayout.masonryPagePlan(
                           session.currentIndex,
                           currentItemCenterX, currentItemCenterY,
                           plannedContentY, masonryPageRowViewportY,
                           direction,
                           galleryLayout.height * 7 / 8)
            if (!plan || !plan.valid)
                return -1
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
            targetNode = {
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

            // A terminal key which changes neither viewport nor cursor is a
            // true no-op. Do not append duplicate history nodes: one Page in
            // the opposite direction must still return a real full page.
            if (Math.abs(destination - plannedContentY) <= 0.01
                    && targetIndex === session.currentIndex) {
                return targetIndex
            }
            // A short catalog can have multiple masonry rows while its whole
            // content still fits in the viewport (maximum contentY == 0).
            // PageUp/PageDown then retain the established terminal cursor
            // semantics, but there is no viewport page to remember. Recording
            // that cursor-only move as a reciprocal page node would make the
            // opposite key return to the previous cursor instead of resolving
            // the opposite terminal (for example 1 -> PageUp 0 -> PageDown
            // must reach the final item, not return to 1).
            if (Math.abs(destination - plannedContentY) > 0.01)
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
        if (!session || galleryLayout.count === 0
                || session.currentIndex < 0 || galleryLayout.height <= 0)
            return

        if (galleryLayout.presentationMode === MasonryLayout.Grid) {
            return navigateGridViewportPage(direction, togglePrevious,
                                            deferCursorCommit)
        }
        if (galleryLayout.presentationMode === MasonryLayout.Masonry) {
            return navigateMasonryViewportPage(direction, togglePrevious,
                                               deferCursorCommit)
        }

        if (currentItemCenterX < 0 || currentItemCenterY < 0)
            resetCurrentItemCenter(session.currentIndex)

        // Keep these calculations byte-for-byte equivalent in meaning to the
        // standalone MasonryMode PageUp/PageDown path. In particular, the hit
        // test uses the animation destination and the persistent viewport Y,
        // while reaching either terminal item deliberately adopts its center.
        const deltaY = galleryLayout.height - galleryLayout.height / 8
        const futureContentY = panelScrollAnimation.running
                ? panelScrollAnimation.to : galleryLayout.contentY
        const currentIndex = session.currentIndex
        let targetIndex = currentIndex
        let hitEdge = false

        if (direction < 0) {
            const pageY = Math.max(0, futureContentY - deltaY)
                    + currentItemCenterY
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
            const maximum = Math.max(
                    0, galleryLayout.contentHeight - galleryLayout.height)
            const pageY = Math.min(maximum, futureContentY + deltaY)
                    + currentItemCenterY
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
        scrollBy(direction * deltaY, false, true)
        return targetIndex
    }

    function ensureCurrentVisible(animateScroll) {
        if (!session || galleryLayout.count === 0
                || session.currentIndex < 0 || galleryLayout.height <= 0)
            return
        const shouldAnimate = animateScroll === undefined
                ? true : Boolean(animateScroll)
        let geometry = galleryLayout.indexGeometry(session.currentIndex)
        // Columns only lays out its current virtual page.  A cursor restored
        // by stable ID (or moved with Home/End) may therefore have no geometry
        // until we first move the page window that contains it.
        if ((geometry.width <= 0 || geometry.height <= 0)
                && galleryLayout.presentationMode === MasonryLayout.Columns) {
            const top = galleryLayout.windowTopIndexForIndex(session.currentIndex)
            if (top >= 0 && top !== galleryLayout.windowTopIndex)
                galleryLayout.windowTopIndex = top
            geometry = galleryLayout.indexGeometry(session.currentIndex)
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
                session.panelScrollOffset = galleryLayout.contentY
                session.panelViewportCursorEntryId = session.cursorEntryId
            }
            return
        }
        const maximum = Math.max(0, galleryLayout.contentHeight
                                 - (presentationMode === "columns"
                                    ? galleryLayout.width
                                    : galleryLayout.height))
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

    function ownsKey(event) {
        // Embedders can retain visual focus in Gallery while an adjacent
        // command line owns editing/navigation input. Decline before matching
        // individual keys so the parent host can forward the whole event.
        if (hostCapabilities
                && hostCapabilities.galleryOwnsPanelInput === false)
            return false
        const modifiers = event.modifiers
                & (Qt.ShiftModifier | Qt.ControlModifier
                   | Qt.AltModifier | Qt.MetaModifier)
        const spatial = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up || event.key === Qt.Key_Down
        if (spatial)
            return modifiers === Qt.NoModifier || modifiers === Qt.ShiftModifier
        if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown
                || event.key === Qt.Key_Home || event.key === Qt.Key_End)
            return modifiers === Qt.NoModifier || modifiers === Qt.ShiftModifier
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // Embedders can temporarily retain Return for a surrounding
            // command line while the Gallery surface keeps visual focus.
            if (hostCapabilities
                    && hostCapabilities.galleryOwnsReturn === false)
                return false
            return modifiers === Qt.NoModifier
        }
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Insert)
            return modifiers === Qt.NoModifier
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal
                || event.key === Qt.Key_Minus)
            return modifiers === Qt.NoModifier
        return false
    }

    function ensureSessionPreviews() {
        if (!session || customContent)
            return
        session.ensurePreviews()
        // A host may apply a catalog before this Loader exists. Metadata can
        // therefore already be present by the time MasonryLayout connects to
        // model notifications; explicitly seed visible decode work once. The
        // fixed-mode path requests only visible cells and bounded overscan.
        Qt.callLater(galleryLayout.reReadAndDecodeThumbnails)
    }

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
        visible: !root.customContent
    }

    // Capture empty-space presses inside the semantic panel. Without this,
    // an unhandled click can reach an embedder's hidden fallback surface.
    MouseArea {
        objectName: "galleryBackgroundPointerArea"
        anchors.fill: parent
        enabled: !root.customContent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => {
            root.forceActiveFocus()
            root.activateRequested()
            mouse.accepted = true
        }
    }

    Rectangle {
        id: detailsHeader
        objectName: "galleryDetailsHeader"
        visible: !root.customContent && root.presentationMode === "details"
                 && root.showDetailsHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.detailsHeaderHeight
        color: "transparent"
        border.width: 0
        z: 5

        readonly property var columns:
            root.columnSchema && root.columnSchema.length > 0
            ? root.columnSchema : [
                { id: "name", role: "name", title: qsTr("Name"),
                  width: 50, alignment: "left", sortMode: "name",
                  sortable: true },
                { id: "size", role: "size", title: qsTr("Size"),
                  width: 14, alignment: "right", sortMode: "size",
                  sortable: true }
            ]
        readonly property real totalColumnWidth: {
            let total = 0
            for (let index = 0; index < columns.length; ++index)
                total += Math.max(1, Number(columns[index].width || 1))
            return Math.max(1, total)
        }

        function columnX(index) {
            let before = 0
            for (let candidate = 0; candidate < index; ++candidate)
                before += Math.max(
                            1, Number(columns[candidate].width || 1))
            return Math.round(width * before / totalColumnWidth)
        }

        function columnWidth(index) {
            const start = columnX(index)
            return index === columns.length - 1
                    ? width - start : columnX(index + 1) - start
        }

        Repeater {
            model: detailsHeader.columns

            delegate: Rectangle {
                id: detailsHeaderCell
                objectName: "galleryDetailsHeaderCell-" + index
                x: detailsHeader.columnX(index)
                width: detailsHeader.columnWidth(index)
                height: detailsHeader.height
                color: detailsHeaderPointer.containsMouse
                       && modelData.sortable === true
                       ? root.headerHoverColor : "transparent"

                Behavior on color { ColorAnimation { duration: 70 } }

                Text {
                    objectName: "galleryDetailsHeaderText-" + index
                    anchors.fill: parent
                    anchors.leftMargin: root.detailsHeaderCellInset
                    anchors.rightMargin: root.detailsHeaderCellInset
                    text: modelData.title || ""
                    color: modelData.sortable === true
                           ? root.headerTextColor : root.mutedColor
                    font.pixelSize: root.detailsHeaderFontPixelSize
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: index > 0
                                         || modelData.alignment === "right"
                                         ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideRight
                }

                Rectangle {
                    objectName: "galleryDetailsHeaderSeparator-" + index
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.detailsSeparatorWidth
                    height: Math.max(
                        1, parent.height
                           - root.detailsSeparatorVerticalMargin * 2)
                    color: root.separatorColor
                    opacity: index < detailsHeader.columns.length - 1
                             ? 0.65 : 0
                }

                MouseArea {
                    id: detailsHeaderPointer
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    enabled: modelData.sortable === true
                    cursorShape: enabled ? Qt.PointingHandCursor
                                         : Qt.ArrowCursor
                    onClicked: mouse => {
                        root.sortRequested(modelData.sortMode
                                           || modelData.role
                                           || modelData.id || "name",
                                           mouse.button === Qt.RightButton)
                        mouse.accepted = true
                    }
                }
            }
        }

        Rectangle {
            objectName: "galleryDetailsHeaderBottomSeparator"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.detailsSeparatorWidth
            color: root.separatorColor
            opacity: 0.7
        }
    }

    Label {
        id: iconLabelFontProbe
        visible: false
        text: "M"
    }

    MasonryLayout {
        id: galleryLayout
        objectName: "galleryMasonryLayout"
        persistSettings: false
        visible: !root.customContent
        enabled: !root.customContent
        opacity: root.pathViewportPlacementPending ? 0 : 1
        anchors {
            left: parent.left
            top: detailsHeader.visible ? detailsHeader.bottom : parent.top
            bottom: parent.bottom
            // The classic Details list paints rows below an overlay scrollbar
            // and uses the whole viewport width. Other presentations retain
            // their established breathing room and reserved scrollbar lane.
            right: root.presentationMode === "details"
                   || root.presentationMode === "columns"
                   ? parent.right : galleryScroll.left
            leftMargin: root.presentationMode === "details" ? 0 : 6
            topMargin: root.presentationMode === "details" ? 0 : 6
            bottomMargin: root.presentationMode === "details" ? 0 : 6
            rightMargin: root.presentationMode === "details" ? 0 : 6
        }
        model: root.session ? root.session.model : null
        currentIndex: root.session ? root.session.currentIndex : -1
        presentationMode: root.nativePresentationMode()
        columnCount: Math.max(2, Math.min(3, root.columnCount))
        spacing: 8
        listView: false
        showTransparentGrid: true
        // Mode changes are atomic. The host applies the mode and then that
        // mode's saved density in one turn; allowing the second rewrap to
        // animate makes recycled tiles visibly fly between layouts.
        animateResizing: !root.presentationSwitchPending
                         && !root.pathViewportPlacementPending
        devicePixelRatio: root.devicePixelRatio
        iconLabelFont: root.iconLabelFont

        onLayoutReset: {
            root.traceBenchmarkStage("layout.reset", {})
            root.resetMasonryPageSequence()
            if (root.cursorChromeTransitionActive)
                cursorChromeLayoutRetargetTimer.restart()
            root.resetCurrentItemCenter(root.session
                                        ? root.session.currentIndex : -1)
            if (!panelScrollAnimation.running)
                root.pendingVisualCursorIndex = -1
            root.coordinateVisualCursor(
                        root.session ? root.session.currentIndex : -1,
                        root.visualCursorIndex)
            if (root.presentationSwitchPending)
                presentationSwitchTimer.restart()
            else if (root.pathViewportPlacementPending)
                root.schedulePathViewportPlacement("layout-reset")
            else if (!root.densityViewportTransaction)
                root.scheduleViewportUpdate(false)
        }
        onCountChanged: {
            root.traceBenchmarkStage("layout.count.changed", {})
            root.cancelCursorChromeTransition()
            root.resetCurrentItemCenter(root.session
                                        ? root.session.currentIndex : -1)
            if (!panelScrollAnimation.running)
                root.pendingVisualCursorIndex = -1
            root.coordinateVisualCursor(
                        root.session ? root.session.currentIndex : -1,
                        root.visualCursorIndex)
            if (root.pathViewportPlacementPending)
                root.schedulePathViewportPlacement("count-changed")
            else
                root.scheduleViewportUpdate(false)
        }
        onContentHeightChanged: {
            root.traceBenchmarkStage("layout.content-height.changed", {})
            root.resetMasonryPageSequence()
            if (root.pathViewportPlacementPending)
                root.schedulePathViewportPlacement("content-height-changed")
            else if (!root.densityViewportTransaction)
                root.scheduleViewportUpdate(false)
        }
        onContentYChanged: {
            root.updateVisualCursorForViewport()
            if (root.pathViewportPlacementPending)
                root.traceBenchmarkStage("layout.content-y.changed", {})
        }
        onWidthChanged: {
            root.resetMasonryPageSequence()
            if (width > 0 && count > 0)
                thumbnailResizeDecodeTimer.restart()
        }
        onHeightChanged: root.invalidateMasonryPageGeometry()
        onDensityChanged: {
            root.resetMasonryPageSequence()
        }
        onLayoutBandsChanged: {
            if (galleryLayout.presentationMode === MasonryLayout.Masonry) {
                // Row-band geometry is the page coordinate system. A width,
                // density, metadata or catalog reflow invalidates an in-flight
                // destination, even when total contentHeight happens to stay
                // unchanged. Stop at the live viewport and re-anchor the next
                // Page key against the new revision.
                root.invalidateMasonryPageGeometry()
            } else if (root.cursorChromeTransitionActive) {
                cursorChromeLayoutRetargetTimer.restart()
            }
        }

        delegate: Component {
            GalleryEntryDelegate {
                panelRoot: root
            }
        }
    }

    // Fill remains below the native viewport, exactly where every delegate's
    // selection surface used to paint it. The separate hollow border stays
    // above opaque previews so an in-between geometry remains legible without
    // tinting thumbnails, shaders, icons, or text.
    Item {
        id: cursorChromeUnderlayLayer
        parent: galleryLayout
        anchors.fill: parent
        z: -1
        clip: true
        visible: root.cursorChromeTransitionActive && root.showCursor
        Rectangle {
            objectName: "galleryCursorChromeUnderlay"
            x: root.cursorChromeX
            y: root.cursorChromeY
            width: root.cursorChromeWidth
            height: root.cursorChromeHeight
            radius: root.cursorChromeRadius
            color: root.cursorChromeFillColor
        }
    }

    Item {
        id: cursorChromeBorderLayer
        parent: galleryLayout
        anchors.fill: parent
        z: 1
        clip: true
        visible: root.cursorChromeTransitionActive && root.showCursor
        Rectangle {
            objectName: "galleryCursorChromeBorder"
            x: root.cursorChromeX
            y: root.cursorChromeY
            width: root.cursorChromeWidth
            height: root.cursorChromeHeight
            radius: root.cursorChromeRadius
            color: "transparent"
            border.width: root.cursorChromeBorderWidth
            border.color: root.cursorChromeBorderColor
        }
    }

    // Qt's classic ListView intentionally estimates unseen fractional-height
    // delegates at an integer extent. Keep the unified renderer's exact
    // content geometry, but reuse that estimator for the Details scrollbar so
    // its thumb and drag mapping remain pixel-identical to f4's old Detailed
    // list. This proxy is noninteractive and materializes only ListView's
    // bounded visible/cache window of empty Items.
    ListView {
        id: detailsScrollMetrics
        objectName: "galleryDetailsScrollMetrics"
        x: galleryLayout.x
        y: galleryLayout.y
        width: galleryLayout.width
        height: galleryLayout.height
        z: -1000
        visible: !root.customContent
                 && root.presentationMode === "details"
        enabled: false
        interactive: false
        clip: true
        // f4's production ListView keeps a 320 logical-pixel cache window.
        // State it explicitly because empty proxy delegates have no rendering
        // demand that would otherwise keep the same buffer populated.
        cacheBuffer: 320
        readonly property real rowExtent: galleryLayout.density
        readonly property int sourceCount: visible ? galleryLayout.count : 0
        // Do not observe the renderer's cursor while this Details-only proxy
        // is dormant. MasonryLayout establishes its model/currentIndex
        // bindings during component completion; forcing an eager read from a
        // hidden ListView can snapshot the model-reset value (zero) before
        // the authoritative session cursor binding settles.
        readonly property int sourceCurrentIndex:
            visible ? galleryLayout.currentIndex : -1
        function applySourceCurrentIndex() {
            if (sourceCount <= 0) {
                currentIndex = -1
                return
            }
            currentIndex = Math.max(
                        0, Math.min(sourceCount - 1,
                                    sourceCurrentIndex))
        }
        function syncContentY() {
            contentY = galleryLayout.contentY
        }
        onSourceCountChanged: {
            applySourceCurrentIndex()
            syncContentY()
        }
        onRowExtentChanged: syncContentY()
        onSourceCurrentIndexChanged: {
            applySourceCurrentIndex()
            syncContentY()
        }
        Component.onCompleted: {
            applySourceCurrentIndex()
            syncContentY()
        }
        model: sourceCount
        boundsBehavior: Flickable.StopAtBounds

        Connections {
            target: galleryLayout
            function onContentYChanged() {
                detailsScrollMetrics.syncContentY()
            }
            function onContentHeightChanged() {
                detailsScrollMetrics.syncContentY()
            }
        }

        delegate: Item {
            required property int index
            objectName: "galleryDetailsScrollMetricRow-" + index
            width: detailsScrollMetrics.width
            height: detailsScrollMetrics.rowExtent
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !root.customContent &&
                 (!root.session || galleryLayout.count === 0)
        text: qsTr("No previewable entries")
        color: root.mutedColor
    }

    MouseArea {
        objectName: "galleryWheelArea"
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        enabled: !root.customContent
        onWheel: wheel => {
            root.handlePanelWheel(wheel.pixelDelta.y, wheel.angleDelta.y,
                                  wheel.modifiers, wheel.pixelDelta.x,
                                  wheel.angleDelta.x)
            wheel.accepted = true
        }
    }

    PinchArea {
        objectName: "galleryPinchArea"
        anchors.fill: galleryLayout
        enabled: !root.customContent

        onPinchStarted: pinch => {
            root.beginThumbnailPinch()
            pinch.accepted = true
        }
        onPinchUpdated: pinch => {
            root.updateThumbnailPinch(pinch.scale)
            pinch.accepted = true
        }
        onPinchFinished: pinch => {
            root.finishThumbnailPinch()
            pinch.accepted = true
        }
    }

    NumberAnimation {
        id: panelScrollAnimation
        objectName: "galleryPanelScrollAnimation"
        target: galleryLayout
        property: "contentY"
        duration: 150
        easing.type: Easing.OutSine
        onRunningChanged: {
            if (running)
                cursorChromeFinalizeTimer.stop()
            else {
                root.masonryPageScrollActive = false
                if (root.cursorChromeTransitionActive)
                    cursorChromeFinalizeTimer.restart()
            }
            if (!running && root.cursorCommitAfterScroll)
                cursorCommitAfterScrollTimer.restart()
            if (!running && !root.suppressScrollAnimationPersistence
                    && root.session) {
                root.session.panelScrollOffset = galleryLayout.contentY
                root.session.panelViewportCursorEntryId =
                        root.session.cursorEntryId
                if (root.viewportUpdatePendingAfterScroll) {
                    const ensureCursor = root.viewportUpdateEnsuresCursor
                    root.viewportUpdatePendingAfterScroll = false
                    root.viewportUpdateEnsuresCursor = false
                    root.scheduleViewportUpdate(ensureCursor)
                }
            }
            if (!running && root.cursorCommitAfterScroll)
                cursorCommitAfterScrollTimer.restart()
            if (!running)
                root.updateVisualCursorForViewport()
        }
    }

    ParallelAnimation {
        id: cursorChromeGeometryAnimation
        objectName: "galleryCursorChromeGeometryAnimation"
        onRunningChanged: {
            if (running)
                cursorChromeFinalizeTimer.stop()
            else {
                if (root.cursorChromeTransitionActive)
                    cursorChromeFinalizeTimer.restart()
                // Compact Details/Columns can animate cursor chrome while
                // their viewport itself moves synchronously.  In that case
                // there is no panelScrollAnimation completion to re-arm the
                // deferred host commit, so the chrome animation owns it.
                if (root.cursorCommitAfterScroll)
                    cursorCommitAfterScrollTimer.restart()
            }
        }
        NumberAnimation {
            id: cursorChromeXAnimation
            target: root
            property: "cursorChromeX"
            duration: 150
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            id: cursorChromeYAnimation
            target: root
            property: "cursorChromeY"
            duration: 150
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            id: cursorChromeWidthAnimation
            target: root
            property: "cursorChromeWidth"
            duration: 150
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            id: cursorChromeHeightAnimation
            target: root
            property: "cursorChromeHeight"
            duration: 150
            easing.type: Easing.OutSine
        }
    }

    Timer {
        id: cursorChromeFinalizeTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (!panelScrollAnimation.running
                    && !cursorChromeGeometryAnimation.running)
                root.finishCursorChromeTransition()
        }
    }

    Timer {
        id: cursorChromeLayoutRetargetTimer
        interval: 0
        repeat: false
        onTriggered: root.retargetCursorChromeAfterLayoutReset()
    }

    // Keep the original MasonryMode scrollbar contract.  MasonryLayout is a
    // custom viewport (not a Flickable), so an attached ScrollBar cannot infer
    // size or position; these exact contentY/contentHeight mappings are the
    // authoritative standalone implementation.
    GalleryScrollBar {
        id: galleryScroll
        objectName: "galleryPanelScrollBar"
        theme: root.theme
        anchors.top: galleryLayout.top
        anchors.bottom: galleryLayout.bottom
        anchors.right: parent.right
        z: 10
        visible: !root.customContent && root.presentationMode !== "columns"
                 && galleryLayout.needScroll
        width: galleryLayout.needScroll
               ? (root.presentationMode === "details"
                  ? root.detailsScrollBarWidth : 16) : 0
        orientation: Qt.Vertical

        onPositionChanged: {
            if (pressed)
                root.setPanelContentY(position * scrollContentHeight(),
                                      true)
        }

        function scrollContentHeight() {
            if (root.presentationMode === "details"
                    && detailsScrollMetrics.count === galleryLayout.count
                    && detailsScrollMetrics.contentHeight > 0)
                return detailsScrollMetrics.contentHeight
            return galleryLayout.contentHeight
        }

        function scrollContentY() {
            return root.presentationMode === "details"
                    ? detailsScrollMetrics.contentY
                    : galleryLayout.contentY
        }

        function updateSize() {
            const extent = scrollContentHeight()
            size = extent > 0
                    ? Math.min(1, galleryLayout.height / extent) : 1
        }

        Connections {
            target: galleryLayout

            function onContentYChanged() {
                const extent = galleryScroll.scrollContentHeight()
                galleryScroll.position = extent > 0
                        ? galleryScroll.scrollContentY() / extent : 0
            }
            function onContentHeightChanged() { galleryScroll.updateSize() }
            function onHeightChanged() { galleryScroll.updateSize() }
        }

        Connections {
            target: detailsScrollMetrics
            function onContentHeightChanged() { galleryScroll.updateSize() }
            function onContentYChanged() {
                const extent = galleryScroll.scrollContentHeight()
                galleryScroll.position = extent > 0
                        ? galleryScroll.scrollContentY() / extent : 0
            }
        }

        Component.onCompleted: {
            const extent = scrollContentHeight()
            size = extent > 0
                    ? Math.min(1, galleryLayout.height / extent) : 1
            position = extent > 0 ? scrollContentY() / extent : 0
        }
    }

    GalleryScrollBar {
        id: galleryColumnsScroll
        objectName: "galleryPanelColumnsScrollBar"
        theme: root.theme
        anchors.left: galleryLayout.left
        anchors.right: galleryLayout.right
        anchors.bottom: galleryLayout.bottom
        z: 10
        height: visible ? 16 : 0
        visible: !root.customContent && root.presentationMode === "columns"
                 && galleryLayout.needScroll
        orientation: Qt.Horizontal
        size: galleryLayout.contentHeight > 0
              ? Math.min(1, galleryLayout.width / galleryLayout.contentHeight)
              : 1
        position: galleryLayout.contentHeight > 0
                  ? galleryLayout.contentY / galleryLayout.contentHeight : 0
        onPositionChanged: {
            if (pressed)
                root.setPanelContentY(position * galleryLayout.contentHeight,
                                      true)
        }
    }

    Keys.onPressed: event => {
        if (root.customContent || !root.session)
            return
        if (event.key === Qt.Key_Shift) {
            event.accepted = true
            return
        }
        // Modified commander shortcuts must bubble to the embedding host. A
        // parent Keys handler cannot pre-empt an active-focus child, so the
        // reusable component must decline them before matching on key alone.
        if (!root.ownsKey(event)) {
            event.accepted = false
            return
        }
        const shiftSelection = Boolean(event.modifiers & Qt.ShiftModifier)
        const cursorIndexBeforePress = root.session.currentIndex
        const spatial = event.key === Qt.Key_Left
                || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up
                || event.key === Qt.Key_Down
        const page = event.key === Qt.Key_PageUp
                || event.key === Qt.Key_PageDown
        const edge = event.key === Qt.Key_Home || event.key === Qt.Key_End
        const navigation = spatial || page || edge
        if (navigation && (!page
                || galleryLayout.presentationMode !== MasonryLayout.Grid)) {
            root.resetGridPageLattice()
        }
        if (navigation && (!page
                || galleryLayout.presentationMode !== MasonryLayout.Masonry)) {
            root.resetMasonryPageSequence()
        }
        const chromeSnapshot = navigation
                ? root.cursorChromeNavigationSnapshot() : null
        let chromeTargetIndex = -1
        if (navigation && !event.isAutoRepeat)
            root.navigationKeyHeld = true
        // Cursor and Shift-selection both remain local while a key is held.
        // The physical release sends one stable-ID toggle batch followed by
        // the final cursor, so autorepeat never starts semantic round-trips.
        const deferCursorCommit = true
        if (spatial) {
            const index = root.navigationTargetForKey(event.key, false)
            if (index >= 0) {
                chromeTargetIndex = index
                root.moveCursorWithSelection(
                            index, shiftSelection,
                            event.key === Qt.Key_Up || event.key === Qt.Key_Down,
                            deferCursorCommit)
            } else if (shiftSelection) {
                root.moveCursorWithSelection(root.session.currentIndex, true,
                                             true, false)
            }
            event.accepted = true
        } else if (page) {
            const direction = event.key === Qt.Key_PageUp ? -1 : 1
            if (galleryLayout.presentationMode === MasonryLayout.Columns) {
                const key = direction < 0 ? Qt.Key_Up : Qt.Key_Down
                const index = root.navigationTargetForKey(key, true)
                if (index >= 0) {
                    chromeTargetIndex = index
                    // Columns owns a discrete virtual page/window rather than
                    // a continuous vertical viewport.
                    root.moveCursorWithSelection(index, shiftSelection, false,
                                                 deferCursorCommit, false)
                }
            } else {
                // Details/Icons retain the original raw 7/8-page choreography.
                // Grid quantizes it to fixed row strides; Masonry chooses real
                // variable-height row-band boundaries. All paths preserve the
                // persistent X/Y probe and keep selection, cursor chrome and
                // the exact planned viewport destination coupled.
                chromeTargetIndex = root.navigateViewportPage(
                            direction, shiftSelection, deferCursorCommit)
            }
            event.accepted = true
        } else if (edge) {
            chromeTargetIndex = event.key === Qt.Key_Home
                    ? 0 : galleryLayout.count - 1
            root.moveCursorWithSelection(
                        chromeTargetIndex,
                        shiftSelection, false, deferCursorCommit)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.cancelCursorChromeTransition()
            root.commitPendingCursor()
            root.selectIndex(root.session.currentIndex, true)
            event.accepted = true
        } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Insert) {
            root.cancelCursorChromeTransition()
            root.commitPendingCursor()
            const currentIndex = root.session.currentIndex
            const id = root.session.entryIdAt(currentIndex)
            if (id !== "" && root.session.entryNameAt(currentIndex) !== "..")
                root.selectionRequested("toggle", [id])
            // Match the f4/Far panel contract: Insert marks the current item
            // and advances, while Space only toggles in place.
            if (event.key === Qt.Key_Insert && currentIndex + 1 < galleryLayout.count)
                root.moveCursor(currentIndex + 1, false, false, false)
            event.accepted = true
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            root.cancelCursorChromeTransition()
            root.changeDensity(function() { galleryLayout.zoomIn() })
            root.noteDensityChanged(false)
            event.accepted = true
        } else if (event.key === Qt.Key_Minus) {
            root.cancelCursorChromeTransition()
            root.changeDensity(function() { galleryLayout.zoomOut() })
            root.noteDensityChanged(false)
            event.accepted = true
        }
        if (chromeTargetIndex >= 0)
            root.startCursorChromeForNavigation(chromeSnapshot,
                                                chromeTargetIndex)
        if ((spatial || page) && !shiftSelection && root.cursorCommitPending
                && root.session.currentIndex === cursorIndexBeforePress) {
            root.refreshPendingCursorCommit()
        }
    }

    Keys.onReleased: event => {
        if (root.customContent || !root.session) {
            event.accepted = false
            return
        }
        if (event.key === Qt.Key_Shift) {
            if (event.isAutoRepeat)
                return
            root.navigationKeyHeld = false
            root.finishKeyboardShiftSelection()
            root.commitCursorAfterNavigation()
            event.accepted = true
            return
        }
        if (!root.ownsKey(event)) {
            event.accepted = false
            return
        }
        const navigation = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up || event.key === Qt.Key_Down
                || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown
                || event.key === Qt.Key_Home || event.key === Qt.Key_End
        if (navigation) {
            event.accepted = true
            // On macOS a held key produces auto-repeat release/press pairs.
            // Those intermediate releases are not the end of navigation and
            // must never trigger a Go semantic-scene round-trip.
            if (event.isAutoRepeat)
                return
            root.navigationKeyHeld = false
            // The original Zoin Gallery keeps one live range preview across
            // all Shift+navigation keys and commits it only when Shift itself
            // is released. Ordinary navigation still commits on key-up.
            if (!root.keyboardShiftSelectionActive)
                root.commitCursorAfterNavigation()
        }
    }

    Connections {
        target: root.session
        function onCatalogRevisionChanged() {
            root.traceBenchmarkStage("session.catalog.changed", {})
            root.pathViewportCatalogReady = typeof root.session.catalogReady
                    === "undefined" || root.session.catalogReady
            root.clearPendingKeyboardSelection()
            root.cancelCursorChromeTransition()
            // Persistent host sessions can outlive (and be populated after)
            // this Loader. A panel completed against an empty session has no
            // metadata or thumbnail work to seed, so every authoritative
            // catalog replacement must prepare previews again.
            root.ensureSessionPreviews()
            root.resetCurrentItemCenter(root.session.currentIndex)
            root.selectionAnchorIndex = root.session.currentIndex
            root.coordinateVisualCursor(root.session.currentIndex,
                                        root.visualCursorIndex)
            root.schedulePathViewportPlacement("catalog-revision-changed")
        }
        function onSelectionRevisionChanged() {
            root.reconcileAcknowledgedKeyboardSelection()
            if (!root.navigationKeyHeld
                    && Object.keys(root.pendingKeyboardSelectionToggles).length)
                Qt.callLater(root.commitPendingKeyboardSelection)
        }
        function onCurrentIndexChanged() {
            root.traceBenchmarkStage("session.index.changed", {})
            if (!root.localCursorNavigation) {
                const previousVisualIndex = root.visualCursorIndex
                root.resetCurrentItemCenter(root.session.currentIndex)
                root.selectionAnchorIndex = root.session.currentIndex
                root.coordinateVisualCursor(root.session.currentIndex,
                                            previousVisualIndex)
                if (root.pathViewportPlacementPending) {
                    // Details geometry is commonly valid by the time the
                    // authoritative cursor arrives. Center in this signal
                    // turn so the first visible paint uses the destination;
                    // keep the zero-delay timer when the catalog transaction
                    // or layout is still settling.
                    if (!root.placeViewportForPathChange())
                        root.schedulePathViewportPlacement(
                                    "current-index-changed")
                } else {
                    root.scheduleViewportUpdate(true)
                }
            }
        }
        function onCurrentPathChanged() {
            root.traceBenchmarkStage("session.path.changed", {})
            root.pathViewportPlacementPending =
                    root.presentationMode === "details"
            root.pathViewportCatalogReady =
                    !root.pathViewportPlacementPending
                    || typeof root.session.catalogReady === "undefined"
                    || root.session.catalogReady
            if (root.pathViewportPlacementPending) {
                viewportUpdateTimer.stop()
                root.viewportUpdatePendingAfterScroll = false
                root.viewportUpdateEnsuresCursor = false
                root.schedulePathViewportPlacement("current-path-changed")
            }
            root.resetCurrentItemCenter(root.session.currentIndex)
            root.selectionAnchorIndex = root.session.currentIndex
        }
        function onCatalogReadyChanged() {
            root.pathViewportCatalogReady = root.session.catalogReady
            if (root.pathViewportPlacementPending
                    && root.pathViewportCatalogReady
                    && !root.placeViewportForPathChange()) {
                root.schedulePathViewportPlacement("catalog-ready-changed")
            }
        }
        function onPanelScrollOffsetChanged() {
            if (!root.restoringScrollOffset
                    && Math.abs(galleryLayout.contentY
                                - root.session.panelScrollOffset) > 0.5) {
                root.scheduleViewportUpdate(false)
            }
        }
    }

    onPathViewportPlacementPendingChanged:
        traceBenchmarkStage("placement.pending.changed", {})

    onSessionChanged: {
        traceBenchmarkStage("session.changed", {})
        clearPendingKeyboardSelection()
        cancelCursorChromeTransition()
        cursorCommitTimer.stop()
        cursorCommitAfterScrollTimer.stop()
        cursorCommitPending = false
        cursorCommitAfterScroll = false
        navigationKeyHeld = false
        currentItemCenterX = -1
        currentItemCenterY = -1
        visualCursorIndex = session ? session.currentIndex : -1
        pendingVisualCursorIndex = -1
        ensureSessionPreviews()
        selectionAnchorIndex = session ? session.currentIndex : -1
        scheduleViewportUpdate(false)
    }

    onPresentationModeChanged: {
        // A presentation switch is a discontinuous layout transaction, not
        // navigation. Freeze every in-flight viewport/cursor transition and
        // keep BrickItem geometry updates synchronous until the new mode,
        // its density and declarative anchors have all settled.
        beginPresentationSwitch()
    }
    onShowCursorChanged: {
        if (!showCursor)
            cancelCursorChromeTransition()
    }
    onViewerTransitionActiveChanged: {
        if (viewerTransitionActive)
            cancelCursorChromeTransition()
    }

    onActiveFocusChanged: {
        if (!activeFocus) {
            navigationKeyHeld = false
            cancelCursorChromeTransition()
            commitPendingCursor()
        }
    }

    Component.onCompleted: {
        visualCursorIndex = session ? session.currentIndex : -1
        pendingVisualCursorIndex = -1
        ensureSessionPreviews()
        scheduleViewportUpdate(false)
        if (autoFocus && !customContent)
            forceActiveFocus()
    }
}
