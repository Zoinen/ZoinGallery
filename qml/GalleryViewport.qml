pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

Item {
    id: viewportRoot

    required property Item panelRoot
    required property GalleryPanelController controller

    property alias layout: galleryLayout
    property alias targetHeight: galleryLayout.targetHeight
    property alias density: galleryLayout.density
    property alias detailsHeader: detailsHeader
    property alias pointerLayer: pointerLayer
    property alias scrollingStarted: pointerLayer.scrollingStarted
    property alias scrollingStartedAtY: pointerLayer.scrollingStartedAtY
    property alias scrollingMode: pointerLayer.scrollingMode
    readonly property font iconLabelFont: iconLabelFontProbe.font
    readonly property bool advancedEntryActions:
        controller.dragEnabled || controller.directoryDropEnabled
        || controller.canRemoveEntries

    function nativePresentationMode(mode) {
        switch (mode) {
        case "columns": return GalleryViewportItem.Columns
        case "details": return GalleryViewportItem.Details
        case "grid": return GalleryViewportItem.Grid
        case "icons": return GalleryViewportItem.Icons
        default: return GalleryViewportItem.Masonry
        }
    }

    function applyPresentationState(switchingMode, requestedMode) {
        const panel = viewportRoot.panelRoot
        const mode = requestedMode === undefined
                ? panel.presentationMode : String(requestedMode)
        const details = mode === "details"
        const verticalContentInset = mode === "masonry"
                || mode === "grid" || mode === "icons"

        panel.beginPresentationStateUpdate(Boolean(switchingMode))
        try {
            detailsHeader.visible = details && panel.showDetailsHeader
            galleryLayout.anchors.leftMargin = details ? 0 : 6
            galleryLayout.anchors.topMargin = detailsHeader.visible
                    ? detailsHeader.height
                    : (verticalContentInset || details ? 0 : 6)
            galleryLayout.anchors.bottomMargin = details
                    || verticalContentInset ? 0 : 6
            galleryLayout.anchors.rightMargin = details ? 0 : 6
            galleryLayout.paddingTop = verticalContentInset ? 6 : 0
            galleryLayout.paddingBottom = verticalContentInset ? 6 : 0
            galleryLayout.presentationMode = nativePresentationMode(mode)
        } finally {
            panel.endPresentationStateUpdate(true)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: viewportRoot.panelRoot.backgroundColor
    }

    // Capture empty-space presses inside the semantic panel. Without this,
    // an unhandled click can reach an embedder's hidden fallback surface.
    MouseArea {
        objectName: "galleryBackgroundPointerArea"
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => {
            viewportRoot.panelRoot.forceActiveFocus()
            viewportRoot.panelRoot.activateRequested()
            mouse.accepted = true
        }
    }

    GalleryDetailsHeader {
        id: detailsHeader
        visible: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: viewportRoot.panelRoot.detailsHeaderHeight
        z: 5
        columnSchema: viewportRoot.panelRoot.columnSchema
        hoverColor: viewportRoot.panelRoot.headerHoverColor
        textColor: viewportRoot.panelRoot.headerTextColor
        mutedTextColor: viewportRoot.panelRoot.mutedColor
        separatorColor: viewportRoot.panelRoot.separatorColor
        cellInset: viewportRoot.panelRoot.detailsHeaderCellInset
        separatorWidth: viewportRoot.panelRoot.detailsSeparatorWidth
        separatorVerticalMargin:
            viewportRoot.panelRoot.detailsSeparatorVerticalMargin
        textPixelSize: viewportRoot.panelRoot.detailsHeaderFontPixelSize
        onSortRequested: (sortMode, contextMenu) =>
            viewportRoot.panelRoot.sortRequested(sortMode, contextMenu)
    }

    Label {
        id: iconLabelFontProbe
        visible: false
        text: "M"
    }

    GalleryViewportItem {
        id: galleryLayout
        objectName: "galleryViewportItem"
        clip: true
        persistSettings: false
        visible: !viewportRoot.panelRoot.presentationLayoutHidden
        opacity: viewportRoot.panelRoot.pathViewportPlacementPending ? 0 : 1
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            leftMargin: 6
            topMargin: 0
            bottomMargin: 0
            rightMargin: 6
        }
        paddingTop: 6
        paddingBottom: 6
        model: viewportRoot.controller.catalogModel
        currentIndex: viewportRoot.controller.currentIndex
        presentationMode: GalleryViewportItem.Masonry
        columnCount: Math.max(
                         2, Math.min(3,
                                     viewportRoot.panelRoot.columnCount))
        spacing: viewportRoot.panelRoot.itemSpacing
        listView: viewportRoot.panelRoot.listView
        showTransparentGrid: viewportRoot.panelRoot.showTransparentGrid
        animateResizing: viewportRoot.panelRoot.animateLayoutChanges
                         && !viewportRoot.panelRoot.presentationSwitchPending
                         && !viewportRoot.panelRoot.pathViewportPlacementPending
        devicePixelRatio: viewportRoot.panelRoot.devicePixelRatio
        iconLabelFont: viewportRoot.iconLabelFont
        deferDelegateRefreshOnReset:
            !viewportRoot.controller.catalogReady

        onLayoutReset: {
            const panel = viewportRoot.panelRoot
            panel.traceBenchmarkStage("layout.reset", {})
            panel.resetMasonryPageSequence()
            if (panel.cursorChromeTransitionActive)
                panel.scheduleCursorChromeLayoutRetarget()
            panel.resetCurrentItemCenter(
                        viewportRoot.controller.currentIndex)
            if (!panel.panelScrollAnimationRunning)
                panel.pendingVisualCursorIndex = -1
            panel.coordinateVisualCursor(
                        viewportRoot.controller.currentIndex,
                        panel.visualCursorIndex)
            if (!panel.presentationSwitchPending) {
                if (panel.pathViewportPlacementPending)
                    panel.schedulePathViewportPlacement("layout-reset")
                else if (!panel.densityViewportTransaction)
                    panel.scheduleViewportUpdate(false)
            }
            panel.refreshHoveredIndex()
        }
        onVisibleIndexesChanged: {
            viewportRoot.panelRoot.publishMetadataVisibleRange()
            viewportRoot.panelRoot.refreshHoveredIndex()
        }
        onCountChanged: {
            const panel = viewportRoot.panelRoot
            panel.traceBenchmarkStage("layout.count.changed", {})
            panel.cancelCursorChromeTransition()
            panel.resetCurrentItemCenter(
                        viewportRoot.controller.currentIndex)
            if (!panel.panelScrollAnimationRunning)
                panel.pendingVisualCursorIndex = -1
            panel.coordinateVisualCursor(
                        viewportRoot.controller.currentIndex,
                        panel.visualCursorIndex)
            if (panel.pathViewportPlacementPending)
                panel.schedulePathViewportPlacement("count-changed")
            else
                panel.scheduleViewportUpdate(false)
        }
        onContentHeightChanged: {
            const panel = viewportRoot.panelRoot
            panel.traceBenchmarkStage("layout.content-height.changed", {})
            panel.resetMasonryPageSequence()
            if (panel.pathViewportPlacementPending)
                panel.schedulePathViewportPlacement("content-height-changed")
            else if (!panel.densityViewportTransaction)
                panel.scheduleViewportUpdate(false)
        }
        onContentYChanged: {
            viewportRoot.panelRoot.updateVisualCursorForViewport()
            viewportRoot.panelRoot.refreshHoveredIndex()
            if (viewportRoot.panelRoot.pathViewportPlacementPending) {
                viewportRoot.panelRoot.traceBenchmarkStage(
                            "layout.content-y.changed", {})
            }
        }
        onWidthChanged: {
            viewportRoot.panelRoot.refreshHoveredIndex()
            viewportRoot.panelRoot.resetMasonryPageSequence()
            if (width > 0 && count > 0)
                viewportRoot.panelRoot.scheduleThumbnailResizeDecode()
        }
        onHeightChanged: {
            viewportRoot.panelRoot.refreshHoveredIndex()
            viewportRoot.panelRoot.invalidateMasonryPageGeometry()
        }
        onDensityChanged: {
            viewportRoot.panelRoot.resetMasonryPageSequence()
            viewportRoot.panelRoot.viewportController.densityLayoutCommitted()
        }
        onLayoutBandsChanged: {
            if (galleryLayout.presentationMode === GalleryViewportItem.Masonry) {
                viewportRoot.panelRoot.invalidateMasonryPageGeometry()
            } else if (viewportRoot.panelRoot.cursorChromeTransitionActive) {
                viewportRoot.panelRoot.scheduleCursorChromeLayoutRetarget()
            }
        }

        delegate: viewportRoot.advancedEntryActions
                  ? interactiveEntryDelegate : standardEntryDelegate

        Component {
            id: standardEntryDelegate
            GalleryEntryDelegateBase {
                panelRoot: viewportRoot.panelRoot
            }
        }

        Component {
            id: interactiveEntryDelegate
            GalleryInteractiveEntryDelegate {
                panelRoot: viewportRoot.panelRoot
            }
        }
    }

    GalleryCursorChrome {
        parent: galleryLayout
        anchors.fill: parent
        active: viewportRoot.panelRoot.cursorChromeTransitionActive
        cursorVisible: viewportRoot.panelRoot.showCursor
        geometry: viewportRoot.panelRoot.cursorChromeRect
        cornerRadius: viewportRoot.panelRoot.cursorChromeRadius
        fillColor: viewportRoot.panelRoot.cursorChromeFillColor
        outlineColor: viewportRoot.panelRoot.cursorChromeBorderColor
        outlineWidth: viewportRoot.panelRoot.cursorChromeBorderWidth
    }

    Label {
        anchors.centerIn: parent
        visible: viewportRoot.panelRoot.emptyStateEnabled
                 && galleryLayout.count === 0
        text: viewportRoot.panelRoot.emptyStateText
        color: viewportRoot.panelRoot.mutedColor
    }

    GalleryPointerLayer {
        id: pointerLayer
        anchors.fill: parent
        layout: galleryLayout
        wheelMode: viewportRoot.panelRoot.mouseWheelMode
        presentationMode: viewportRoot.panelRoot.presentationMode
        densityAdjustmentEnabled:
            viewportRoot.panelRoot.densityAdjustmentEnabled
        primarySelectionEnabled:
            viewportRoot.panelRoot.controllerReady
            && !viewportRoot.controller.dragEnabled
            && !viewportRoot.controller.directoryDropEnabled
            && !viewportRoot.controller.canRemoveEntries
        autoScrollExtent: viewportRoot.panelRoot.Window.window
                          ? viewportRoot.panelRoot.Window.window.height
                          : (presentationMode === "columns"
                             ? galleryLayout.width : galleryLayout.height)

        onWheelRequested: (x, y, pixelDeltaY, angleDeltaY, modifiers,
                           pixelDeltaX, angleDeltaX) =>
            viewportRoot.panelRoot.handlePanelWheel(
                pixelDeltaY, angleDeltaY, modifiers,
                pixelDeltaX, angleDeltaX)
        onConsoleWheelRequested: (x, y, angleDeltaY, modifiers) =>
            viewportRoot.panelRoot.consoleWheelRequested(
                x, y, angleDeltaY, modifiers)
        onHoverMoved: (x, y) =>
            viewportRoot.panelRoot.updateHoveredIndexAt(x, y)
        onHoverExited: viewportRoot.panelRoot.clearHoveredIndex()
        onMiddlePressed: (x, y, modifiers) =>
            viewportRoot.panelRoot.handlePanelMiddlePress(x, y, modifiers)
        onMiddleReleased: (x, y, modifiers) =>
            viewportRoot.panelRoot.handlePanelMiddleRelease(x, y, modifiers)
        onConsoleMiddleCanceled: (x, y) =>
            viewportRoot.panelRoot.consoleMouseButtonRequested(
                x, y, Qt.MiddleButton, false, Qt.NoModifier)
        onPinchStarted: viewportRoot.panelRoot.beginThumbnailPinch()
        onPinchUpdated: scale =>
            viewportRoot.panelRoot.updateThumbnailPinch(scale)
        onPinchFinished: viewportRoot.panelRoot.finishThumbnailPinch()
        onPrimaryPressed: (x, y, button, modifiers) => {
            const point = galleryLayout.mapFromItem(pointerLayer, x, y)
            const index = galleryLayout.indexAtViewport(point.x, point.y)
            if (index >= 0) {
                viewportRoot.panelRoot.handlePointerPress(index, button,
                                                          modifiers)
            } else {
                viewportRoot.panelRoot.forceActiveFocus()
                viewportRoot.panelRoot.activateRequested()
            }
        }
        onPrimaryDragged: (x, y) => {
            const point = pointerLayer.mapToItem(
                        viewportRoot.panelRoot, x, y)
            viewportRoot.panelRoot.handlePointerDrag(point.x, point.y)
        }
        onPrimaryReleased: viewportRoot.panelRoot.endPointerDrag()
        onPrimaryDoubleClicked: (x, y, button) => {
            const point = galleryLayout.mapFromItem(pointerLayer, x, y)
            const index = galleryLayout.indexAtViewport(point.x, point.y)
            if (index < 0)
                return
            if ((button & Qt.LeftButton) !== 0)
                viewportRoot.panelRoot.selectIndex(index, true)
            else if ((button & Qt.RightButton) !== 0)
                viewportRoot.panelRoot.invertPanelSelection()
        }
    }

    Connections {
        target: viewportRoot.panelRoot
        function onShowDetailsHeaderChanged() {
            viewportRoot.applyPresentationState(false)
        }
    }

    Component.onCompleted:
        applyPresentationState(false, viewportRoot.panelRoot.presentationMode)
}
