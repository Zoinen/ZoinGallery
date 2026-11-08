pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.PixelGrid 1.0
import ZoinGallery as ZG

import ZoinGallery.Native 1.0

BrickItem {
    id: entry

    required property Item panelRoot
    Accessible.role: Accessible.ListItem
    Accessible.ignored: !entry.masonryMode || entry.viewIndex < 0
    Accessible.name: entry.effectiveDisplayName
    Accessible.selected: entry.selected
    accessibleChildrenDecorative: entry.masonryMode && !entry.panelRoot.controller.canRemoveEntries
    Accessible.onPressAction: {
        if (entry.masonryMode && entry.sceneVisible && entry.viewIndex >= 0)
            entry.panelRoot.selectIndex(entry.viewIndex, true)
    }
    property var model
    // Installed by the layout after committing the whole metadata-ready row.
    property bool masonryGeometryReady: false
    // Cleared only during synchronous photo-to-folder delegate reassignment.
    property bool visualGeometryReady: true
    // Temporary visual aid shared by every mode, including folder previews.
    property bool debugLayoutZones: panelRoot.debugLayoutZones
    readonly property Item thumbnailItem: sharedPreview.thumbnailItem
    readonly property Item previewContainerItem: sharedPreview
    readonly property real paintedContentHeight:
        folderPreviewActive ? height : (modeVisual.item ? modeVisual.item.paintedHeight : height)

    readonly property bool pointerHovered:
        viewIndex >= 0 && panelRoot.hoveredIndex === viewIndex
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
        && panelRoot.controller.thumbnailsEnabled
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
    readonly property real cellHorizontalInset: Math.min(
        Math.max(0, Math.floor((Math.round(width * renderDpr) - 1) / 2)) / renderDpr,
        Math.round(Math.max(0, panelRoot.metrics.cellHorizontalPadding) * renderDpr) / renderDpr)
    readonly property real cellContentWidth: Math.max(0, width - 2 * cellHorizontalInset)
    readonly property real cellCaptionSpacing:
        Math.round(panelRoot.metrics.cellCaptionSpacing * renderDpr) / renderDpr
    FontMetrics {
        id: standaloneCaptionMetrics
        font: entry.panelRoot.iconLabelFont
    }
    readonly property real masonryCaptionReserve: masonryMode && !isImage
        && !(visualModel && visualModel.isVideo)
        ? Math.ceil(standaloneCaptionMetrics.height * renderDpr) / renderDpr
          + cellCaptionSpacing : 0
    readonly property real cellVerticalInset: {
        // Native layout can clamp the requested inset to reserve caption space
        // or preserve a positive Masonry preview at minimum zoom.
        if ((masonryMode || gridMode || iconsMode)
            && previewWidth > 0 && previewHeight > 0)
            return Math.max(0, previewY)
        return Math.min(
            Math.max(0, Math.floor((Math.round(height * renderDpr) - 1) / 2)) / renderDpr,
            Math.round(Math.max(0, panelRoot.metrics.cellVerticalPadding) * renderDpr) / renderDpr)
    }
    readonly property bool iconPixelAlignmentActive:
        visible && sceneVisible && !geometryAnimationRunning
    readonly property bool motionActive:
        geometryAnimationRunning || panelRoot.cursorPixelAlignmentSuspended
    // Compatibility values for embedders; local placement is now owned by the
    // cached grid scope rather than reactive scene-origin dependency walks.
    readonly property point iconSceneOrigin: Qt.point(0, 0)
    readonly property real iconPixelAlignmentRevision: 0

    function snapIconExtent(value) {
        // sourceSize is an integer logical QSize. Derive the displayed
        // physical extent from that same integer so the provider's raster
        // and the rendered quad agree even at fractional DPR.
        return Math.max(0, Math.round(Math.round(Number(value || 0)) * renderDpr)
                           / renderDpr)
    }

    function iconPixelAlignment(item) {
        if (!iconPixelAlignmentActive || !item || !item.parent || !item.visible)
            return null
        return item.PixelGridAlignment
    }

    function iconPixelOffset(item) {
        if (!iconPixelAlignmentActive || !item || !item.parent || !item.visible)
            return Qt.point(0, 0)
        return item.PixelGridAlignment.offset
    }

    readonly property bool current:
        viewIndex >= 0 && panelRoot.visualCursorIndex === viewIndex
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
        : (visualModel.normalForeground !== ""
           ? visualModel.normalForeground
           : (isFolder ? panelRoot.folderIconColor : panelRoot.mutedColor))
    readonly property color highlightLabelBackground:
        masonryMode && selected
        ? Qt.alpha(panelRoot.markedBackgroundColor, ZG.Style.brickInfoPanelSelected.a)
        : ((current && panelRoot.showCursor
            ? (masonryMode ? ZG.Style.brickInfoPanelSelected : visualModel.cursorBackground)
            : visualModel.normalBackground)
           || panelRoot.labelBackgroundColor)
    // Keep component reads scalar: a rect read from QML creates a GC wrapper,
    // including a notify connection for referenced value types.
    readonly property bool compactPreview: detailsMode || columnsMode
    readonly property bool nativePreviewValid: previewWidth > 0 && previewHeight > 0
    readonly property bool useMasonryPreview: masonryMode && nativePreviewValid
        && (isImage || visualModel.isVideo)
    readonly property real compactPreviewExtent: compactPreview
        ? Math.max(0, Math.round((height
                - 2 * panelRoot.metrics.detailsIconVerticalPadding
                + panelRoot.detailsIconSlotSize - panelRoot.detailsIconSize)
                * renderDpr) / renderDpr) : 0
    readonly property real effectivePreviewX: {
        if (!compactPreview)
            return useMasonryPreview ? previewX : cellHorizontalInset
        const originX = detailsMode ? 0 : iconSceneOrigin.x
        return Math.round((originX + panelRoot.detailsRowInset) * renderDpr) / renderDpr - originX
    }
    readonly property real effectivePreviewY: {
        if (!compactPreview)
            return nativePreviewValid ? previewY : cellVerticalInset
        const originY = detailsMode ? 0 : iconSceneOrigin.y
        const top = Math.max(0, (height - compactPreviewExtent) / 2)
        return Math.round((originY + top) * renderDpr) / renderDpr - originY
    }
    readonly property real effectivePreviewWidth:
        compactPreview ? compactPreviewExtent
                       : useMasonryPreview ? previewWidth : cellContentWidth
    readonly property real effectivePreviewHeight: compactPreview
        ? compactPreviewExtent
        : (nativePreviewValid
           ? Math.max(1 / renderDpr, previewHeight - masonryCaptionReserve)
           : Math.max(0, height - cellVerticalInset * 2))
    // Preserve the public rectangle for embedders and geometric operations.
    readonly property rect effectivePreviewRect:
        Qt.rect(effectivePreviewX, effectivePreviewY,
                effectivePreviewWidth, effectivePreviewHeight)

    readonly property real compactImageContentWidth: {
        const extent = effectivePreviewWidth
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

    function isExactCompactIconSource(source) {
        const value = source.toString()
        return value.startsWith("image://")
                && (value.indexOf("/zoin/") >= 0 || value.indexOf("/lucide/") >= 0)
    }

    function systemFileFallbackSource(logicalSize) {
        return panelRoot.iconResolver.fallbackSource(
                    isFolder, isImage,
                    effectiveDisplayName === "..")
    }

    function sourceColorIconAtSize(source, logicalSize, tint) {
        const value = source ? source.toString() : ""
        const provider = panelRoot.iconProvider
        if (compactPreview && value.startsWith("qrc:/F4QtHost/icons/zoin/")
                && provider && typeof provider.compactFileArtworkSource === "function") {
            return provider.compactFileArtworkSource(value, Math.round(logicalSize), renderDpr)
        }
        const lucide = isLucideIconSource(value)
        const iconName = iconKey !== ""
                ? iconKey : panelRoot.iconResolver.keyFromSource(value)
        if (lucide && iconName !== "" && provider
                && typeof provider.rasterizedLucideSource === "function"
                && typeof provider.lucideStrokeWidth === "function") {
            const requestedSize = Math.max(1, Math.round(Number(logicalSize) || 1))
            const strokeWidth = provider.lucideStrokeWidth(
                        Math.max(1, Number(logicalSize) || 1))
            return fittedIconSource(provider.rasterizedLucideSource(
                        iconName, requestedSize,
                        Math.max(0.5, Number(renderDpr) || 1),
                        tint === undefined || tint === null ? "" : String(tint),
                        strokeWidth))
        }
        return fittedIconSource(panelRoot.iconResolver.retargetProviderSource(
                    value,
                    Math.max(1, Math.round(Number(logicalSize) || 1)),
                    Math.max(0.5, Number(renderDpr) || 1),
                    tint === undefined || tint === null ? "" : String(tint),
                    lucide))
    }

    function fittedIconSource(source) {
        const value = source ? source.toString() : ""
        return value.startsWith("image://") && value.indexOf("/lucide/") >= 0
                ? value + (value.indexOf("?") >= 0 ? "&" : "?") + "fileArt=1"
                  + (iconsMode ? "&alignBottom=1" : "")
                : value
    }

    GalleryEntrySelectionSurface {
        entry: entry
    }

    GalleryLayoutDebugZone {
        objectName: "galleryCellZone-" + entry.viewIndex
        zone: "cell"
        visible: entry.debugLayoutZones
    }

    GalleryLayoutDebugZone {
        objectName: "galleryPreviewZone-" + entry.viewIndex
        anchors.fill: undefined
        x: visible ? entry.effectivePreviewX : 0
        y: visible ? entry.effectivePreviewY : 0
        width: visible ? entry.effectivePreviewWidth : 0
        height: visible ? entry.effectivePreviewHeight : 0
        zone: "preview"
        visible: entry.debugLayoutZones && !entry.folderPreviewActive
    }

    // Preview/icon rendering is common to every presentation. Keep exactly
    // one live preview primitive while the mode-specific text/layout layer is
    // exchanged. This avoids rebuilding image-provider and pixel-grid state
    // for every retained viewport row during a presentation transaction.
    GalleryEntryPreview {
        id: sharedPreview
        entry: entry
        shaderThumbnail: entry.masonryMode || entry.gridMode
        activePresentation: entry.viewIndex >= 0
            && entry.visualGeometryReady && !entry.visualIdentityChanging
            && (!entry.folderPreviewRequested
            || entry.visualModel.folderPreviewState !== 2)
        z: 1
    }

    GallerySceneItem {
        anchors.fill: parent
        z: 2
        sceneVisible: !entry.folderPreviewActive
        visible: (entry.masonryMode && nativeCullingAvailable) || sceneVisible
        GalleryVisualHost {
            id: modeVisual
            objectName: "galleryEntryModeHost-" + entry.viewIndex
            entry: entry
            active: entry.visualGeometryReady && !(entry.folderPreviewRequested
                && (entry.visualModel.folderPreviewState === 2 || entry.folderPreviewActive))
            anchors.fill: parent
            asynchronous: false
            sourceComponent: entry.detailsMode ? entry.panelRoot.entryVisualFactories.details
                             : entry.columnsMode ? entry.panelRoot.entryVisualFactories.columns
                             : entry.gridMode ? entry.panelRoot.entryVisualFactories.grid
                             : entry.iconsMode ? entry.panelRoot.entryVisualFactories.icons
                             : entry.panelRoot.entryVisualFactories.masonry
        }
    }

    GallerySceneItem {
        anchors.fill: parent
        z: 2
        sceneVisible: entry.folderPreviewRequested
        visible: (entry.masonryMode && nativeCullingAvailable) || sceneVisible
        GalleryVisualHost {
            id: folderLoader
            objectName: "galleryEntryFolderHost-" + entry.viewIndex
            entry: entry
            // This small shell follows the existing bounded delegate pool. Its
            // image grid has a separate lifetime inside GalleryFolderPreview.
            property bool frameCreated: false
            anchors.fill: parent
            active: entry.folderPreviewRequested || frameCreated
            onLoaded: frameCreated = true
            sourceComponent: entry.panelRoot.entryVisualFactories.folder
        }
    }


}
