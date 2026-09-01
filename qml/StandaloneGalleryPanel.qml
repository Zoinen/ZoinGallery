pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

FocusScope {
    id: root

    // The primary standalone catalog is the shell's initial keyboard
    // surface. Secondary/filtered panels acquire focus explicitly on press.
    focus: primaryView

    property GallerySession session: null
    property GalleryPanelController suppliedController: null
    required property var viewerController
    required property var selectionModel
    property var selectionMapper: null
    property bool primaryView: true
    property bool selectionInteractionEnabled: true
    property bool quickSearchEnabled: primaryView
    property bool disableAnimation: false
    property bool alwaysShowFileNames: false
    property color selectionAccentColor: Style.persistentSelectionBorder

    property alias view: panel.galleryLayout
    property alias controller: panel.controller
    property alias targetHeight: panel.thumbnailHeight
    property alias density: panel.density
    property alias columnCount: panel.columnCount
    property alias masonrySpacing: panel.itemSpacing
    property alias listView: panel.listView
    property alias showTransparentGrid: panel.showTransparentGrid
    property bool emptyStateEnabled: true
    property alias viewerTransitionActive: panel.viewerTransitionActive
    property alias scrollingStarted: panel.scrollingStarted
    property alias scrollingStartedAtY: panel.scrollingStartedAtY
    property alias scrollingMode: panel.scrollingMode
    readonly property Item focusProxy: panel
    readonly property bool scrolled:
        panel.galleryLayout.needScroll && panel.galleryLayout.contentY > 0
    readonly property bool quickSearchMode:
        panel.controllerReady && panel.controller.quickSearchActive

    signal toggleViewer()
    signal currentIndexActivated(int index)
    signal currentSelectionToggleRequested(int index)
    signal fileDropFailed(string title, string message)
    signal fullscreenRequested()

    function presentationName(mode) {
        if (mode === GalleryViewportItem.Columns)
            return "columns"
        if (mode === GalleryViewportItem.Details)
            return "details"
        if (mode === GalleryViewportItem.Grid)
            return "grid"
        if (mode === GalleryViewportItem.Icons)
            return "icons"
        return "masonry"
    }

    function choosePresentation(mode, columns) {
        panel.columnCount = columns || 2
        panel.applyPresentationMode(presentationName(mode))
        focusView()
    }

    function focusView() {
        panel.forceActiveFocus()
    }

    function setCurrentIndex(index, keepLastPosX, keepLastPosY,
                             neverScroll, keepQuickSearch) {
        if (!panel.controllerReady || panel.galleryLayout.count <= 0)
            return
        const bounded = Math.max(
            0, Math.min(panel.galleryLayout.count - 1, Number(index)))
        panel.controller.requestCursor(bounded, false)
        if (!neverScroll)
            panel.ensureCurrentVisible(!disableAnimation)
        if (!keepLastPosX)
            panel.resetCurrentItemCenterX(bounded)
        if (!keepLastPosY)
            panel.resetCurrentItemCenterY(bounded)
        if (!keepQuickSearch)
            panel.controller.clearQuickSearch()
    }

    function moveInImageList(forward, toEnd) {
        const nextIndex = panel.galleryLayout.nextImageIndex(forward, toEnd)
        setCurrentIndex(nextIndex)
        return nextIndex
    }

    function loadSavedState() {
        disableAnimation = true
        const savedContentY = viewerController.savedContentY()
        if (savedContentY !== -1)
            panel.setPanelContentY(savedContentY, true)
        const savedCurrentIndex = viewerController.savedCurrentIndex()
        if (savedCurrentIndex !== -1)
            setCurrentIndex(savedCurrentIndex, false, false, true, true)
        disableAnimation = false
    }

    function currentItemImageGeometry() {
        return panel.currentItemImageGeometry(root)
    }

    function currentItemImageSource() {
        return panel.currentItemImageSource()
    }

    function singleItemDragRequested(modifiers) {
        const modifier = Qt.platform.os === "osx"
                ? Qt.MetaModifier : Qt.AltModifier
        return Boolean(modifiers & modifier)
    }

    function selectionIndex(entryId) {
        const viewIndex = panel.controller.indexForEntryId(entryId)
        if (viewIndex < 0)
            return -1
        return selectionMapper
                ? selectionMapper.mapToSourceRow(viewIndex) : viewIndex
    }

    function applySelection(mode, entryIds) {
        if (!selectionInteractionEnabled || !selectionModel)
            return
        if (mode === "replace")
            selectionModel.setAllSelection(false)
        for (let item = 0; item < entryIds.length; ++item) {
            const index = selectionIndex(entryIds[item])
            if (index < 0)
                continue
            if (mode === "toggle")
                selectionModel.toggleSelection(index)
            else
                selectionModel.setSelection(index, true)
        }
    }

    StandaloneGalleryTheme {
        id: palette
        selection: root.selectionAccentColor
    }

    GalleryPanelController {
        id: sessionController
        session: root.session
    }

    GalleryPanel {
        id: panel
        anchors.fill: parent
        controller: root.suppliedController
                    ? root.suppliedController : sessionController
        theme: palette
        autoFocus: false
        focus: true
        localQuickSearchEnabled: root.quickSearchEnabled
        quickSearchExcludedCharacters: "\\*"
        emptyStateText: qsTr("No images")
        emptyStateEnabled: root.emptyStateEnabled

        onActivateRequested: root.focusView()
        onSelectionRequested: (mode, entryIds) =>
            root.applySelection(mode, entryIds)
        onOpenRequested: (entryId, sourceIndex, isImage, autoRepeat) => {
            if (!root.primaryView) {
                root.currentIndexActivated(panel.controller.currentIndex)
            } else if (isImage) {
                root.toggleViewer()
            } else {
                root.viewerController.saveCurrentState(
                            panel.galleryLayout.contentY,
                            panel.controller.currentIndex)
                if (root.session)
                    root.session.requestOpen(panel.controller.currentIndex)
            }
        }
    }

    Connections {
        target: panel.controller

        function onFileOperationFailed(title, message) {
            root.fileDropFailed(title, message)
        }
    }

    Keys.onPressed: event => {
        let handled = true
        const modifiers = event.modifiers
        const control = Boolean(modifiers
                                & (Qt.ControlModifier | Qt.MetaModifier))
        const alt = Boolean(modifiers & Qt.AltModifier)
        const shift = Boolean(modifiers & Qt.ShiftModifier)
        if (root.primaryView && alt && event.key === Qt.Key_Left) {
            root.viewerController.saveCurrentState(
                        root.view.contentY, root.controller.currentIndex)
            root.viewerController.back()
            root.loadSavedState()
        } else if (root.primaryView && alt && event.key === Qt.Key_Right) {
            root.viewerController.saveCurrentState(
                        root.view.contentY, root.controller.currentIndex)
            root.viewerController.forward()
            root.loadSavedState()
        } else if (root.primaryView && control
                   && event.key === Qt.Key_Backslash) {
            if (root.selectionMapper)
                root.selectionMapper.selectedOnly =
                        !root.selectionMapper.selectedOnly
        } else if (root.primaryView && event.key === Qt.Key_Tab) {
            root.alwaysShowFileNames = !root.alwaysShowFileNames
        } else if (!root.primaryView
                   && (event.key === Qt.Key_Space
                       || event.key === Qt.Key_Backslash)) {
            root.currentSelectionToggleRequested(root.controller.currentIndex)
        } else if (root.primaryView && event.key === Qt.Key_Backslash) {
            const index = root.selectionIndex(root.controller.cursorEntryId)
            if (index >= 0)
                root.selectionModel.toggleSelection(index)
        } else if (root.primaryView && event.key === Qt.Key_Asterisk) {
            root.selectionModel.invertSelection()
        } else if (root.primaryView && control
                   && (event.key === Qt.Key_Equal
                       || event.key === Qt.Key_Plus)) {
            const index = root.selectionIndex(root.controller.cursorEntryId)
            if (index >= 0)
                root.selectionModel.setSameKindSelection(index, true)
        } else if (root.primaryView && control
                   && event.key === Qt.Key_Minus) {
            const index = root.selectionIndex(root.controller.cursorEntryId)
            if (index >= 0)
                root.selectionModel.setSameKindSelection(index, false)
        } else if (root.primaryView && shift
                   && (event.key === Qt.Key_Equal
                       || event.key === Qt.Key_Plus)) {
            root.selectionModel.setAllSelection(true)
        } else if (root.primaryView && shift
                   && event.key === Qt.Key_Minus) {
            root.selectionModel.setAllSelection(false)
        } else if (root.primaryView
                   && (event.key === Qt.Key_Backspace
                       || (alt && event.key === Qt.Key_Up)
                       || (control && event.key === Qt.Key_PageUp))) {
            root.disableAnimation = true
            root.viewerController.saveCurrentState(
                        root.view.contentY, root.controller.currentIndex)
            root.setCurrentIndex(root.viewerController.up(),
                                 false, false, false, true)
            root.loadSavedState()
            root.disableAnimation = false
        } else if (root.primaryView
                   && (event.key === Qt.Key_F11
                       || (control && event.key === Qt.Key_F)
                       || (alt && (event.key === Qt.Key_Return
                                   || event.key === Qt.Key_Enter)))) {
            root.fullscreenRequested()
        } else if (root.primaryView && control
                   && (event.key === Qt.Key_C
                       || event.key === Qt.Key_Insert)) {
            root.viewerController.clipboardCopyIndexName(
                        root.controller.currentIndex)
        } else if (root.primaryView && control
                   && event.key === Qt.Key_D) {
            root.viewerController.clipboardCopyIndexFullPath(
                        root.controller.currentIndex)
        } else if (root.primaryView
                   && (event.key === Qt.Key_F5
                       || (control && event.key === Qt.Key_R))) {
            root.view.preserveCurrentItemPositionForNextModelReset()
            root.viewerController.cd(root.viewerController.currentPath, false)
        } else if (root.primaryView && event.key === Qt.Key_F8) {
            root.listView = !root.listView
        } else if (root.primaryView && event.key === Qt.Key_F9) {
            root.showTransparentGrid = !root.showTransparentGrid
        } else {
            handled = false
        }
        event.accepted = handled
    }

    Component.onCompleted: {
        if (primaryView)
            focusView()
    }
}
