import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

import ZoinGallery 1.0
import ZoinGallery.Native 1.0

MouseArea {
    id: masonryView

    property alias view: masonryLayout
    property alias focusProxy: quickSearchField
    property alias targetHeight: masonryLayout.targetHeight
    property alias presentationMode: masonryLayout.presentationMode
    property alias columnCount: masonryLayout.columnCount
    property alias density: masonryLayout.density
    property alias masonrySpacing: masonryLayout.spacing
    property var masonryModel: galleryViewModel
    property Component masonryDelegate: BrickDelegate {}
    property var selectionModel: fileListModel
    property var selectionMapper: galleryViewModel
    property bool primaryView: true
    property bool selectionInteractionEnabled: primaryView
    property bool quickSearchEnabled: primaryView
    property bool viewerTransitionActive: false
    property color selectionAccentColor: fileListModel.activeSelectionGroupColor

    signal toggleViewer()
    signal currentIndexActivated(int index)
    signal currentSelectionToggleRequested(int index)
    signal fileDropFailed(string title, string message)

    // <Scrolling>
    property alias scrollingStarted: scrollingController.scrollingStarted
    property alias scrollingStartedAtY: scrollingController.startCoordinate
    property alias scrollingMode: scrollingController.scrollingMode
    property bool quickSearchMode: false

    property bool disableAnimation: false
    property bool scrolled: masonryLayout.needScroll && masonryScroll.position > 0

    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
    clip: true

    function focusView() {
        if (quickSearchEnabled) {
            focusProxy.forceActiveFocus()
        }
        else {
            masonryView.forceActiveFocus()
        }
    }

    function minimumDensity() {
        if (masonryLayout.presentationMode === MasonryLayout.Columns
                || masonryLayout.presentationMode === MasonryLayout.Details)
            return 22
        if (masonryLayout.presentationMode === MasonryLayout.Grid)
            return 96
        if (masonryLayout.presentationMode === MasonryLayout.Icons)
            return 72
        return 30
    }

    function maximumDensity() {
        if (masonryLayout.presentationMode === MasonryLayout.Columns
                || masonryLayout.presentationMode === MasonryLayout.Details)
            return 72
        if (masonryLayout.presentationMode === MasonryLayout.Grid)
            return 320
        if (masonryLayout.presentationMode === MasonryLayout.Icons)
            return 256
        return 500
    }

    function startScrolling() {
        scrollingController.start()
    }

    function endScrolling() {
        scrollingController.end()
        hideHovered = false
    }

    function resetCurrentItemCenter() {
        let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
        currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
        currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
    }

    function restoreStateAfterLayoutRebuild() {
        resetCurrentItemCenter()
        if (shiftSelectionActive && shiftSelectionAnchorPath !== "") {
            let remappedAnchor = -1
            for (let i = 0; i < masonryLayout.count; ++i) {
                if (masonryLayout.indexFullPath(i) ===
                        shiftSelectionAnchorPath) {
                    remappedAnchor = i
                    break
                }
            }
            if (remappedAnchor === -1) {
                selectionModel.cancelSelectionPreview()
                shiftSelectionActive = false
                shiftSelectionAnchorIndex = -1
                shiftSelectionAnchorPath = ""
            }
            else {
                shiftSelectionAnchorIndex = remappedAnchor
            }
        }
    }

    onPressed: (mouse) => {
        if (mouse.button === Qt.LeftButton) {
            focusView()
            let p = masonryLayout.mapFromItem(masonryView, mouse.x, mouse.y)
            if (selectionInteractionEnabled &&
                    p.x >= 0 && p.y >= 0 && p.x <= masonryLayout.width && p.y <= masonryLayout.height &&
                    masonryLayout.indexAtViewport(p.x, p.y) === -1) {
                startRubberBand(mouse.x, mouse.y, mouse.modifiers)
            }
        }
        else if (mouse.button === Qt.MiddleButton) {
            if (!scrollingMode) {
                startScrolling()
            }
            else {
                endScrolling()
            }
        }
        else if (mouse.button === Qt.RightButton) {
            if (scrollingMode) {
                endScrolling()
            }
            if (selectionInteractionEnabled) {
                focusView()
                let p = masonryLayout.mapFromItem(masonryView, mouse.x, mouse.y)
                let viewIndex = masonryLayout.indexAtViewport(p.x, p.y)
                if (viewIndex !== -1) {
                    setCurrentIndex(viewIndex, false, false, false, true)
                    selectionModel.toggleSelection(sourceIndexForViewIndex(viewIndex))
                }
            }
        }
    }

    function singleItemDragRequested(modifiers) {
        // Option is macOS's native "copy while dragging" modifier, so keep it
        // free there. Physical Control is exposed as MetaModifier on macOS.
        const modifier = Qt.platform.os === "osx"
                ? Qt.MetaModifier : Qt.AltModifier
        return Boolean(modifiers & modifier)
    }

    onReleased: (mouse) => {
        if (mouse.button === Qt.LeftButton && selectionInteractionEnabled) {
            finishRubberBand()
        }
        else {
            if (scrollingStarted) {
                endScrolling()
            }
        }
    }

    AutoScrollController {
        id: scrollingController
        objectName: "masonryScrollingController"
        layout: masonryLayout
        pointerSource: masonryView
        scrollExtent: topLevelWindow
                     ? topLevelWindow.availableScreenHeight()
                     : masonryView.height
        onScrollingStartedChanged: {
            if (scrollingStarted)
                masonryView.hideHovered = true
        }
    }

    // </Scrolling>

    hoverEnabled: true
    onPositionChanged: {
        if (rubberBandPending || rubberBandActive) {
            rubberBandCurrentX = mouseX
            rubberBandCurrentY = mouseY
            updateRubberBandSelection()
        }
        if (!scrollingMode) {
            hideHovered = false
        }
    }

    property bool hideHovered: false
    property real currentItemCenterX: 0
    property real currentItemCenterY: 0
    property bool alwaysShowFileNames: false
    property bool shiftSelectionActive: false
    property int shiftSelectionAnchorIndex: -1
    property string shiftSelectionAnchorPath: ""
    property bool shiftNavigationSelectionValue: true
    property string shiftSelectionDescription: "Range selection"
    readonly property int rubberBandModeAdd: 0
    readonly property int rubberBandModeDeselect: 1
    readonly property int rubberBandModeReplace: 2
    readonly property int rubberBandModeToggle: 3
    readonly property real rubberBandThreshold: 4
    property bool rubberBandPending: false
    property bool rubberBandActive: false
    property int rubberBandMode: rubberBandModeReplace
    property real rubberBandStartX: 0
    property real rubberBandStartY: 0
    property real rubberBandCurrentX: 0
    property real rubberBandCurrentY: 0

    Connections {
        target: masonryLayout
        function onLayoutReset() {
            restoreStateAfterLayoutRebuild()
        }
        function onCountChanged() {
            // countChanged is emitted after a model rebuild has finished its
            // rewrap and viewport restoration. Unlike layoutReset it carries
            // no "re-decode thumbnails" semantics for folder delegates.
            restoreStateAfterLayoutRebuild()
        }
    }

    Connections {
        target: viewerController
        enabled: masonryView.primaryView

        function onCurrentPathChanged() {
            resetCurrentItemCenter()
        }

        function onSetCurrentIndex(index) {
            masonryLayout.currentIndex = index
            ensureColumnWindowForIndex(masonryLayout.currentIndex)
            let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
            currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
            let indexGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
            let newContentY = indexGeometry.y + indexGeometry.height / 2 - masonryLayout.height / 2
            newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
            masonryLayout.contentY = newContentY

            currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
            hideHovered = true
        }
    }

    function moveInImageList(forward, toEnd) {
        let nextIndex = masonryLayout.nextImageIndex(forward, toEnd)
        setCurrentIndex(nextIndex)
        return nextIndex
    }

    function currentItemImageGeometry() {
        const item = masonryLayout.currentItem
        if (!item)
            return Qt.rect(0, 0, 0, 0)
        let preview = item.previewRect
        if (!preview || preview.width <= 0 || preview.height <= 0) {
            const inset = masonryLayout.spacing / 2
            preview = Qt.rect(inset, inset,
                              Math.max(0, item.width - inset * 2),
                              Math.max(0, item.height - inset * 2))
        }
        // Map from the actual media rectangle rather than reconstructing it
        // from the whole brick. This keeps the viewer expansion attached to
        // the visible thumbnail in Details/Grid/Icons and also accounts for
        // the Details header and the scrolled viewport transform.
        const topLeft = masonryView.mapFromItem(item, preview.x, preview.y)
        const bottomRight = masonryView.mapFromItem(
                    item, preview.x + preview.width,
                    preview.y + preview.height)
        return Qt.rect(Math.min(topLeft.x, bottomRight.x),
                       Math.min(topLeft.y, bottomRight.y),
                       Math.abs(bottomRight.x - topLeft.x),
                       Math.abs(bottomRight.y - topLeft.y))
    }

    function setCurrentIndex(index, keepLastPosX = false, keepLastPosY = false, neverScroll = false, keepQuickSearch = false) {
        masonryLayout.currentIndex = index
        ensureColumnWindowForIndex(masonryLayout.currentIndex)
        let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
        if (!keepLastPosX) {
            currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
        }
        if (!keepLastPosY) {
            if (!neverScroll) {
                ensureVisible(masonryLayout.currentIndex)
            }
            currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
        }
        hideHovered = true
        if (!keepQuickSearch) {
            hideQuickSearch()
        }
    }

    function scrollBy(deltaY, quickScroll) {
        let newContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY) + deltaY
        newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
        scrollAnimation2.from = masonryLayout.contentY
        scrollAnimation2.to = newContentY
        scrollAnimation2.duration = quickScroll ? 15 : 150
        scrollAnimation2.restart()
    }

    function ensureVisible(index) {
        ensureColumnWindowForIndex(index)
        let indexGeometry = masonryLayout.indexGeometry(index)
        let newContentY = -1
        if (indexGeometry.y < masonryLayout.contentY) {
            newContentY = indexGeometry.y
        }
        else if (indexGeometry.y + indexGeometry.height > masonryLayout.contentY + masonryLayout.height) {
            newContentY = indexGeometry.y + indexGeometry.height - masonryLayout.height
        }

        if (newContentY !== -1) {
            newContentY = Math.max(0, Math.min(masonryLayout.contentHeight - masonryLayout.height, newContentY))
            if (!masonryView.disableAnimation) {
                scrollAnimation2.from = masonryLayout.contentY
                scrollAnimation2.to = newContentY
                scrollAnimation2.duration = 150
                scrollAnimation2.restart()
            }
            else {
                masonryLayout.contentY = newContentY
            }

        }
    }

    function ensureColumnWindowForIndex(index) {
        if (masonryLayout.presentationMode !== MasonryLayout.Columns
                || index < 0 || index >= masonryLayout.count)
            return
        const geometry = masonryLayout.indexGeometry(index)
        if (geometry.width > 0 && geometry.height > 0)
            return
        const top = masonryLayout.windowTopIndexForIndex(index)
        if (top >= 0 && top !== masonryLayout.windowTopIndex)
            masonryLayout.windowTopIndex = top
    }

    function hideQuickSearch() {
        if (quickSearchMode) {
            quickSearchField.text = ""
            masonryLayout.quickSearch.mask = quickSearchField.text
            backspaceDisabledUntilKeyUp = true
            quickSearchMode = false
        }
    }

    function searchNext(forward) {
        let nextIndex = masonryLayout.quickSearch.nextImage(forward, true)
        setCurrentIndex(nextIndex, /*<defaults>*/ false, false, false /*</defaults>*/, true)
    }

    function sourceIndexForViewIndex(viewIndex) {
        return selectionMapper.mapToSourceRow(viewIndex)
    }

    function currentSourceIndex() {
        return sourceIndexForViewIndex(masonryLayout.currentIndex)
    }

    function beginShiftSelection() {
        if (shiftSelectionActive) {
            return
        }
        shiftSelectionActive = true
        shiftSelectionAnchorIndex = masonryLayout.currentIndex
        shiftSelectionAnchorPath =
                masonryLayout.indexFullPath(shiftSelectionAnchorIndex)
        shiftNavigationSelectionValue = true
        shiftSelectionDescription = "Range selection"
        selectionModel.beginSelectionPreview()
    }

    function updateShiftNavigationSelection(targetIndex) {
        beginShiftSelection()
        shiftSelectionDescription = shiftNavigationSelectionValue ? "Range selection" : "Range deselection"
        selectionModel.previewSelectionIndexes(
                    selectionMapper.sourceRowsForViewRange(shiftSelectionAnchorIndex, targetIndex, false),
                    shiftNavigationSelectionValue ? rubberBandModeAdd : rubberBandModeDeselect)
    }

    function shiftClickSelection(targetIndex) {
        beginShiftSelection()
        shiftSelectionDescription = "Range selection"
        selectionModel.previewSelectionIndexes(
                    selectionMapper.sourceRowsForViewRange(shiftSelectionAnchorIndex, targetIndex, true),
                    rubberBandModeAdd)
        setCurrentIndex(targetIndex, false, false, false, true)
    }

    function toggleCurrentSelection() {
        setCurrentIndex(masonryLayout.currentIndex,
                        false, false, false, true)
        selectionModel.toggleSelection(currentSourceIndex())
    }

    function finishShiftSelection() {
        if (!shiftSelectionActive) {
            return
        }
        selectionModel.commitSelectionPreview(shiftSelectionDescription)
        shiftSelectionActive = false
        shiftSelectionAnchorIndex = -1
        shiftSelectionAnchorPath = ""
    }

    function updateSelectionAfterKeyboardNavigation(event) {
        if (selectionInteractionEnabled && event.modifiers & Qt.ShiftModifier) {
            updateShiftNavigationSelection(masonryLayout.currentIndex)
        }
    }

    function keyboardNavigationChangesSelection(modifiers) {
        return selectionInteractionEnabled &&
               Boolean(modifiers & Qt.ShiftModifier)
    }

    function startRubberBand(mouseX, mouseY, modifiers) {
        rubberBandPending = true
        rubberBandActive = false
        rubberBandStartX = mouseX
        rubberBandStartY = mouseY
        rubberBandCurrentX = mouseX
        rubberBandCurrentY = mouseY
        if (modifiers & Qt.ControlModifier) {
            rubberBandMode = rubberBandModeToggle
        }
        else if (modifiers & Qt.ShiftModifier) {
            rubberBandMode = rubberBandModeAdd
        }
        else {
            rubberBandMode = rubberBandModeReplace
        }
    }

    function rubberBandMovedFarEnough() {
        return Math.abs(rubberBandCurrentX - rubberBandStartX) >= rubberBandThreshold ||
               Math.abs(rubberBandCurrentY - rubberBandStartY) >= rubberBandThreshold
    }

    function activateRubberBandIfNeeded() {
        if (rubberBandActive) {
            return true
        }
        if (!rubberBandPending || !rubberBandMovedFarEnough()) {
            return false
        }
        rubberBandActive = true
        selectionModel.beginSelectionPreview()
        return true
    }

    function rubberBandDescription() {
        if (rubberBandMode === rubberBandModeToggle) {
            return "Rubber band toggle selection"
        }
        if (rubberBandMode === rubberBandModeAdd) {
            return "Rubber band add selection"
        }
        return "Rubber band selection"
    }

    function updateRubberBandSelection() {
        if (!activateRubberBandIfNeeded()) {
            return
        }
        let x1 = Math.min(rubberBandStartX, rubberBandCurrentX)
        let y1 = Math.min(rubberBandStartY, rubberBandCurrentY)
        let x2 = Math.max(rubberBandStartX, rubberBandCurrentX)
        let y2 = Math.max(rubberBandStartY, rubberBandCurrentY)
        let p = masonryLayout.mapFromItem(masonryView, x1, y1)
        let indexes = masonryLayout.indexesInViewportRect(p.x, p.y, x2 - x1, y2 - y1)
        selectionModel.previewSelectionIndexes(selectionMapper.mapToSourceRows(indexes), rubberBandMode)
    }

    function finishRubberBand() {
        if (!rubberBandPending && !rubberBandActive) {
            return
        }

        if (rubberBandActive) {
            selectionModel.commitSelectionPreview(rubberBandDescription())
        }
        else {
            if (shiftSelectionActive) {
                selectionModel.cancelSelectionPreview()
            }
            selectionModel.setAllSelection(false)
        }

        if (shiftSelectionActive) {
            shiftSelectionActive = false
            shiftSelectionAnchorIndex = -1
            shiftSelectionAnchorPath = ""
        }
        rubberBandPending = false
        rubberBandActive = false
    }

    function handleItemPressed(viewIndex, modifiers) {
        focusView()
        if (scrollingMode) {
            endScrolling()
        }

        if (modifiers & Qt.ShiftModifier) {
            shiftClickSelection(viewIndex)
            return
        }
        setCurrentIndex(viewIndex, false, false, false,
                        Boolean(modifiers & Qt.ControlModifier))
        if (modifiers & Qt.ControlModifier) {
            selectionModel.toggleSelection(currentSourceIndex())
        }
    }

    function sortDetailsBy(role) {
        if (role === "size") {
            galleryViewModel.sortMode = galleryViewModel.sortMode === 6 ? 7 : 6
        } else {
            galleryViewModel.sortMode = galleryViewModel.sortMode === 0 ? 1 : 0
        }
    }

    function collectionDirection(key) {
        if (key === Qt.Key_Left)
            return MasonryLayout.NavigateLeft
        if (key === Qt.Key_Right)
            return MasonryLayout.NavigateRight
        if (key === Qt.Key_Up || key === Qt.Key_PageUp)
            return MasonryLayout.NavigateUp
        return MasonryLayout.NavigateDown
    }

    function navigateCollection(event, page) {
        const result = masonryLayout.navigationTarget(
                         masonryLayout.currentIndex,
                         collectionDirection(event.key), Boolean(page))
        if (!result || Number(result.targetIndex) < 0)
            return false
        const nextTop = Number(result.windowTopIndex)
        if (nextTop >= 0 && nextTop !== masonryLayout.windowTopIndex)
            masonryLayout.windowTopIndex = nextTop
        setCurrentIndex(Number(result.targetIndex), false, false, false,
                        keyboardNavigationChangesSelection(event.modifiers))
        updateSelectionAfterKeyboardNavigation(event)
        ensureVisible(masonryLayout.currentIndex)
        return true
    }

    property bool backspaceDisabledUntilKeyUp: false
    Keys.onPressed:
        (event) => {
            event.accepted = true
            if (selectionInteractionEnabled && event.key === Qt.Key_Shift && !event.isAutoRepeat) {
                beginShiftSelection()
            }
            else if (primaryView && event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier)) {
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                viewerController.back()
                masonryLayout.loadSavedState()
            }
            else if (primaryView && event.key === Qt.Key_Right && (event.modifiers & Qt.AltModifier)) {
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                viewerController.forward()
                masonryLayout.loadSavedState()
            }
            else if (primaryView && event.key === Qt.Key_Insert && !(event.modifiers & Qt.ControlModifier)) {
                selectionModel.toggleSelection(currentSourceIndex())
                setCurrentIndex(masonryLayout.currentIndex + 1,
                                false, false, false, true)
            }
            else if (primaryView && event.key === Qt.Key_Backslash && (event.modifiers & Qt.ControlModifier) && !quickSearchMode) {
                galleryViewModel.selectedOnly = !galleryViewModel.selectedOnly
            }
            else if (primaryView && event.key === Qt.Key_Tab && !quickSearchMode) {
                masonryView.alwaysShowFileNames = !masonryView.alwaysShowFileNames
            }
            else if (!primaryView && (event.key === Qt.Key_Space || event.key === Qt.Key_Backslash)) {
                masonryView.currentSelectionToggleRequested(masonryLayout.currentIndex)
            }
            else if (primaryView && event.key === Qt.Key_Backslash && !quickSearchMode) {
                toggleCurrentSelection()
            }
            else if (primaryView && event.key === Qt.Key_Asterisk) {
                selectionModel.invertSelection()
            }
            else if (primaryView && (event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) && (event.modifiers & Qt.ControlModifier) && !quickSearchMode) {
                selectionModel.setSameKindSelection(currentSourceIndex(), true)
            }
            else if (primaryView && event.key === Qt.Key_Minus && (event.modifiers & Qt.ControlModifier) && !quickSearchMode) {
                selectionModel.setSameKindSelection(currentSourceIndex(), false)
            }
            else if (primaryView && (event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) && (event.modifiers & Qt.ShiftModifier) && !quickSearchMode) {
                selectionModel.setAllSelection(true)
            }
            else if (primaryView && event.key === Qt.Key_Minus && (event.modifiers & Qt.ShiftModifier) && !quickSearchMode) {
                selectionModel.setAllSelection(false)
            }
            else if (masonryLayout.presentationMode !== MasonryLayout.Masonry
                     && !(event.modifiers
                          & (Qt.AltModifier | Qt.ControlModifier))
                     && (event.key === Qt.Key_Left
                         || event.key === Qt.Key_Right
                         || event.key === Qt.Key_Up
                         || event.key === Qt.Key_Down)) {
                navigateCollection(event, false)
            }
            else if (masonryLayout.presentationMode !== MasonryLayout.Masonry
                     && (event.key === Qt.Key_PageUp
                         || event.key === Qt.Key_PageDown)
                     && !(event.modifiers & Qt.ControlModifier)) {
                navigateCollection(event, true)
            }
            else if (event.key === Qt.Key_Left) {
                setCurrentIndex(
                            masonryLayout.currentIndex - 1,
                            false, false, false,
                            keyboardNavigationChangesSelection(event.modifiers))
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_Right) {
                setCurrentIndex(
                            masonryLayout.currentIndex + 1,
                            false, false, false,
                            keyboardNavigationChangesSelection(event.modifiers))
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_Up && !(event.modifiers & Qt.AltModifier)) {
                let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                let yAbove = currentItemGeometry.y - 2
                let indexAbove = masonryLayout.indexAt(currentItemCenterX, yAbove)
                if (indexAbove !== -1) {
                    if (event.modifiers & Qt.ControlModifier) {
                        ensureVisible(masonryLayout.currentIndex)
                        setCurrentIndex(
                                    indexAbove, true, false, true,
                                    keyboardNavigationChangesSelection(
                                        event.modifiers))
                        let newCurrentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                        scrollBy(newCurrentItemGeometry.y - currentItemGeometry.y)
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                    else {
                        setCurrentIndex(
                                    indexAbove, true, false, false,
                                    keyboardNavigationChangesSelection(
                                        event.modifiers))
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                }
            }
            else if (event.key === Qt.Key_Down && !(event.modifiers & Qt.AltModifier)) {
                let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                let yBelow = currentItemGeometry.y + currentItemGeometry.height + 2
                let indexBelow = masonryLayout.indexAt(currentItemCenterX, yBelow)

                if (indexBelow === -1) {
                    if (masonryLayout.listView) {
                        indexBelow = masonryLayout.indexAt(0, yBelow)
                    }
                    // else {
                    //     let newX = currentItemCenterX -
                    //     while (newX > 0) {
                    //         indexBelow = masonryLayout.indexAt(0, yBelow)
                    //     }
                    // }
                }

                if (indexBelow !== -1) {
                    if (event.modifiers & Qt.ControlModifier) {
                        ensureVisible(masonryLayout.currentIndex)
                        setCurrentIndex(
                                    indexBelow, true, false, true,
                                    keyboardNavigationChangesSelection(
                                        event.modifiers))
                        let newCurrentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                        scrollBy(newCurrentItemGeometry.y - currentItemGeometry.y)
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                    else {
                        setCurrentIndex(
                                    indexBelow, true, false, false,
                                    keyboardNavigationChangesSelection(
                                        event.modifiers))
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                }
            }
            else if (event.key === Qt.Key_Home) {
                setCurrentIndex(
                            0, false, false, false,
                            keyboardNavigationChangesSelection(event.modifiers))
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_End) {
                setCurrentIndex(
                            masonryLayout.count - 1,
                            false, false, false,
                            keyboardNavigationChangesSelection(event.modifiers))
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_PageUp && !(event.modifiers & Qt.ControlModifier)) {
                let deltaY = (masonryLayout.height - masonryLayout.height / 8)
                let futureContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
                let prevPageY = Math.max(0, futureContentY - deltaY) + currentItemCenterY
                let newCurrentIndex = masonryLayout.indexAt(currentItemCenterX, prevPageY)
                if (newCurrentIndex === -1) {
                    newCurrentIndex = 0
                }
                let hitStart = newCurrentIndex === 0
                if (newCurrentIndex === masonryLayout.currentIndex) {
                    newCurrentIndex = masonryLayout.indexAt(currentItemCenterX, 1)
                    hitStart = true

                    if (newCurrentIndex === masonryLayout.currentIndex) {
                        newCurrentIndex = 0
                    }
                }

                setCurrentIndex(
                            newCurrentIndex, !hitStart, !hitStart, false,
                            keyboardNavigationChangesSelection(event.modifiers))
                updateSelectionAfterKeyboardNavigation(event)
                scrollBy(-deltaY)
            }
            else if (event.key === Qt.Key_PageDown && !(event.modifiers & Qt.ControlModifier)) {
                let deltaY = (masonryLayout.height - masonryLayout.height / 8)
                let futureContentY = (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
                let nextPageY = Math.min(masonryLayout.contentHeight - masonryLayout.height, futureContentY + deltaY) + currentItemCenterY
                let newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, nextPageY)
                console.log("Key_PageDown", masonryLayout.currentIndex, "->", newCurrentIndex2, "of", masonryLayout.count)

                if (newCurrentIndex2 === -1) {
                    newCurrentIndex2 = masonryLayout.count - 1
                }
                let hitEnd = newCurrentIndex2 >= masonryLayout.count - 1
                if (newCurrentIndex2 === masonryLayout.currentIndex && nextPageY >= masonryLayout.contentHeight - masonryLayout.height * 1.5) {
                    newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, masonryLayout.contentHeight - 1)
                    hitEnd = true

                    if (newCurrentIndex2 === -1) {
                        newCurrentIndex2 = masonryLayout.indexAt(currentItemCenterX, masonryLayout.contentHeight - masonryLayout.targetHeight * 0.5)
                    }
                    if (newCurrentIndex2 === masonryLayout.currentIndex || newCurrentIndex2 === -1) {
                        newCurrentIndex2 = masonryLayout.count - 1
                    }
                }

                setCurrentIndex(
                            newCurrentIndex2, !hitEnd, !hitEnd, false,
                            keyboardNavigationChangesSelection(event.modifiers))
                updateSelectionAfterKeyboardNavigation(event)
                scrollBy(deltaY)
            }
            else if (primaryView && ((event.key === Qt.Key_Backspace && !quickSearchMode && !backspaceDisabledUntilKeyUp) ||
                     event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier) ||
                     event.key === Qt.Key_PageUp && (event.modifiers & Qt.ControlModifier))) {
                masonryView.disableAnimation = true
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                setCurrentIndex(viewerController.up())
                masonryView.disableAnimation = false
                masonryLayout.loadSavedState()
            }
            else if (primaryView && (event.key === Qt.Key_F11 || event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier) ||
                     (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier))) {
                topLevelWindow.toggleFullscreen()
            }
            else if (!primaryView && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                masonryView.currentIndexActivated(masonryLayout.currentIndex)
            }
            else if (primaryView && ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ControlModifier) ||
                     event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier) ||
                     event.key === Qt.Key_PageDown && (event.modifiers & Qt.ControlModifier))) {
                hideQuickSearch()
                let currentItem = masonryLayout.currentItem
                if (!currentItem || !currentItem.model) {
                    return
                }
                if (currentItem.model.isFolder) {
                    viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                    viewerController.cd(currentItem.model.text)
                }
                else if (currentItem.model.isImage) {
                    masonryView.toggleViewer()
                }
            }
            else if ((event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) && !quickSearchMode) {
                masonryLayout.zoomIn()
            }
            else if (event.key === Qt.Key_Minus && !quickSearchMode) {
                masonryLayout.zoomOut()
            }
            else if (event.key === Qt.Key_Escape) {
                endScrolling()

                if (quickSearchMode) {
                    event.accepted = false
                }
            }
            else if (primaryView && (event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_C || event.key === Qt.Key_Insert)) {
                viewerController.clipboardCopyIndexName(masonryLayout.currentIndex)
            }
            else if (primaryView && (event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_D) {
                viewerController.clipboardCopyIndexFullPath(masonryLayout.currentIndex)
            }
            else if (primaryView && (event.key === Qt.Key_F5 || (event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_R)) {
                masonryLayout.preserveCurrentItemPositionForNextModelReset()
                viewerController.cd(viewerController.currentPath, false)
            }
            else {
                event.accepted = false
            }
        }

    Keys.onReleased:
        (event) => {
            if (!event.isAutoRepeat) {
                backspaceDisabledUntilKeyUp = false
                if (selectionInteractionEnabled && event.key === Qt.Key_Shift) {
                    finishShiftSelection()
                }
            }
        }

    Rectangle {
        id: collectionDetailsHeader
        visible: masonryLayout.presentationMode === MasonryLayout.Details
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(26, Math.min(34, masonryLayout.density))
        color: Style.darker
        z: 5

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Math.max(28, masonryLayout.density) + 12
            anchors.right: detailsSizeHeader.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            text: qsTr("Name")
            color: Style.text
            opacity: 0.72
            verticalAlignment: Text.AlignVCenter

            MouseArea {
                anchors.fill: parent
                onClicked: masonryView.sortDetailsBy("name")
            }
        }

        Text {
            id: detailsSizeHeader
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 112
            text: qsTr("Size")
            color: Style.text
            opacity: 0.72
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter

            MouseArea {
                anchors.fill: parent
                onClicked: masonryView.sortDetailsBy("size")
            }
        }
    }

    MasonryLayout {
        id: masonryLayout
        anchors {
//            topMargin: 1
            leftMargin: masonryLayout.spacing / 2

            left: parent.left
            top: collectionDetailsHeader.visible
                 ? collectionDetailsHeader.bottom : parent.top
            bottom: parent.bottom
            right: masonryScroll.left
            rightMargin: masonryLayout.spacing / 2
        }
        model: masonryView.masonryModel

        paddingLeft: 1+5
        paddingRight: 1+5
        paddingTop: 7+5
        paddingBottom: 7+5

        delegate: masonryView.masonryDelegate

        function loadSavedState() {
            masonryView.disableAnimation = true
            let savedContentY = viewerController.savedContentY()
            if (savedContentY !== -1) {
                masonryLayout.contentY = savedContentY
                // console.log("RESTORING SAVED CONTENTY", savedContentY)
            }
            let savedCurrentIndex = viewerController.savedCurrentIndex()
            if (savedCurrentIndex !== -1) {
                setCurrentIndex(savedCurrentIndex, false, false, true)
                // console.log("RESTORING CURRENT INDEX", savedContentY)
            }
            masonryView.disableAnimation = false
        }

        Shortcut {
            sequence: "F9"
            enabled: masonryView.primaryView
            onActivated: {
                masonryLayout.showTransparentGrid = !masonryLayout.showTransparentGrid
            }
        }

        MouseArea {
            property alias animation: scrollAnimation2

            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel:
                (wheel) => {
                    let delta = (Qt.platform.os === "osx" ? wheel.pixelDelta.y : wheel.angleDelta.y)
                    if (wheel.modifiers & Qt.ControlModifier) {
                        if (delta < 0) {
                            masonryLayout.zoomOut()
                        }
                        else {
                            masonryLayout.zoomIn()
                        }
                    }
                    else {
                        scrollBy(-delta, Qt.platform.os === "osx" ? true : false)
                    }
                }
        }

        PinchArea {
            anchors.fill: parent
            property int startDensity: 0

            onPinchStarted: {
                startDensity = masonryLayout.density
            }

            onPinchUpdated: (pinch) => {
                masonryLayout.density = Math.min(
                            masonryView.maximumDensity(),
                            Math.max(masonryView.minimumDensity(),
                                     startDensity * pinch.scale))
            }

            onPinchFinished: {
                if (startDensity != masonryLayout.density) {
                    masonryLayout.reReadAndDecodeThumbnails()
                }
            }
        }

        NumberAnimation {
            id: scrollAnimation2

            target: masonryLayout
            property: "contentY"
            duration: 150
            easing.type: Easing.OutSine
        }
    }

    Rectangle {
        x: Math.min(rubberBandStartX, rubberBandCurrentX)
        y: Math.min(rubberBandStartY, rubberBandCurrentY)
        width: Math.abs(rubberBandCurrentX - rubberBandStartX)
        height: Math.abs(rubberBandCurrentY - rubberBandStartY)
        visible: rubberBandActive && (width > 2 || height > 2)
        color: Qt.rgba(selectionAccentColor.r, selectionAccentColor.g,
                       selectionAccentColor.b, 0.12)
        border.width: 1
        border.color: selectionAccentColor
        radius: 2
        z: 20
    }

    GalleryScrollBar {
        id: masonryScroll
        theme: ({
            "scrollBarHandle": Style.darker,
            "scrollBarHandleBackgroundHovered": Style.lighter2,
            "scrollBarHandleHovered": Style.lighter,
            "scrollBarHandlePressed": Style.brickPressed
        })
        anchors {
            top: masonryLayout.top
            bottom: masonryLayout.bottom
            right: parent.right
        }
        visible: masonryLayout.needScroll
        width: masonryLayout.needScroll ? 16 : 0

        Connections {
            target: masonryLayout

            function onNeedScrollChanged() {
                if (!topLevelWindow.isResizing) {
                    // console.log("_need scroll changed")

                    //TODO: CHECK THIS
                    //masonryLayout.reReadAndDecodeThumbnails()
                }
            }
        }

        onPositionChanged: {
            if (pressed) {
                masonryLayout.contentY = masonryScroll.position * masonryLayout.contentHeight
            }
        }

        Connections {
            id: conn
            target: masonryLayout
            function onContentYChanged() {
                masonryScroll.position = masonryLayout.contentY / masonryLayout.contentHeight
            }

            function onContentHeightChanged() {
                masonryScroll.size = masonryLayout.height / masonryLayout.contentHeight
            }

            function onHeightChanged() {
                masonryScroll.size = masonryLayout.height / masonryLayout.contentHeight
            }
        }
    }

    Rectangle {
        id: quickSearchArea
        anchors {
            left: parent.left
            bottom: parent.bottom
            leftMargin: 20
            bottomMargin: 20
        }
        width: 330
        height: 49
        color: Style.popupBackground
        border.width: 1
        border.color: Style.popupBorder
        radius: 4
        visible: quickSearchEnabled && quickSearchMode

        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            TextField {
                id: quickSearchField
                Layout.fillWidth: true
                Layout.fillHeight: true

                leftPadding: 17
                rightPadding: 17
                focus: true
                hasBackground: false
                color: masonryLayout.quickSearch.matches ? Style.text : Style.textError

                validator: masonryLayout.quickSearch.validator

                Keys.forwardTo: masonryView

                Keys.onPressed:
                    (event) => {
                        delayedOnPressed.start()

                        if (event.key === Qt.Key_Escape) {
                            quickSearchField.text = ""
                            event.accepted = false
                        }
                        else if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return) && (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier) ||
                                 event.key === Qt.Key_F3 && (event.modifiers & Qt.ShiftModifier)) {
                            searchNext(false)
                        }
                        else if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return) && (event.modifiers & Qt.ControlModifier) ||
                                 event.key === Qt.Key_F3) {
                            searchNext(true)
                        }
                    }
            }

            Text {
                id: matchesFoundText
                Layout.fillHeight: true
                Layout.preferredWidth: 34

                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: Style.text
                opacity: 0.4
                text: masonryLayout.quickSearch.matchesInfo
            }

            Rectangle {
                color: Style.lighter2
                Layout.preferredWidth: 1
                Layout.preferredHeight: 32
                Layout.leftMargin: 16
                Layout.rightMargin: 8
                Layout.alignment: Qt.AlignVCenter
            }

            component SearchButton : Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 32

                icon.width: 9
                icon.height: 9
            }

            SearchButton {
                icon.source: "qrc:/ZoinGallery/resources/SearchBack.svg"
                onClicked: searchNext(false)

                ToolTip.text: "Previous\tShift+F3"
            }

            SearchButton {
                icon.source: "qrc:/ZoinGallery/resources/SearchNext.svg"
                onClicked: searchNext(true)

                ToolTip.text: "Next\tF3"
            }

            SearchButton {
                Layout.rightMargin: 8
                icon.source: "qrc:/ZoinGallery/resources/SearchClose.svg"
                onClicked: hideQuickSearch()

                ToolTip.text: "Close quick search\tEsc"
            }
        }

        Timer {
            id: delayedOnPressed
            interval: 0
                onTriggered: {
                if (!quickSearchEnabled) {
                    return
                }
                if (quickSearchField.text !== "" && !quickSearchMode) {
                    quickSearchMode = true
                }
                else if (quickSearchField.text === "" && quickSearchMode) {
                    hideQuickSearch()
                }

                masonryLayout.quickSearch.mask = quickSearchField.text
                if (quickSearchField.text) {
                    let nextIndex = masonryLayout.quickSearch.nextImage(true, false)
                    setCurrentIndex(nextIndex, /*<defaults>*/ false, false, false /*</defaults>*/, true)
                }
            }
        }
    }
}
