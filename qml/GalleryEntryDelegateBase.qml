pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

BrickItem {
    id: entry

    required property Item panelRoot
    property var model
    readonly property Item thumbnailItem: sharedPreview.thumbnailItem
    readonly property Item previewContainerItem: sharedPreview
    readonly property real paintedContentHeight:
        modeVisual.item ? modeVisual.item.paintedHeight : height

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
    readonly property bool folderPreviewActive:
        masonryMode && panelRoot.controllerReady
        && panelRoot.controller.directoryPreviewEnabled
        && Boolean(model && model.folderView)
    readonly property real renderDpr:
        Math.max(0.01, Number(panelRoot.devicePixelRatio) || 1)
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
        return Math.max(0, Math.round(Number(value || 0) * renderDpr)
                           / renderDpr)
    }

    function iconPixelOffset(item) {
        if (!visible || !item || !item.parent || !item.visible)
            return Qt.point(0, 0)
        const revision = iconPixelAlignmentRevision
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return Qt.point(
            Math.round(scenePoint.x * renderDpr) / renderDpr
                - scenePoint.x + revision * 0,
            Math.round(scenePoint.y * renderDpr) / renderDpr
                - scenePoint.y)
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
        if (detailsMode) {
            return Qt.rect(panelRoot.detailsRowInset,
                           Math.max(0, (height
                                        - panelRoot.detailsIconSlotSize) / 2),
                           panelRoot.detailsIconSlotSize,
                           panelRoot.detailsIconSlotSize)
        }
        const rect = previewRect
        if (rect && rect.width > 0 && rect.height > 0)
            return rect
        const inset = masonryMode || gridMode ? 4 : 3
        return Qt.rect(inset, inset,
                       Math.max(0, width - inset * 2),
                       Math.max(0, height - inset * 2))
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
        activePresentation: true
        z: 1
    }

    Loader {
        id: modeVisual
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
