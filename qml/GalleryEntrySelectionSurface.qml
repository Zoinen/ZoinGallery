pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: surface

    required property GalleryEntryDelegateBase entry
    objectName: "gallerySelectionSurface-" + entry.viewIndex
    readonly property real visualBorderWidth: border.width
    readonly property color visualBorderColor: border.color
    readonly property bool visualBorderPixelAligned: border.pixelAligned
    readonly property real surfaceInset: entry.detailsMode ? 0
                                          : (entry.columnsMode ? 1 : 2)
    readonly property rect nominalSurfaceRect: Qt.rect(
        surfaceInset, surfaceInset,
        Math.max(0, parent.width - surfaceInset * 2),
        entry.iconsMode
            ? Math.max(0, Math.min(parent.height - surfaceInset * 2,
                                   entry.paintedContentHeight - surfaceInset))
            : Math.max(0, parent.height - surfaceInset * 2))
    readonly property bool cursorVisible:
        entry.current && entry.panelRoot.showCursor && !entry.cursorChromeSuppressed
    readonly property bool pixelAlignedCursorGeometry:
        entry.current && entry.panelRoot.showCursor
        && !entry.panelRoot.cursorPixelAlignmentSuspended
        && !entry.geometryAnimationRunning
    readonly property bool pixelAlignedSelectionGeometry:
        entry.selected
        && !entry.panelRoot.cursorPixelAlignmentSuspended
        && !entry.geometryAnimationRunning
    readonly property rect visualSurfaceRect:
        pixelAlignedCursorGeometry || pixelAlignedSelectionGeometry
        ? entry.panelRoot.alignViewportItemRectToDevicePixels(
              entry, nominalSurfaceRect)
        : nominalSurfaceRect
    readonly property real nominalBorderWidth:
        cursorVisible && !entry.cursorChromeExposesUnderlay ? 1 : 0

    x: visualSurfaceRect.x
    y: visualSurfaceRect.y
    width: visualSurfaceRect.width
    height: visualSurfaceRect.height
    radius: entry.columnsMode || entry.detailsMode ? 4 : 6
    antialiasing: true
    readonly property color baseColor: {
        if (entry.cursorChromeExposesUnderlay)
            return "transparent"
        if (entry.selected)
            return entry.panelRoot.markedBackgroundColor
        if (entry.detailsMode) {
            if (cursorVisible)
                return entry.visualModel.cursorBackground
                        || entry.panelRoot.cursorBackgroundColor
            return entry.visualModel.normalBackground || "transparent"
        }
        if (cursorVisible)
            return entry.panelRoot.cursorColor
        if (entry.masonryMode) {
            if (entry.folderPreviewActive)
                return "transparent"
            return entry.isFolder
                    ? entry.panelRoot.directoryBackgroundColor
                    : entry.panelRoot.itemBackgroundColor
        }
        return "transparent"
    }
    // Hover is a visual overlay, not an alternative to cursor/selection state.
    // Compose the live palette tint with the resolved background so marked
    // rows and the current row respond too, without losing their text/border.
    color: {
        if (!entry.pointerHovered || entry.cursorChromeExposesUnderlay)
            return baseColor
        const hover = entry.selected
            ? Qt.alpha(entry.panelRoot.selectionColor, entry.panelRoot.itemHoverColor.a)
            : entry.panelRoot.itemHoverColor
        const alpha = hover.a + baseColor.a * (1 - hover.a)
        if (alpha <= 0)
            return baseColor
        // Qt.tint interpolates RGB as if the base were opaque. Normalize the
        // weight for transparent rows and translucent theme backgrounds.
        const mixed = Qt.tint(baseColor, Qt.rgba(
                                  hover.r, hover.g, hover.b, hover.a / alpha))
        return Qt.rgba(mixed.r, mixed.g, mixed.b, alpha)
    }
    border.width: nominalBorderWidth
    border.pixelAligned: true
    border.color: entry.selected && entry.panelRoot.showSelectionBorders
                  ? entry.panelRoot.selectionColor
                  : entry.detailsMode ? entry.panelRoot.cursorBorderColor
                  : entry.panelRoot.cardCursorBorderColor
}
