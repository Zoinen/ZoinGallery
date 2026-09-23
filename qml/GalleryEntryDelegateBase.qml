pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

BrickItem {
    id: entry

    required property Item panelRoot
    property var model
    // Installed by the layout after committing the whole metadata-ready row.
    property bool masonryGeometryReady: false
    // Cleared only during synchronous photo-to-folder delegate reassignment.
    property bool visualGeometryReady: true
    readonly property Item thumbnailItem: sharedPreview.thumbnailItem
    readonly property Item previewContainerItem: sharedPreview
    readonly property real paintedContentHeight:
        folderPreviewActive ? height : (modeVisual.item ? modeVisual.item.paintedHeight : height)

    readonly property bool pointerHovered:
        panelRoot.hoveredIndex === viewIndex
    // A sparse page hand-off may briefly invalidate the row facade while the
    // bounded replacement snapshot is being installed. Keep the already
    // supplied row paintable through the typed controller's one-row lookup;
    // this never scans or copies the catalog.
    readonly property string effectiveDisplayName:
        visualModel.valid
        ? displayName
        : (panelRoot.controllerReady
           ? panelRoot.controller.entryNameAt(sourceIndex) : "")
    readonly property bool folderPreviewRequested:
        visualGeometryReady && (masonryMode || gridMode || iconsMode) && panelRoot.controllerReady
        && panelRoot.controller.directoryPreviewEnabled
        && (visualModel.folderPreviewState === 2 || Boolean(model && model.folderView))
    readonly property bool folderPreviewActive:
        folderPreviewRequested && Boolean(folderLoader.item && folderLoader.item.hasUsablePreview)
    onFolderPreviewActiveChanged: if (panelRoot.benchmarkTracingEnabled) panelRoot.traceBenchmarkStage("directory.appearance.changed", {
        "entryId": entryId, "row": viewIndex, "active": folderPreviewActive,
        "requested": folderPreviewRequested, "facadeReady": Boolean(model)
    })
    readonly property real renderDpr:
        Math.max(0.01, Number(panelRoot.devicePixelRatio) || 1)
    readonly property point iconSceneOrigin: {
        // mapToItem() does not register dependencies on ancestor geometry.
        // Track geometry through the complete chain so host
        // panel placement invalidates the correction without any scrolling.
        let dependency = 0
        let ancestor = entry
        while (ancestor) {
            dependency += ancestor.x + ancestor.y + ancestor.scale + ancestor.rotation
            // Size does not affect an untransformed item's origin. Anchors
            // publish x/y when resizing actually moves it; subscribing every
            // row to every ancestor's size repeats the whole chain on resize.
            if (ancestor.scale !== 1 || ancestor.rotation !== 0)
                dependency += ancestor.width + ancestor.height + ancestor.transformOrigin
            ancestor = ancestor.parent
        }
        return entry.mapToItem(null, dependency * 0, dependency * 0)
    }
    readonly property real iconPixelAlignmentRevision: {
        if (!visible)
            return 0
        const viewport = panelRoot.cursorPixelGridViewportOrigin
        const preview = previewContainerItem
        return Number(viewport.x || 0) + Number(viewport.y || 0)
                + x + y + width + height
                + (preview ? preview.x + preview.y
                             + preview.width + preview.height : 0)
                + presentationMode
    }

    function snapIconExtent(value) {
        // sourceSize is an integer logical QSize. Derive the displayed
        // physical extent from that same integer so the provider's raster
        // and the rendered quad agree even at fractional DPR.
        return Math.max(0, Math.round(Math.round(Number(value || 0)) * renderDpr)
                           / renderDpr)
    }

    function iconPixelOffset(item) {
        if (!visible || !item || !item.parent || !item.visible)
            return Qt.point(0, 0)
        const revision = iconPixelAlignmentRevision
        const origin = iconSceneOrigin
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return Qt.point(
            Math.round(scenePoint.x * renderDpr) / renderDpr
                - scenePoint.x + revision * 0 + origin.x * 0,
            Math.round(scenePoint.y * renderDpr) / renderDpr
                - scenePoint.y + origin.y * 0)
    }

    readonly property bool current:
        panelRoot.visualCursorIndex === viewIndex
    readonly property bool selected: panelRoot.effectiveEntrySelected(
        entryId, visualModel.isSelected)
    readonly property bool cursorChromeSuppressed:
        current && panelRoot.cursorChromeTransitionActive
    readonly property bool cursorChromeExposesUnderlay:
        panelRoot.cursorChromeTransitionActive
        && panelRoot.cursorChromeCoveredIndex === viewIndex
    readonly property string highlightForegroundValue: {
        const cursor = current && panelRoot.showCursor
        if (cursor && selected)
            return visualModel.selectedCursorForeground
        if (cursor)
            return visualModel.cursorForeground
        if (selected)
            return visualModel.selectedForeground
        return visualModel.normalForeground
    }
    readonly property color highlightForeground:
        selected
        ? panelRoot.markedTextColor
        : (highlightForegroundValue !== ""
           ? highlightForegroundValue : panelRoot.foregroundColor)
    readonly property color semanticTextColor:
        highlightForegroundValue !== ""
        ? highlightForegroundValue : panelRoot.foregroundColor
    readonly property color itemTextColor:
        selected
        ? panelRoot.markedTextColor
        : (panelRoot.neutralFileTextColors
           ? (isFolder
              ? panelRoot.folderTextColor : panelRoot.fileTextColor)
           : semanticTextColor)
    readonly property color itemMetadataColor:
        selected
        ? panelRoot.markedTextColor
        : (panelRoot.neutralFileTextColors
           ? panelRoot.mutedColor
           : (highlightForegroundValue !== ""
              ? highlightForeground : panelRoot.mutedColor))
    readonly property color fallbackIconColor:
        selected
        ? panelRoot.markedTextColor
        : (isFolder
           ? (current && panelRoot.showCursor
              ? panelRoot.foregroundColor : panelRoot.folderIconColor)
           : (highlightForegroundValue !== ""
              ? highlightForeground : panelRoot.mutedColor))
    readonly property color highlightLabelBackground:
        (current && panelRoot.showCursor
         ? visualModel.cursorBackground : visualModel.normalBackground)
        || panelRoot.labelBackgroundColor
    readonly property rect effectivePreviewRect: {
        if (detailsMode || columnsMode) {
            // Keep the compact slot's padding as the row grows. Both the
            // shared preview and Details text layout consume this rectangle.
            const extent = Math.max(0, Math.round((height - 4) * renderDpr)
                                      / renderDpr)
            const origin = iconSceneOrigin
            const left = panelRoot.detailsRowInset
            const top = Math.max(0, (height - extent) / 2)
            return Qt.rect(Math.round((origin.x + left) * renderDpr) / renderDpr - origin.x,
                           Math.round((origin.y + top) * renderDpr) / renderDpr - origin.y,
                           extent, extent)
        }
        const rect = previewRect
        if (rect && rect.width > 0 && rect.height > 0)
            return rect
        const inset = masonryMode || gridMode ? 4 : 3
        return Qt.rect(inset, inset,
                       Math.max(0, width - inset * 2),
                       Math.max(0, height - inset * 2))
    }

    readonly property real compactImageContentWidth: {
        const extent = effectivePreviewRect.width
        if (!(detailsMode || columnsMode) || !isImage)
            return extent
        // Position a portrait inside the shared square slot rather than
        // shrinking that slot: every row's filename must retain one x-origin.
        const size = model && model.fullSize ? model.fullSize : null
        const imageWidth = Number(visualModel.imageWidth
                                  || (size && size.width) || 0)
        const imageHeight = Number(visualModel.imageHeight
                                   || (size && size.height) || 0)
        if (imageWidth <= 0 || imageHeight <= imageWidth)
            return extent
        return Math.max(0, Math.round(extent * imageWidth / imageHeight
                                       * renderDpr) / renderDpr)
    }

    function isLucideIconSource(source) {
        return panelRoot.iconResolver.isMonochrome(
                    iconKey, source ? source.toString() : "")
    }

    function isSystemFileIconSource(source) {
        return panelRoot.iconResolver.isSystemFileSource(
                    source ? source.toString() : "")
    }

    function systemFileFallbackSource(logicalSize) {
        return panelRoot.iconResolver.fallbackSource(
                    isFolder, isImage,
                    effectiveDisplayName === "..")
    }

    function sourceColorIconAtSize(source, logicalSize, tint) {
        const value = source ? source.toString() : ""
        return panelRoot.iconResolver.retargetProviderSource(
                    value,
                    Math.max(1, Math.round(Number(logicalSize) || 1)),
                    Math.max(0.5, Number(renderDpr) || 1),
                    tint === undefined || tint === null ? "" : String(tint),
                    isLucideIconSource(value))
    }

    GalleryEntrySelectionSurface {
        entry: entry
    }

    // Preview/icon rendering is common to every presentation. Keep exactly
    // one live preview primitive while the mode-specific text/layout layer is
    // exchanged. This avoids rebuilding image-provider and pixel-grid state
    // for every retained viewport row during a presentation transaction.
    GalleryEntryPreview {
        id: sharedPreview
        entry: entry
        shaderThumbnail: entry.masonryMode || entry.gridMode
        activePresentation: entry.visualGeometryReady && (!entry.folderPreviewRequested
            || entry.visualModel.folderPreviewState !== 2)
        z: 1
    }

    Loader {
        id: modeVisual
        active: entry.visualGeometryReady && !(entry.folderPreviewRequested
            && (entry.visualModel.folderPreviewState === 2 || entry.folderPreviewActive))
        visible: !entry.folderPreviewActive
        anchors.fill: parent
        asynchronous: false
        z: 2
        sourceComponent: entry.detailsMode ? detailsVisual
                         : entry.columnsMode ? columnsVisual
                         : entry.gridMode ? gridVisual
                         : entry.iconsMode ? iconsVisual
                         : masonryVisual
    }

    Component {
        id: masonryVisual
        GalleryMasonryEntryDelegate {
            anchors.fill: parent
            entry: entry
        }
    }

    Loader {
        id: folderLoader
        // This small shell follows the existing bounded delegate pool. Its
        // image grid has a separate lifetime inside GalleryFolderPreview.
        property bool frameCreated: false
        anchors.fill: parent
        z: 2
        active: entry.folderPreviewRequested || frameCreated
        visible: entry.folderPreviewRequested
        onLoaded: frameCreated = true
        sourceComponent: Component {
            GalleryFolderPreview { entry: entry }
        }
    }

    Component {
        id: columnsVisual
        ColumnsEntryDelegate {
            anchors.fill: parent
            entry: entry
        }
    }

    Component {
        id: detailsVisual
        DetailsEntryDelegate {
            anchors.fill: parent
            entry: entry
        }
    }

    Component {
        id: gridVisual
        GridEntryDelegate {
            anchors.fill: parent
            entry: entry
        }
    }

    Component {
        id: iconsVisual
        IconsEntryDelegate {
            anchors.fill: parent
            entry: entry
        }
    }

}
