pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: root

    required property GalleryPanel panel
    required property GalleryPanelController controller
    required property GalleryViewportItem layoutItem
    required property NumberAnimation scrollAnimation
    required property ParallelAnimation chromeAnimation
    required property NumberAnimation xAnimation
    required property NumberAnimation yAnimation
    required property NumberAnimation widthAnimation
    required property NumberAnimation heightAnimation
    required property Timer finalizeTimer
    required property Timer layoutRetargetTimer

    readonly property GalleryViewportItem galleryLayout: layoutItem
    readonly property NumberAnimation panelScrollAnimation: scrollAnimation
    readonly property ParallelAnimation cursorChromeGeometryAnimation:
        chromeAnimation
    readonly property NumberAnimation cursorChromeXAnimation: xAnimation
    readonly property NumberAnimation cursorChromeYAnimation: yAnimation
    readonly property NumberAnimation cursorChromeWidthAnimation: widthAnimation
    readonly property NumberAnimation cursorChromeHeightAnimation:
        heightAnimation
    readonly property Timer cursorChromeFinalizeTimer: finalizeTimer
    readonly property Timer cursorChromeLayoutRetargetTimer:
        layoutRetargetTimer

    readonly property bool controllerReady: panel.controllerReady
    readonly property string presentationMode: panel.presentationMode
    readonly property real devicePixelRatio: panel.devicePixelRatio
    readonly property bool showCursor: panel.showCursor
    readonly property real currentItemCenterX: panel.currentItemCenterX
    readonly property real currentItemCenterY: panel.currentItemCenterY
    readonly property color cursorColor: panel.cursorColor
    readonly property color cardCursorBorderColor:
        panel.cardCursorBorderColor
    readonly property color cursorBackgroundColor:
        panel.cursorBackgroundColor
    readonly property color cursorBorderColor: panel.cursorBorderColor

    function resetGridPageLattice() {
        panel.resetGridPageLattice()
    }
    function resetMasonryPageSequence() {
        panel.resetMasonryPageSequence()
    }

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
    // A settled cursor is snapped in scene coordinates, not merely inside its
    // delegate: at fractional DPRs an integer delegate coordinate can still
    // land between physical pixels once the panel and viewport offsets are
    // included. Keep the live viewport origin observable so recycled
    // delegates update their correction after scrolling or panel relayout.
    readonly property point cursorPixelGridViewportOrigin: {
        // mapToItem() itself is not a bindable property. Read the relevant
        // item coordinates explicitly so moving this panel re-evaluates the
        // scene-space origin as well.
        const dependencyX = panel.x + panel.width
                + galleryLayout.x + galleryLayout.width
        const dependencyY = panel.y + panel.height
                + galleryLayout.y + galleryLayout.height
        const sceneOrigin = galleryLayout.mapToItem(
            null, dependencyX * 0, dependencyY * 0)
        return Qt.point(
            sceneOrigin.x + galleryLayout.paddingLeft
                - (presentationMode === "columns"
                   ? galleryLayout.contentY : 0),
            sceneOrigin.y - (presentationMode === "columns"
                             ? 0 : galleryLayout.contentY))
    }
    readonly property bool cursorPixelAlignmentSuspended:
        cursorChromeTransitionActive || panelScrollAnimation.running

    function alignViewportItemRectToDevicePixels(item, rect) {
        if (!item || !rect)
            return rect
        const dpr = Math.max(0.01, Number(devicePixelRatio) || 1)
        const origin = cursorPixelGridViewportOrigin
        const itemSceneX = origin.x + item.x
        const itemSceneY = origin.y + item.y
        const left = Math.round((itemSceneX + rect.x) * dpr) / dpr
        const top = Math.round((itemSceneY + rect.y) * dpr) / dpr
        const right = Math.round(
            (itemSceneX + rect.x + rect.width) * dpr) / dpr
        const bottom = Math.round(
            (itemSceneY + rect.y + rect.height) * dpr) / dpr
        return Qt.rect(left - itemSceneX, top - itemSceneY,
                       Math.max(0, right - left),
                       Math.max(0, bottom - top))
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
            index = galleryLayout.indexAt(0, currentItemCenterY)
        return index
    }

    function updateVisualCursorForViewport() {
        if (!controllerReady || galleryLayout.count <= 0) {
            visualCursorIndex = -1
            pendingVisualCursorIndex = -1
            return
        }
        const target = pendingVisualCursorIndex >= 0
                ? pendingVisualCursorIndex : controller.currentIndex
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
                ? visualCursorIndex
                : (controllerReady ? controller.currentIndex : -1)
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
                border: cardCursorBorderColor,
                borderWidth: 1,
                radius: cursorChromeModeRadius()
            }
        }
        const selected = controllerReady && controller.isSelectedAt(index)
        const style = controllerReady
                ? controller.highlightStyleAt(index) : ({})
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
        if (!snapshot || !snapshot.valid || !showCursor || !controllerReady
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
        if (!cursorChromeTransitionActive || !controllerReady
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
        if (wasActive && controllerReady) {
            // Once the independent keyboard transition is abandoned, no
            // intermediate anchor identity may leak into ordinary wheel,
            // scrollbar or focus semantics. The logical cursor owns its row
            // again (and is allowed to be outside the manually moved viewport).
            pendingVisualCursorIndex = -1
            visualCursorIndex = controller.currentIndex
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
        if (!controllerReady || targetIndex < 0
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
}
