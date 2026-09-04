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
    readonly property bool pixelAlignedCursorGeometry:
        entry.current && entry.panelRoot.showCursor
        && !entry.panelRoot.cursorPixelAlignmentSuspended
        && !entry.geometryAnimationRunning
    readonly property bool selectionBorderVisible:
        entry.selected && entry.panelRoot.showSelectionBorders
    readonly property bool pixelAlignedSelectionGeometry:
        selectionBorderVisible
        && !entry.panelRoot.cursorPixelAlignmentSuspended
        && !entry.geometryAnimationRunning
    readonly property rect visualSurfaceRect:
        pixelAlignedCursorGeometry || pixelAlignedSelectionGeometry
        ? entry.panelRoot.alignViewportItemRectToDevicePixels(
              entry, nominalSurfaceRect)
        : nominalSurfaceRect
    readonly property real nominalBorderWidth: {
        if (entry.cursorChromeExposesUnderlay)
            return 0
        if (selectionBorderVisible)
            return 1
        return entry.current && entry.panelRoot.showCursor
                && !entry.cursorChromeSuppressed ? 1 : 0
    }

    x: visualSurfaceRect.x
    y: visualSurfaceRect.y
    width: visualSurfaceRect.width
    height: visualSurfaceRect.height
    radius: entry.columnsMode || entry.detailsMode ? 4 : 6
    antialiasing: true
    readonly property color baseColor: {
        if (entry.cursorChromeExposesUnderlay)
            return "transparent"
        if (entry.detailsMode) {
            if (entry.current && entry.panelRoot.showCursor
                    && !entry.cursorChromeSuppressed)
                return entry.visualModel.cursorBackground
                        || entry.panelRoot.cursorBackgroundColor
            return entry.visualModel.normalBackground || "transparent"
        }
        if (entry.current && entry.panelRoot.showCursor
                && !entry.cursorChromeSuppressed)
            return entry.panelRoot.cursorColor
        if (entry.masonryMode || entry.gridMode) {
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
        const hover = entry.panelRoot.itemHoverColor
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
    border.color: selectionBorderVisible ? entry.panelRoot.selectionColor
                  : entry.detailsMode ? entry.panelRoot.cursorBorderColor
                  : entry.panelRoot.cardCursorBorderColor
}
