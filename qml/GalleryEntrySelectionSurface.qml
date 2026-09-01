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
    readonly property rect visualSurfaceRect:
        pixelAlignedCursorGeometry
        ? entry.panelRoot.alignViewportItemRectToDevicePixels(
              entry, nominalSurfaceRect)
        : nominalSurfaceRect
    readonly property real nominalBorderWidth: {
        if (entry.cursorChromeExposesUnderlay)
            return 0
        if (entry.detailsMode || entry.columnsMode) {
            return entry.current && entry.panelRoot.showCursor
                    && !entry.cursorChromeSuppressed ? 1 : 0
        }
        if (entry.selected)
            return 3
        return entry.current && entry.panelRoot.showCursor
                && !entry.cursorChromeSuppressed ? 1 : 0
    }

    x: visualSurfaceRect.x
    y: visualSurfaceRect.y
    width: visualSurfaceRect.width
    height: visualSurfaceRect.height
    radius: entry.columnsMode || entry.detailsMode ? 4 : 6
    antialiasing: true
    color: {
        if (entry.cursorChromeExposesUnderlay)
            return "transparent"
        if (entry.detailsMode) {
            if (entry.cursorChromeSuppressed) {
                if (!entry.selected
                        && entry.nonCursorHighlightBackgroundValue !== "")
                    return entry.nonCursorHighlightBackgroundValue
            } else {
                if ((!entry.selected || (entry.current
                                          && entry.panelRoot.showCursor))
                        && entry.highlightBackgroundValue !== "")
                    return entry.highlightBackgroundValue
                if (entry.current && entry.panelRoot.showCursor)
                    return entry.panelRoot.cursorBackgroundColor
            }
            if (!entry.selected && entry.pointerHovered)
                return entry.panelRoot.itemHoverColor
            return "transparent"
        }
        if (entry.current && entry.panelRoot.showCursor
                && !entry.cursorChromeSuppressed)
            return entry.panelRoot.cursorColor
        if (entry.selected)
            return "transparent"
        if (entry.pointerHovered)
            return entry.panelRoot.itemHoverColor
        if (entry.masonryMode || entry.gridMode) {
            return entry.isFolder
                    ? entry.panelRoot.directoryBackgroundColor
                    : entry.panelRoot.itemBackgroundColor
        }
        return "transparent"
    }
    border.width: nominalBorderWidth
    border.pixelAligned: true
    border.color: entry.detailsMode
        ? entry.panelRoot.cursorBorderColor
        : (entry.columnsMode
           ? entry.panelRoot.cardCursorBorderColor
           : (entry.selected
              ? entry.panelRoot.selectionColor
              : (entry.current && entry.panelRoot.showCursor
                 ? entry.panelRoot.cardCursorBorderColor
                 : entry.panelRoot.selectionColor)))
}
