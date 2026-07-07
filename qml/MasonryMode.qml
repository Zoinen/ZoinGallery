import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

import ZoinGallery 1.0

MouseArea {
    id: masonryView

    property alias view: masonryLayout
    property alias focusProxy: quickSearchField

    signal toggleViewer()

    // <Scrolling>
    property bool scrollingStarted: false
    property var scrollingStartedAtY
    property bool scrollingMode: false
    property bool quickSearchMode: false

    property bool disableAnimation: false
    property bool scrolled: masonryLayout.needScroll && masonryScroll.position > 0

    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
    clip: true

    function startScrolling() {
        scrollingStarted = false
        scrollingMode = true
        scrollingStartedAtY = masonryView.mouseY
        scrollingAnimation.start()
        masonryLayout.setScrollingMode(true)
    }

    function endScrolling() {
        scrollingStarted = false
        scrollingMode = false
        // This immediately stops behavior animation
        masonryLayout.contentY += 0.01
        masonryLayout.contentY -= 0.01

        scrollingAnimation.stop()
        masonryLayout.setScrollingMode(false)

        hideHovered = false
    }

    function resetCurrentItemCenter() {
        let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
        currentItemCenterX = currentItemGeometry.x + currentItemGeometry.width / 2
        currentItemCenterY = currentItemGeometry.y + currentItemGeometry.height / 2 - (scrollAnimation2.running ? scrollAnimation2.to : masonryLayout.contentY)
    }

    onPressed: (mouse) => {
        if (mouse.button === Qt.LeftButton) {
            focusProxy.forceActiveFocus()
            let p = masonryLayout.mapFromItem(masonryView, mouse.x, mouse.y)
            if (p.x >= 0 && p.y >= 0 && p.x <= masonryLayout.width && p.y <= masonryLayout.height &&
                    masonryLayout.indexAtViewport(p.x, p.y) === -1) {
                startRubberBand(mouse.x, mouse.y, mouse.modifiers)
            }
        }
        else {
            if (mouse.button === Qt.MiddleButton && !scrollingMode) {
                startScrolling()
            }
            else {
                endScrolling()
            }
        }
    }

    onReleased: (mouse) => {
        if (mouse.button === Qt.LeftButton) {
            finishRubberBand()
        }
        else {
            if (scrollingStarted) {
                endScrolling()
            }
        }
    }

    FrameAnimation {
        id: scrollingAnimation
        onTriggered: {
            let distance = masonryView.mouseY - scrollingStartedAtY
            distance = Math.min(Math.max(0, distance - 25), distance + 25)
            let totalHeight = topLevelWindow.availableScreenHeight()
            let fraction = Math.abs(distance) / (totalHeight - 25)
            if (fraction > 0) {
                fraction += 0.05
            }

            let speed = frameTime * 30
            let increment = (Math.pow(fraction, 3) * totalHeight) * speed * 2
            masonryLayout.contentY += increment * (distance < 0 ? -1 : 1)

            if (distance !== 0 && !scrollingStarted) {
                scrollingStarted = true
                hideHovered = true
            }

            if (distance > 0) {
                masonryLayout.setScrollingMode(true, 1)
            }
            else if (distance < 0) {
                masonryLayout.setScrollingMode(true, -1)
            }
            else {
                masonryLayout.setScrollingMode(true)
            }
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
            resetCurrentItemCenter()
        }
    }

    Connections {
        target: viewerController

        function onCurrentPathChanged() {
            resetCurrentItemCenter()
        }

        function onSetCurrentIndex(index) {
            masonryLayout.currentIndex = index
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
        let currentItemGeometry = Qt.rect(masonryLayout.currentItem.x,
                                          masonryLayout.currentItem.y,
                                          masonryLayout.currentItem.width,
                                          masonryLayout.currentItem.height)
        let spacing = masonryLayout.spacing
        let imageGeometry = Qt.rect(currentItemGeometry.x + spacing / 2 + masonryLayout.paddingLeft,
                                    currentItemGeometry.y + spacing / 2 - masonryLayout.contentY,
                                    currentItemGeometry.width - spacing,
                                    currentItemGeometry.height - spacing)
        return imageGeometry
    }

    function setCurrentIndex(index, keepLastPosX = false, keepLastPosY = false, neverScroll = false, keepQuickSearch = false) {
        masonryLayout.currentIndex = index
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
        return galleryViewModel.mapToSourceRow(viewIndex)
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
        shiftNavigationSelectionValue = true
        shiftSelectionDescription = "Range selection"
        fileListModel.beginSelectionPreview()
    }

    function updateShiftNavigationSelection(targetIndex) {
        beginShiftSelection()
        shiftSelectionDescription = shiftNavigationSelectionValue ? "Range selection" : "Range deselection"
        fileListModel.previewSelectionIndexes(
                    galleryViewModel.sourceRowsForViewRange(shiftSelectionAnchorIndex, targetIndex, false),
                    shiftNavigationSelectionValue ? rubberBandModeAdd : rubberBandModeDeselect)
    }

    function shiftClickSelection(targetIndex) {
        beginShiftSelection()
        shiftSelectionDescription = "Range selection"
        fileListModel.previewSelectionIndexes(galleryViewModel.sourceRowsForViewRange(shiftSelectionAnchorIndex, targetIndex, true),
                                              rubberBandModeAdd)
        setCurrentIndex(targetIndex)
    }

    function toggleCurrentSelection() {
        setCurrentIndex(masonryLayout.currentIndex)
        fileListModel.toggleSelection(currentSourceIndex())
    }

    function finishShiftSelection() {
        if (!shiftSelectionActive) {
            return
        }
        fileListModel.commitSelectionPreview(shiftSelectionDescription)
        shiftSelectionActive = false
        shiftSelectionAnchorIndex = -1
    }

    function updateSelectionAfterKeyboardNavigation(event) {
        if (event.modifiers & Qt.ShiftModifier) {
            updateShiftNavigationSelection(masonryLayout.currentIndex)
        }
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
        fileListModel.beginSelectionPreview()
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
        fileListModel.previewSelectionIndexes(galleryViewModel.mapToSourceRows(indexes), rubberBandMode)
    }

    function finishRubberBand() {
        if (!rubberBandPending && !rubberBandActive) {
            return
        }

        if (rubberBandActive) {
            fileListModel.commitSelectionPreview(rubberBandDescription())
        }
        else {
            if (shiftSelectionActive) {
                fileListModel.cancelSelectionPreview()
            }
            fileListModel.setAllSelection(false)
        }

        if (shiftSelectionActive) {
            shiftSelectionActive = false
            shiftSelectionAnchorIndex = -1
        }
        rubberBandPending = false
        rubberBandActive = false
    }

    property bool backspaceDisabledUntilKeyUp: false
    Keys.onPressed:
        (event) => {
            event.accepted = true
            if (event.key === Qt.Key_Shift && !event.isAutoRepeat) {
                beginShiftSelection()
            }
            else if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier)) {
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                viewerController.back()
                masonryLayout.loadSavedState()
            }
            else if (event.key === Qt.Key_Right && (event.modifiers & Qt.AltModifier)) {
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                viewerController.forward()
                masonryLayout.loadSavedState()
            }
            else if (event.key === Qt.Key_Insert && !(event.modifiers & Qt.ControlModifier)) {
                fileListModel.toggleSelection(currentSourceIndex())
                setCurrentIndex(masonryLayout.currentIndex + 1)
            }
            else if (event.key === Qt.Key_Backslash && (event.modifiers & Qt.ControlModifier) && !quickSearchMode) {
                galleryViewModel.selectedOnly = !galleryViewModel.selectedOnly
            }
            else if (event.key === Qt.Key_Tab && !quickSearchMode) {
                masonryView.alwaysShowFileNames = !masonryView.alwaysShowFileNames
            }
            else if (event.key === Qt.Key_Backslash && !quickSearchMode) {
                toggleCurrentSelection()
            }
            else if (event.key === Qt.Key_Asterisk) {
                fileListModel.invertSelection()
            }
            else if ((event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) && (event.modifiers & Qt.ControlModifier) && !quickSearchMode) {
                fileListModel.setSameKindSelection(currentSourceIndex(), true)
            }
            else if (event.key === Qt.Key_Minus && (event.modifiers & Qt.ControlModifier) && !quickSearchMode) {
                fileListModel.setSameKindSelection(currentSourceIndex(), false)
            }
            else if ((event.key === Qt.Key_Equal || event.key === Qt.Key_Plus) && (event.modifiers & Qt.ShiftModifier) && !quickSearchMode) {
                fileListModel.setAllSelection(true)
            }
            else if (event.key === Qt.Key_Minus && (event.modifiers & Qt.ShiftModifier) && !quickSearchMode) {
                fileListModel.setAllSelection(false)
            }
            else if (event.key === Qt.Key_Left) {
                setCurrentIndex(masonryLayout.currentIndex - 1)
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_Right) {
                setCurrentIndex(masonryLayout.currentIndex + 1)
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_Up && !(event.modifiers & Qt.AltModifier)) {
                let currentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                let yAbove = currentItemGeometry.y - 2
                let indexAbove = masonryLayout.indexAt(currentItemCenterX, yAbove)
                if (indexAbove !== -1) {
                    if (event.modifiers & Qt.ControlModifier) {
                        ensureVisible(masonryLayout.currentIndex)
                        setCurrentIndex(indexAbove, true, false, true)
                        let newCurrentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                        scrollBy(newCurrentItemGeometry.y - currentItemGeometry.y)
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                    else {
                        setCurrentIndex(indexAbove, true)
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
                        setCurrentIndex(indexBelow, true, false, true)
                        let newCurrentItemGeometry = masonryLayout.indexGeometry(masonryLayout.currentIndex)
                        scrollBy(newCurrentItemGeometry.y - currentItemGeometry.y)
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                    else {
                        setCurrentIndex(indexBelow, true)
                        updateSelectionAfterKeyboardNavigation(event)
                    }
                }
            }
            else if (event.key === Qt.Key_Home) {
                setCurrentIndex(0)
                updateSelectionAfterKeyboardNavigation(event)
            }
            else if (event.key === Qt.Key_End) {
                setCurrentIndex(masonryLayout.count - 1)
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

                setCurrentIndex(newCurrentIndex, !hitStart, !hitStart)
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

                setCurrentIndex(newCurrentIndex2, !hitEnd, !hitEnd)
                updateSelectionAfterKeyboardNavigation(event)
                scrollBy(deltaY)
            }
            else if ((event.key === Qt.Key_Backspace && !quickSearchMode && !backspaceDisabledUntilKeyUp) ||
                     event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier) ||
                     event.key === Qt.Key_PageUp && (event.modifiers & Qt.ControlModifier)) {
                masonryView.disableAnimation = true
                viewerController.saveCurrentState(masonryLayout.contentY, masonryLayout.currentIndex)
                setCurrentIndex(viewerController.up())
                masonryView.disableAnimation = false
                masonryLayout.loadSavedState()
            }
            else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier) ||
                     (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier)) {
                topLevelWindow.toggleFullscreen()
            }
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ControlModifier) ||
                     event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier) ||
                     event.key === Qt.Key_PageDown && (event.modifiers & Qt.ControlModifier)) {
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
            else if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_C || event.key === Qt.Key_Insert)) {
                viewerController.clipboardCopyIndexName(masonryLayout.currentIndex)
            }
            else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_D) {
                viewerController.clipboardCopyIndexFullPath(masonryLayout.currentIndex)
            }
            else if (event.key === Qt.Key_F5 || (event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_R) {
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
                if (event.key === Qt.Key_Shift) {
                    finishShiftSelection()
                }
            }
        }

    MasonryLayout {
        id: masonryLayout
        anchors {
//            topMargin: 1
            leftMargin: masonryLayout.spacing / 2

            left: parent.left
            top: parent.top
            bottom: parent.bottom
            right: masonryScroll.left
            rightMargin: masonryLayout.spacing / 2
        }
        model: galleryViewModel

        paddingLeft: 1+5
        paddingRight: 1+5
        paddingTop: 7+5
        paddingBottom: 7+5

        delegate: BrickDelegate {}

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
            property int startTargetHeight: 0

            onPinchStarted: {
                startTargetHeight = masonryLayout.targetHeight
            }

            onPinchUpdated: (pinch) => {
                masonryLayout.targetHeight = Math.min(500, Math.max(30, startTargetHeight * pinch.scale))
            }

            onPinchFinished: {
                if (startTargetHeight != masonryLayout.targetHeight) {
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
        color: Qt.rgba(1, 0.83, 0.23, 0.12)
        border.width: 1
        border.color: Style.persistentSelectionBorder
        radius: 2
        z: 20
    }

    ScrollBar {
        id: masonryScroll
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
        visible: quickSearchMode

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
                icon.source: "qrc:/resources/SearchBack.svg"
                onClicked: searchNext(false)

                ToolTip.text: "Previous\tShift+F3"
            }

            SearchButton {
                icon.source: "qrc:/resources/SearchNext.svg"
                onClicked: searchNext(true)

                ToolTip.text: "Next\tF3"
            }

            SearchButton {
                Layout.rightMargin: 8
                icon.source: "qrc:/resources/SearchClose.svg"
                onClicked: hideQuickSearch()

                ToolTip.text: "Close quick search\tEsc"
            }
        }

        Timer {
            id: delayedOnPressed
            interval: 0
            onTriggered: {
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
