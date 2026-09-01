pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: reconciler

    required property GalleryPanel panel

    function ensureSessionPreviews() {
        if (!panel.controllerReady)
            return
        panel.controller.ensurePreviews()
        // The model can predate the QML viewport. Seed only the native
        // viewport/overscan decode range after its geometry is available.
        Qt.callLater(panel.galleryLayout.reReadAndDecodeThumbnails)
    }

    function resetControllerState() {
        panel.clearPendingKeyboardSelection()
        panel.cancelCursorChromeTransition()
        // setSession() first exposes the backend cursor and only then emits
        // sessionChanged. Discard the ensure-with-animation request produced
        // by that intermediate signal: attaching/recreating a presentation is
        // a viewport restore transaction and must place its first frame
        // atomically at the saved or authoritative cursor.
        panel.viewportUpdateTimerObject.stop()
        panel.viewportUpdatePendingAfterScroll = false
        panel.viewportUpdateEnsuresCursor = false
        panel.viewportUpdateSuppressAnimation = false
        panel.cursorCommitTimerObject.stop()
        panel.cursorAfterScrollTimerObject.stop()
        panel.cursorCommitPending = false
        panel.cursorCommitAfterScroll = false
        panel.navigationKeyHeld = false
        panel.currentItemCenterX = -1
        panel.currentItemCenterY = -1
        panel.visualCursorIndex = panel.controllerReady
                ? panel.controller.currentIndex : -1
        panel.pendingVisualCursorIndex = -1
        ensureSessionPreviews()
        panel.selectionAnchorIndex = panel.controllerReady
                ? panel.controller.currentIndex : -1
        panel.scheduleViewportUpdate(false)
    }

    function initialize() {
        resetControllerState()
        if (panel.autoFocus)
            panel.forceActiveFocus()
    }

    property Connections intentConnections: Connections {
        target: reconciler.panel.controller

        function onCursorIntentRequested(entryId, viewIndex, sourceIndex,
                                         catalogRevision, localRevision,
                                         deferred) {
            // Keep the old host signal adapter at the embedding edge. The
            // common controller itself remains stable-ID/revision based.
            reconciler.panel.cursorRequested(entryId, sourceIndex, deferred)
        }

        function onSelectionIntentRequested(selectedEntryIds,
                                            deselectedEntryIds,
                                            catalogRevision,
                                            localRevision) {
            const panel = reconciler.panel
            const changes = []
            for (let index = 0; index < selectedEntryIds.length; ++index) {
                changes.push({
                    entryId: selectedEntryIds[index],
                    selected: true
                })
            }
            for (let index = 0; index < deselectedEntryIds.length; ++index) {
                changes.push({
                    entryId: deselectedEntryIds[index],
                    selected: false
                })
            }

            let cursorEntryId = ""
            let cursorIndex = -1
            if (panel.cursorCommitPending && panel.controllerReady
                    && panel.controller.currentIndex >= 0) {
                cursorEntryId = panel.controller.entryIdAt(
                            panel.controller.currentIndex)
                cursorIndex = panel.sourceIndex(panel.controller.currentIndex)
                panel.cursorCommitPending = false
                panel.cursorCommitAfterScroll = false
                panel.cursorAfterScrollTimerObject.stop()
                panel.cursorCommitTimerObject.stop()
                panel.controller.cancelPendingCursor()
            }
            panel.selectionTransactionRequested(changes, cursorEntryId,
                                                cursorIndex)
        }
    }

    property Connections stateConnections: Connections {
        target: reconciler.panel.controller

        function onCatalogRevisionChanged() {
            const panel = reconciler.panel
            panel.traceBenchmarkStage("session.catalog.changed", {})
            panel.pathViewportCatalogReady = panel.controller.catalogReady
            panel.clearPendingKeyboardSelection()
            panel.cancelCursorChromeTransition()
            reconciler.ensureSessionPreviews()
            panel.resetCurrentItemCenter(panel.controller.currentIndex)
            panel.selectionAnchorIndex = panel.controller.currentIndex
            panel.coordinateVisualCursor(panel.controller.currentIndex,
                                         panel.visualCursorIndex)
            panel.schedulePathViewportPlacement("catalog-revision-changed")
        }

        function onSelectionRevisionChanged() {
            reconciler.panel.reconcileAcknowledgedKeyboardSelection()
        }

        function onCurrentIndexChanged() {
            const panel = reconciler.panel
            panel.traceBenchmarkStage("session.index.changed", {})
            if (panel.localCursorNavigation)
                return
            const previousVisualIndex = panel.visualCursorIndex
            panel.resetCurrentItemCenter(panel.controller.currentIndex)
            panel.selectionAnchorIndex = panel.controller.currentIndex
            panel.coordinateVisualCursor(panel.controller.currentIndex,
                                         previousVisualIndex)
            if (panel.pathViewportPlacementPending) {
                if (!panel.placeViewportForPathChange())
                    panel.schedulePathViewportPlacement(
                                "current-index-changed")
            } else {
                panel.scheduleViewportUpdate(true)
            }
        }

        function onCurrentPathChanged() {
            const panel = reconciler.panel
            panel.traceBenchmarkStage("session.path.changed", {})
            panel.galleryLayout.prepareViewportForModelReset(
                        panel.controller.currentIndex,
                        panel.controller.panelScrollOffset,
                        panel.controller.panelViewportStateAvailable)
            panel.pathViewportPlacementPending =
                    panel.presentationMode === "details"
            panel.pathViewportCatalogReady =
                    !panel.pathViewportPlacementPending
                    || panel.controller.catalogReady
            if (panel.pathViewportPlacementPending) {
                panel.viewportUpdateTimerObject.stop()
                panel.viewportUpdatePendingAfterScroll = false
                panel.viewportUpdateEnsuresCursor = false
                panel.viewportUpdateSuppressAnimation = false
                panel.schedulePathViewportPlacement("current-path-changed")
            } else {
                panel.viewportUpdateSuppressAnimation = true
            }
            panel.resetCurrentItemCenter(panel.controller.currentIndex)
            panel.selectionAnchorIndex = panel.controller.currentIndex
        }

        function onCatalogReadyChanged() {
            const panel = reconciler.panel
            panel.pathViewportCatalogReady = panel.controller.catalogReady
            if (panel.pathViewportPlacementPending
                    && panel.pathViewportCatalogReady
                    && !panel.placeViewportForPathChange()) {
                panel.schedulePathViewportPlacement("catalog-ready-changed")
            }
        }

        function onPanelViewportChanged() {
            const panel = reconciler.panel
            if (!panel.restoringScrollOffset
                    && Math.abs(panel.galleryLayout.contentY
                                - panel.controller.panelScrollOffset) > 0.5)
                panel.scheduleViewportUpdate(false)
        }
    }

    property Connections lifecycleConnections: Connections {
        target: reconciler.panel

        function onPathViewportPlacementPendingChanged() {
            reconciler.panel.traceBenchmarkStage(
                        "placement.pending.changed", {})
        }

        function onSessionChanged() {
            reconciler.resetControllerState()
        }

        function onShowCursorChanged() {
            if (!reconciler.panel.showCursor)
                reconciler.panel.cancelCursorChromeTransition()
        }

        function onViewerTransitionActiveChanged() {
            if (reconciler.panel.viewerTransitionActive)
                reconciler.panel.cancelCursorChromeTransition()
        }

        function onActiveFocusChanged() {
            const panel = reconciler.panel
            if (panel.activeFocus)
                return
            panel.navigationKeyHeld = false
            panel.cancelCursorChromeTransition()
            const selectionCommitted = panel.finishKeyboardSelectionGesture()
            if (!selectionCommitted)
                panel.commitPendingCursor()
        }
    }
}
