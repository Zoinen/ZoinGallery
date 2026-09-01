pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: input

    required property GalleryPanel panel

    function ownsKey(event) {
        // The embedding shell may leave visual focus in the gallery while its
        // command line owns editing/navigation input. Decline the complete
        // gesture before matching individual keys so it can bubble intact.
        if (panel.hostCapabilities
                && panel.hostCapabilities.galleryOwnsPanelInput === false)
            return false
        const modifiers = event.modifiers
                & (Qt.ShiftModifier | Qt.ControlModifier
                   | Qt.AltModifier | Qt.MetaModifier)
        const spatial = event.key === Qt.Key_Left
                || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up
                || event.key === Qt.Key_Down
        if (spatial)
            return modifiers === Qt.NoModifier || modifiers === Qt.ShiftModifier
        if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown
                || event.key === Qt.Key_Home || event.key === Qt.Key_End)
            return modifiers === Qt.NoModifier || modifiers === Qt.ShiftModifier
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (panel.hostCapabilities
                    && panel.hostCapabilities.galleryOwnsReturn === false)
                return false
            return modifiers === Qt.NoModifier
        }
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Insert)
            return modifiers === Qt.NoModifier
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal
                || event.key === Qt.Key_Minus)
            return modifiers === Qt.NoModifier
        return false
    }

    function handleLocalQuickSearchKey(event) {
        if (!panel.localQuickSearchEnabled || !panel.controllerReady)
            return false
        const controller = panel.controller
        const active = controller.quickSearchActive
        const control = Boolean(event.modifiers
                                & (Qt.ControlModifier | Qt.MetaModifier))
        const alt = Boolean(event.modifiers & Qt.AltModifier)
        if (!active && ownsKey(event))
            return false
        if (active && event.key === Qt.Key_Escape) {
            controller.clearQuickSearch()
            return true
        }
        if (active && event.key === Qt.Key_Backspace && !control && !alt) {
            const characters = Array.from(controller.quickSearchQuery)
            characters.pop()
            controller.setQuickSearchQuery(characters.join(""))
            if (controller.quickSearchActive)
                controller.quickSearchNext(true, false, true)
            return true
        }
        if (active && (event.key === Qt.Key_F3
                || ((event.key === Qt.Key_Return
                     || event.key === Qt.Key_Enter) && control))) {
            controller.quickSearchNext(
                        !Boolean(event.modifiers & Qt.ShiftModifier),
                        true, true)
            return true
        }
        const printable = !control && !alt
                && event.text !== undefined && event.text.length > 0
                && event.text.charCodeAt(0) >= 0x20
                && panel.quickSearchExcludedCharacters.indexOf(event.text) < 0
        if (!printable)
            return false
        const candidate = controller.quickSearchQuery + event.text
        if (controller.setQuickSearchQuery(candidate))
            controller.quickSearchNext(true, false, true)
        return true
    }

    function isSpatialKey(key) {
        return key === Qt.Key_Left || key === Qt.Key_Right
                || key === Qt.Key_Up || key === Qt.Key_Down
    }

    function isPageKey(key) {
        return key === Qt.Key_PageUp || key === Qt.Key_PageDown
    }

    function prepareNavigation(isPage, event) {
        const mode = panel.galleryLayout.presentationMode
        if (!isPage || mode !== GalleryViewportItem.Grid)
            panel.resetGridPageLattice()
        if (!isPage || mode !== GalleryViewportItem.Masonry)
            panel.resetMasonryPageSequence()
        if (!event.isAutoRepeat)
            panel.navigationKeyHeld = true
    }

    function handleSpatialPress(event, shiftSelection) {
        const index = panel.navigationTargetForKey(event.key, false)
        const direction = event.key === Qt.Key_Left
                || event.key === Qt.Key_Up ? -1 : 1
        if (index >= 0) {
            panel.moveCursorWithSelection(
                        index, shiftSelection,
                        event.key === Qt.Key_Up
                            || event.key === Qt.Key_Down,
                        true, false, direction)
        } else if (shiftSelection) {
            panel.moveCursorWithSelection(
                        panel.controller.currentIndex, true, true,
                        false, false, direction)
        }
        event.accepted = true
        return index
    }

    function handlePagePress(event, shiftSelection) {
        const direction = event.key === Qt.Key_PageUp ? -1 : 1
        let index = -1
        if (panel.galleryLayout.presentationMode
                === GalleryViewportItem.Columns) {
            const key = direction < 0 ? Qt.Key_Up : Qt.Key_Down
            index = panel.navigationTargetForKey(key, true)
            if (index >= 0) {
                panel.moveCursorWithSelection(
                            index, shiftSelection, false,
                            true, false, direction)
            }
        } else {
            index = panel.navigateViewportPage(
                        direction, shiftSelection, true)
        }
        event.accepted = true
        return index
    }

    function handleEdgePress(event, shiftSelection) {
        const home = event.key === Qt.Key_Home
        const index = home ? 0 : panel.galleryLayout.count - 1
        panel.moveCursorWithSelection(
                    index, shiftSelection, false, true, false,
                    home ? -1 : 1)
        event.accepted = true
        return index
    }

    function handleNonNavigationPress(event) {
        const controller = panel.controller
        const layout = panel.galleryLayout
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            panel.cancelCursorChromeTransition()
            panel.commitPendingCursor()
            panel.selectIndex(controller.currentIndex, true, false,
                              event.isAutoRepeat)
            event.accepted = true
        } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Insert) {
            panel.cancelCursorChromeTransition()
            if (!event.isAutoRepeat) {
                if (panel.keyboardShiftSelectionActive)
                    panel.finishKeyboardShiftSelection()
                panel.beginKeyboardToggleSelection(event.key)
                panel.navigationKeyHeld = true
            } else if (panel.keyboardToggleSelectionKey !== event.key) {
                panel.beginKeyboardToggleSelection(event.key)
                panel.navigationKeyHeld = true
            }
            const currentIndex = controller.currentIndex
            panel.togglePendingKeyboardSelection(currentIndex)
            if (event.key === Qt.Key_Insert && currentIndex + 1 < layout.count)
                panel.moveCursor(currentIndex + 1, false, false,
                                 true, false, 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            panel.stepDensity(true)
            event.accepted = true
        } else if (event.key === Qt.Key_Minus) {
            panel.stepDensity(false)
            event.accepted = true
        }
    }

    function handlePressed(event) {
        if (!panel.controllerReady)
            return
        if (handleLocalQuickSearchKey(event)) {
            event.accepted = true
            return
        }
        const controller = panel.controller
        if (event.key === Qt.Key_Shift) {
            if (!event.isAutoRepeat)
                panel.beginKeyboardShiftSelection(controller.currentIndex, true)
            event.accepted = true
            return
        }
        if (!ownsKey(event)) {
            event.accepted = false
            return
        }

        const shiftSelection = Boolean(event.modifiers & Qt.ShiftModifier)
        const cursorIndexBeforePress = controller.currentIndex
        const spatial = isSpatialKey(event.key)
        const page = isPageKey(event.key)
        const edge = event.key === Qt.Key_Home || event.key === Qt.Key_End
        const navigation = spatial || page || edge
        const chromeSnapshot = navigation
                ? panel.cursorChromeNavigationSnapshot() : null
        let chromeTargetIndex = -1
        if (navigation) {
            prepareNavigation(page, event)
            if (spatial)
                chromeTargetIndex = handleSpatialPress(event, shiftSelection)
            else if (page)
                chromeTargetIndex = handlePagePress(event, shiftSelection)
            else
                chromeTargetIndex = handleEdgePress(event, shiftSelection)
        } else {
            handleNonNavigationPress(event)
        }
        if (chromeTargetIndex >= 0)
            panel.startCursorChromeForNavigation(chromeSnapshot,
                                                 chromeTargetIndex)
        if ((spatial || page) && !shiftSelection
                && panel.cursorCommitPending
                && controller.currentIndex === cursorIndexBeforePress)
            panel.refreshPendingCursorCommit()
    }

    function handleReleased(event) {
        if (!panel.controllerReady) {
            event.accepted = false
            return
        }
        if (event.key === Qt.Key_Shift) {
            if (event.isAutoRepeat)
                return
            panel.navigationKeyHeld = false
            const selectionCommitted = panel.finishKeyboardShiftSelection()
            if (!selectionCommitted)
                panel.commitCursorAfterNavigation()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Insert) {
            if (event.isAutoRepeat)
                return
            if (panel.keyboardToggleSelectionKey === event.key) {
                panel.navigationKeyHeld = false
                const selectionCommitted =
                        panel.finishKeyboardToggleSelection()
                if (!selectionCommitted)
                    panel.commitCursorAfterNavigation()
                event.accepted = true
                return
            }
        }
        if (!ownsKey(event)) {
            event.accepted = false
            return
        }
        const navigation = event.key === Qt.Key_Left
                || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up
                || event.key === Qt.Key_Down
                || event.key === Qt.Key_PageUp
                || event.key === Qt.Key_PageDown
                || event.key === Qt.Key_Home
                || event.key === Qt.Key_End
        if (!navigation)
            return
        event.accepted = true
        if (event.isAutoRepeat)
            return
        panel.navigationKeyHeld = false
        if (!panel.keyboardShiftSelectionActive)
            panel.commitCursorAfterNavigation()
    }
}
