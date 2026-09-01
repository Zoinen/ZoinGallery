pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: diagnostics

    required property GalleryPanel panel

    function publishMetadataVisibleRange() {
        const indexes = panel.galleryLayout.visibleIndexes || []
        let first = -1
        let last = -1
        for (let offset = 0; offset < indexes.length; ++offset) {
            const row = Number(indexes[offset])
            if (!Number.isInteger(row) || row < 0)
                continue
            first = first < 0 ? row : Math.min(first, row)
            last = Math.max(last, row)
        }
        panel.metadataVisibleRangeChanged(first, last)
    }

    function state(extra) {
        const layout = panel.galleryLayout
        const controller = panel.controller
        const count = layout ? layout.count : 0
        const index = controller ? controller.currentIndex : -1
        const geometry = layout && index >= 0 && index < count
                ? layout.indexGeometry(index) : Qt.rect(0, 0, 0, 0)
        const geometryValid = geometry.width > 0 && geometry.height > 0
        const horizontal = panel.presentationMode === "columns"
        const viewportExtent = layout
                ? (horizontal ? layout.width : layout.height) : 0
        const contentExtent = layout ? layout.contentHeight : 0
        const maximum = Math.max(0, contentExtent - viewportExtent)
        const itemCenter = geometryValid
                ? (horizontal ? geometry.x + geometry.width / 2
                              : geometry.y + geometry.height / 2) : -1
        const target = geometryValid && viewportExtent > 0
                ? Math.max(0, Math.min(maximum,
                                      itemCenter - viewportExtent / 2)) : -1
        const contentY = layout ? layout.contentY : 0
        const result = {
            "currentPath": controller
                    ? String(controller.currentPath || "") : "",
            "catalogRevision": controller
                    ? Number(controller.catalogRevision || 0) : 0,
            "currentIndex": index,
            "cursorEntryId": controller
                    ? String(controller.cursorEntryId || "") : "",
            "presentationMode": String(panel.presentationMode || ""),
            "nativePresentationMode": layout
                    ? Number(layout.presentationMode) : -1,
            "count": count,
            "contentY": contentY,
            "contentHeight": contentExtent,
            "viewportWidth": layout ? layout.width : 0,
            "viewportHeight": layout ? layout.height : 0,
            "viewportExtent": viewportExtent,
            "maximumContentY": maximum,
            "targetContentY": target,
            "placementMatchesTarget": count === 0
                    || (target >= 0 && Math.abs(contentY - target) <= 0.51),
            "pathViewportPlacementPending":
                    panel.pathViewportPlacementPending,
            "placementTimerRunning": panel.pathPlacementTimerObject
                    ? panel.pathPlacementTimerObject.running : false,
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

    function trace(stage, extra) {
        if (panel.benchmarkTracingEnabled)
            panel.benchmarkStage(stage, state(extra || ({})))
    }
}
