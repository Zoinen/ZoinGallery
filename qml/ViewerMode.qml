import QtQuick

import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects
import ZoinGallery 1.0
import ZoinGallery.Native 1.0

pragma ComponentBehavior: Bound

Item {
    id: viewerMode

    visible: false

    property var sourceContext
    readonly property var sourceMasonry:
        sourceContext ? sourceContext.masonry : null
    readonly property var sourceMapper:
        sourceContext ? sourceContext.mapper : null
    readonly property var decodeModel:
        sourceContext ? sourceContext.decodeModel : null
    readonly property var selectionModel:
        sourceContext ? sourceContext.selectionModel : null
    readonly property var filmstripModel:
        sourceContext ? sourceContext.filmstripModel : null

    // sourceContext is a short-lived wrapper recreated whenever the viewer is
    // opened. The masonry instance identifies the actual persistent source.
    property var previousSourceMasonry: null

    onSourceContextChanged: {
        let nextSourceMasonry =
                sourceContext ? sourceContext.masonry : null
        let actualSourceChanged =
                previousSourceMasonry !== nextSourceMasonry

        flickableArea.resetViewerImages()
        if (actualSourceChanged) {
            clearPreviousImage()
        } else {
            restorePreviousImageLockIndexes()
        }
        resetViewerNavigation()
        lastKnownIndex = sourceMasonry ? sourceMasonry.view.currentIndex : -1
        lastKnownPath = sourceMasonry && lastKnownIndex >= 0
                ? pathForIndex(lastKnownIndex) : ""
        previousSourceMasonry = nextSourceMasonry
    }

    property int animationDuration: 150
    property int easingType: Easing.OutSine
    property real devicePixelRatio: 1.0
    required property Item shell
    required property QtObject hostWindow
    required property QtObject standaloneController
    required property Item titleBarItem
    required property Item viewerBackgroundItem
    required property Item minimizeButton
    required property Item maximizeButton
    required property Item closeButton
    required property bool quickWindowKitEnabled

    readonly property alias flickableArea: viewerSurface.viewport
    readonly property alias flickableAreaContainer: viewerSurface.viewportContainer
    readonly property alias delegateOutline: viewerSurface.outline
    readonly property alias imageInfoPanel: viewerSurface.imageInfo
    readonly property alias sphericViewerLoader: viewerSurface.sphereLoader
    readonly property alias sphericViewerComponent: viewerSurface.sphereComponent
    readonly property alias viewerNavigationNeighborImage: viewerSurface.neighborImage
    readonly property alias viewerNavigationFinishTimer: viewerSurface.navigationFinishTimer
    readonly property alias viewerWheelPanFinishTimer: viewerSurface.wheelPanFinishTimer
    readonly property alias viewerNavigationGestureEndTimer: viewerSurface.gestureEndTimer
    readonly property alias viewerNavigationResidualQuietTimer: viewerSurface.residualQuietTimer
    readonly property alias viewerNavigationOffsetAnimation: viewerSurface.navigationOffsetAnimation
    readonly property alias topPanel: viewerChrome.topChrome
    readonly property alias rightPanel: viewerChrome.rightChrome
    readonly property alias leftPanel: viewerChrome.leftChrome

    property real sphericViewerOpacity: 1
    property bool sphericViewerMode: false
    onSphericViewerModeChanged: {
        if (sphericViewerMode) {
            decodeModel.cancelAllDecodeViewerRunners()
            requestCurrentViewer()
            sphericViewerLoader.sourceComponent = sphericViewerComponent
        }
        else {
            sphericViewerLoader.sourceComponent = undefined
        }
    }

    property bool panelsVisible: false
    property alias zoomFitView: viewerSurface.zoomFitView
    readonly property real viewerChromeOpacity: shell.state === "viewer" ?
            (shell.viewerPinchCloseActive ? 1 - shell.viewerPinchCloseProgress : 1) : 0

    property alias animation: viewerSurface.animation
    property alias imageContainer: viewerSurface.viewport

    signal pinchZoomOutToThumbnailsProgressed(real progress)
    signal pinchZoomOutToThumbnailsFinished(bool commit)

    function sourceIndexForViewIndex(viewIndex) {
        return sourceMapper.mapToSourceRow(viewIndex)
    }

    function currentSourceIndex() {
        return sourceIndexForViewIndex(sourceMasonry.view.currentIndex)
    }

    function currentViewerPrefetchRows() {
        if (!sourceMapper ||
                typeof sourceMapper.viewerPrefetchSourceRows !== "function") {
            return []
        }
        return sourceMapper.viewerPrefetchSourceRows(
                    sourceMasonry.view.currentIndex, 16)
    }

    function requestCurrentViewer(width, height) {
        if (!decodeModel || !sourceMasonry || !sourceMasonry.view) {
            return
        }
        const sourceIndex = currentSourceIndex()
        const requestedWidth = width === undefined ? -1 : width
        const requestedHeight = height === undefined ? -1 : height
        if (typeof decodeModel.requestViewerInOrder === "function") {
            decodeModel.requestViewerInOrder(
                        sourceIndex, currentViewerPrefetchRows(),
                        requestedWidth, requestedHeight)
        }
        else {
            decodeModel.requestViewer(
                        sourceIndex, requestedWidth, requestedHeight)
        }
    }

    readonly property bool currentItemSelected: selectionModel.selectedCount >= 0 &&
            selectionModel.isIndexSelected(currentSourceIndex())
    readonly property bool viewerNavigationTargetSelected: selectionModel.selectedCount >= 0 &&
            viewerNavigationTargetIndex !== -1 && selectionModel.isIndexSelected(sourceIndexForViewIndex(viewerNavigationTargetIndex))
    readonly property color currentItemSelectionColor: {
        // Keep the binding subscribed when an already-selected item moves
        // between groups without changing the current index.
        selectionModel.selectedCount
        return selectionModel.selectionGroupColorForIndex(currentSourceIndex())
    }
    readonly property color viewerNavigationTargetSelectionColor: {
        selectionModel.selectedCount
        return viewerNavigationTargetIndex === -1 ? "transparent" :
               selectionModel.selectionGroupColorForIndex(
                   sourceIndexForViewIndex(viewerNavigationTargetIndex))
    }
    readonly property real viewerBackgroundOpacity: viewerBackgroundItem.opacity
    readonly property real currentSelectionHighlightPresence: !viewerNavigationActive || viewerNavigationTargetIndex === -1 ? 1 :
            (viewerNavigationDirection < 0 ? viewerNavigationCurrentOpacity : 1 - viewerNavigationProgress)
    readonly property real targetSelectionHighlightPresence: viewerNavigationActive && viewerNavigationTargetIndex !== -1 ?
            (viewerNavigationDirection < 0 ? viewerNavigationCoverProgress : viewerNavigationProgress) : 0
    readonly property real selectionHighlightNavigationOpacity:
            Math.min(1, (currentItemSelected ? currentSelectionHighlightPresence : 0) +
                        (viewerNavigationTargetSelected ? targetSelectionHighlightPresence : 0))
    property bool selectionHighlightAnimationSuppressed: false

    // ZZZ: Viewer size request takes current image's full size for all future images!!!

    function fitCurrentImageWhenReady() {
        if (!zoomFitView || sphericViewerMode) {
            delayedFitTimer.stop()
            return
        }

        if (flickableArea.originalSize.width > 1 && flickableArea.originalSize.height > 1) {
            delayedFitTimer.stop()
            if (!flickableArea.viewportAnimationRunning) {
                flickableArea.zoomToFit(true)
            }
        }
        else {
            delayedFitTimer.restartWait()
        }
    }

    function setImage(imageIdUrl, originalSize, fromIndex, level) {
        flickableArea.setImage(imageIdUrl, originalSize, fromIndex, level)
    }

    Timer {
        id: delayedFitTimer
        interval: 50
        repeat: true
        property int attempts: 0

        function restartWait() {
            attempts = 0
            restart()
        }

        onTriggered: {
            let size = sourceMasonry.view.indexOriginalSize(sourceMasonry.view.currentIndex)
            if (size.width > 1 && size.height > 1) {
                stop()
                let level = flickableArea.image.fromLevel >= 0 ? flickableArea.image.fromLevel : 0
                viewerMode.setImage(flickableArea.image.source, size, sourceMasonry.view.currentIndex, level)
                fitCurrentImageWhenReady()
                decodeModel.cancelAllDecodeViewerRunners()
                requestCurrentViewer(viewerMode.width * dpr,
                                     viewerMode.height * dpr)
            }
            else if (++attempts >= 600) {
                stop()
            }
        }
    }

    function show(sphericViewer) {
        sphericViewerMode = sphericViewer === "True"
        onCurrentIndexChanged()
        visible = true
        fitCurrentImageWhenReady()
    }

    function completeInstantOpen() {
        delegateOutline.opacity = 0
        imageInfoPanel.opacity = 0
        sphericViewerOpacity = 1
        flickableArea.x = 0
        flickableArea.y = 0
        flickableArea.width = Qt.binding(() => viewerMode.width)
        flickableArea.height = Qt.binding(() => viewerMode.height)
        Qt.callLater(() => fitCurrentImageWhenReady())
    }

    function onCurrentIndexChanged() {
        let exif = sourceMasonry.view.indexExif(sourceMasonry.view.currentIndex)
        sphericViewerMode = exif["Panorama"] === "True"

        if (zoomFitView && !sphericViewerMode) {
            // console.log("onCIC FIT", viewerMode.width * dpr, viewerMode.height * dpr)
            requestCurrentViewer(viewerMode.width * dpr,
                                 viewerMode.height * dpr)
        }
        else {
            // console.log("onCIC ORIG", flickableArea.originalSize.width * dpr, flickableArea.originalSize.height * dpr)
            requestCurrentViewer()
            flickableArea.forceShowScrollBars = true
            flickableArea.forceShowScrollBars = false
        }

        updateTitle()
    }

    function updateTitle() {
        hostWindow.title = sourceMasonry.view.indexText(sourceMasonry.view.currentIndex) + " [" +
                (sourceMasonry.view.currentImageIndex + 1) + "/" + sourceMasonry.view.imageCount + "] - ZoinGallery"
    }

    function toggleCurrentSelection() {
        selectionModel.toggleSelection(currentSourceIndex())
    }

    property bool leftPressed: false
    property bool rightPressed: false
    property bool upPressed: false
    property bool downPressed: false
    property bool zoomInPressed: false
    property bool zoomOutPressed: false
    property bool controlPressed: false
    property bool shiftSelectionActive: false
    property int shiftSelectionAnchorIndex: -1
    property string shiftSelectionAnchorPath: ""
    property bool shiftNavigationSelectionValue: true

    property int previousImageIndex: -1
    // Indexes are only a projection of the current model order. Keep the
    // semantic identity as a path so a watcher reset can remap history.
    property string previousImagePath: ""
    property int lastKnownIndex: -1
    property string lastKnownPath: ""
    property bool previousImageLocked: false
    property int lockedPreviousReturnIndex: -1
    property string lockedPreviousImagePath: ""
    property string lockedPreviousReturnPath: ""
    property string lockedPreviousImageIdUrl: previousImageLocked && previousImageIndex !== -1 ? sourceMasonry.view.indexImageIdUrl(previousImageIndex) : ""
    property string lockedPreviousPath: previousImageLocked ?
                                            (lockedPreviousImagePath !== "" ? lockedPreviousImagePath :
                                                                            (previousImageIndex !== -1 ? sourceMasonry.view.indexFullPath(previousImageIndex) : "")) : ""
    property var pendingPreviousImageViewport: null

    property real viewerNavigationOffsetX: 0
    property real viewerNavigationOverdrag: 0
    property real viewerNavigationVelocityX: 0
    property real viewerNavigationLastTime: 0
    property bool viewerGestureLogging: true
    property bool viewerNavigationActive: false
    property bool viewerNavigationRevealed: false
    property bool viewerNavigationCommitAfterAnimation: false
    property bool viewerNavigationGestureActive: false
    property bool viewerNavigationGestureCommitted: false
    property bool viewerNavigationGestureHasPhase: false
    property bool viewerNavigationSuppressMomentum: false
    property int viewerNavigationGestureSerial: 0
    property int viewerNavigationDirection: 0 // -1 = previous image, 1 = next image
    property int viewerNavigationTargetIndex: -1
    property string viewerNavigationTargetPath: ""
    property string viewerNavigationTargetSource: ""
    property int viewerNavigationTargetSourceLevel: -1
    property int viewerNavigationTargetRequestWidth: -1
    property int viewerNavigationTargetRequestHeight: -1
    // Records the native-size value handed to FlickableZoomable when the
    // already-visible Fit transition frame is adopted at commit time. Besides
    // documenting the handoff contract, this keeps the value-type lifetime
    // regression observable to the standalone shell test.
    property size viewerNavigationLastAdoptedOriginalSize: Qt.size(0, 0)
    function viewerNavigationOriginalSize(index) {
        return standaloneNavigation.viewerNavigationOriginalSize(index)
    }
    function isTildeKey(event) {
        return standaloneNavigation.isTildeKey(event)
    }
    function viewerGestureNumber(value) {
        return standaloneNavigation.viewerGestureNumber(value)
    }
    function viewerGesturePhaseName(phase) {
        return standaloneNavigation.viewerGesturePhaseName(phase)
    }
    function viewerGestureDirectionName(direction) {
        return standaloneNavigation.viewerGestureDirectionName(direction)
    }
    function viewerGestureSnapshot() {
        return standaloneNavigation.viewerGestureSnapshot()
    }
    function logViewerGesture(message) {
        return standaloneNavigation.logViewerGesture(message)
    }
    function resetViewerNavigation(reason) {
        return standaloneNavigation.resetViewerNavigation(reason)
    }
    function beginViewerNavigationGesture(forceNew, hasPhase) {
        return standaloneNavigation.beginViewerNavigationGesture(forceNew, hasPhase)
    }
    function continueViewerNavigationGesture(hasPhase) {
        return standaloneNavigation.continueViewerNavigationGesture(hasPhase)
    }
    function endViewerNavigationGesture(clearCommitted) {
        return standaloneNavigation.endViewerNavigationGesture(clearCommitted)
    }
    function startViewerNavigationResidualSuppression(reason) {
        return standaloneNavigation.startViewerNavigationResidualSuppression(reason)
    }
    function clearViewerNavigationResidualSuppression(reason) {
        return standaloneNavigation.clearViewerNavigationResidualSuppression(reason)
    }
    function hiddenNavigationOffset(overdrag) {
        return standaloneNavigation.hiddenNavigationOffset(overdrag)
    }
    function updateViewerNavigationTargetSource() {
        return standaloneNavigation.updateViewerNavigationTargetSource()
    }
    function prepareViewerNavigationTarget() {
        return standaloneNavigation.prepareViewerNavigationTarget()
    }
    function beginViewerNavigation(direction) {
        return standaloneNavigation.beginViewerNavigation(direction)
    }
    function applyViewerNavigationDelta(deltaX) {
        return standaloneNavigation.applyViewerNavigationDelta(deltaX)
    }
    function viewerNavigationFinishAnimationDuration(targetOffset, shouldCommit) {
        return standaloneNavigation.viewerNavigationFinishAnimationDuration(targetOffset, shouldCommit)
    }
    function finishViewerNavigation() {
        return standaloneNavigation.finishViewerNavigation()
    }
    function commitViewerNavigation() {
        return standaloneNavigation.commitViewerNavigation()
    }
    function finishViewerNavigationAnimationNow(reason) {
        return standaloneNavigation.finishViewerNavigationAnimationNow(reason)
    }
    function wheelDeltaPixels(pixelDelta, angleDelta) {
        return standaloneNavigation.wheelDeltaPixels(pixelDelta, angleDelta)
    }
    function switchImageForLegacyWheel(angleDeltaY) {
        return standaloneNavigation.switchImageForLegacyWheel(angleDeltaY)
    }
    function isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, hasPixelDelta, nativeMomentum, nativePhase, nativeMomentumPhase) {
        return standaloneNavigation.isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, hasPixelDelta, nativeMomentum, nativePhase, nativeMomentumPhase)
    }
    function panZoomedImageFromWheel(deltaX, deltaY, recordVelocity) {
        return standaloneNavigation.panZoomedImageFromWheel(deltaX, deltaY, recordVelocity)
    }
    function scheduleWheelPanFallbackFinish() {
        return standaloneNavigation.scheduleWheelPanFallbackFinish()
    }
    function handleViewerWheel(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers, buttons, hasPixelDelta, inverted, source, deviceType, nativeMomentum, nativePhase, nativeMomentumPhase) {
        return standaloneNavigation.handleViewerWheel(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers, buttons, hasPixelDelta, inverted, source, deviceType, nativeMomentum, nativePhase, nativeMomentumPhase)
    }

    readonly property alias viewerNavigationTargetOriginalSize: standaloneNavigation.viewerNavigationTargetOriginalSize
    readonly property alias viewerNavigationTargetHasSize: standaloneNavigation.viewerNavigationTargetHasSize
    readonly property alias viewerNavigationTargetDisplayOriginalSize: standaloneNavigation.viewerNavigationTargetDisplayOriginalSize
    readonly property alias viewerNavigationTargetEffectiveOriginalSize: standaloneNavigation.viewerNavigationTargetEffectiveOriginalSize
    readonly property alias viewerNavigationTargetKeepsZoom: standaloneNavigation.viewerNavigationTargetKeepsZoom
    readonly property alias viewerNavigationTargetAspect: standaloneNavigation.viewerNavigationTargetAspect
    readonly property alias viewerNavigationTargetFitToHeight: standaloneNavigation.viewerNavigationTargetFitToHeight
    readonly property alias viewerNavigationTargetScale: standaloneNavigation.viewerNavigationTargetScale
    readonly property alias viewerNavigationTargetDisplayWidth: standaloneNavigation.viewerNavigationTargetDisplayWidth
    readonly property alias viewerNavigationTargetDisplayHeight: standaloneNavigation.viewerNavigationTargetDisplayHeight
    readonly property alias viewerNavigationTargetPreservedImageX: standaloneNavigation.viewerNavigationTargetPreservedImageX
    readonly property alias viewerNavigationTargetLeftAlignedImageX: standaloneNavigation.viewerNavigationTargetLeftAlignedImageX
    readonly property alias viewerNavigationTargetRightAlignedImageX: standaloneNavigation.viewerNavigationTargetRightAlignedImageX
    readonly property alias viewerNavigationTargetFinalImageX: standaloneNavigation.viewerNavigationTargetFinalImageX
    readonly property alias viewerNavigationTargetFinalImageY: standaloneNavigation.viewerNavigationTargetFinalImageY
    readonly property alias viewerNavigationTargetTravelDistance: standaloneNavigation.viewerNavigationTargetTravelDistance
    readonly property alias viewerNavigationProgress: standaloneNavigation.viewerNavigationProgress
    readonly property alias viewerNavigationCoverProgress: standaloneNavigation.viewerNavigationCoverProgress
    readonly property alias viewerNavigationTargetOpacity: standaloneNavigation.viewerNavigationTargetOpacity
    readonly property alias viewerNavigationCurrentOpacity: standaloneNavigation.viewerNavigationCurrentOpacity
    readonly property alias viewerNavigationCurrentOffsetX: standaloneNavigation.viewerNavigationCurrentOffsetX
    readonly property alias viewerNavigationTargetImageX: standaloneNavigation.viewerNavigationTargetImageX
    readonly property alias viewerNavigationTargetImageY: standaloneNavigation.viewerNavigationTargetImageY
    readonly property alias viewerNavigationOverdragThreshold: standaloneNavigation.viewerNavigationOverdragThreshold
    readonly property alias viewerNavigationCommitThreshold: standaloneNavigation.viewerNavigationCommitThreshold

    StandaloneViewerNavigationController {
        id: standaloneNavigation
        viewer: viewerMode
        imageViewport: flickableArea
        neighborImage: viewerNavigationNeighborImage
        navigationAnimation: viewerNavigationOffsetAnimation
        navigationFinishTimer: viewerNavigationFinishTimer
        wheelPanFinishTimer: viewerWheelPanFinishTimer
        gestureEndTimer: viewerNavigationGestureEndTimer
        residualQuietTimer: viewerNavigationResidualQuietTimer
    }

    function beginShiftSelection() {
        return selectionHistoryState.beginShiftSelection()
    }
    function updateShiftNavigationSelection(targetIndex) {
        return selectionHistoryState.updateShiftNavigationSelection(targetIndex)
    }
    function finishShiftSelection() {
        return selectionHistoryState.finishShiftSelection()
    }
    function cancelShiftSelection() {
        return selectionHistoryState.cancelShiftSelection()
    }
    function clearPreviousImage() {
        return selectionHistoryState.clearPreviousImage()
    }
    function pathForIndex(index) {
        return selectionHistoryState.pathForIndex(index)
    }
    function effectiveSizeFromOriginalSize(originalSize) {
        return selectionHistoryState.effectiveSizeFromOriginalSize(originalSize)
    }
    function fitScaleForEffectiveSize(size) {
        return selectionHistoryState.fitScaleForEffectiveSize(size)
    }
    function indexForPath(path) {
        return selectionHistoryState.indexForPath(path)
    }
    function restorePreviousImageLockIndexes() {
        return selectionHistoryState.restorePreviousImageLockIndexes()
    }
    function rememberViewportForPreviousImageSwitch(targetIndex) {
        return selectionHistoryState.rememberViewportForPreviousImageSwitch(targetIndex)
    }
    function applyPendingPreviousImageViewport() {
        return selectionHistoryState.applyPendingPreviousImageViewport()
    }
    function togglePreviousImageLock(index) {
        return selectionHistoryState.togglePreviousImageLock(index)
    }
    function switchToPreviousImage(currentIndex) {
        return selectionHistoryState.switchToPreviousImage(currentIndex)
    }

    StandaloneViewerSelectionHistoryController {
        id: selectionHistoryState
        viewer: viewerMode
        imageViewport: flickableArea
    }

    StandaloneViewerReconciler {
        viewer: viewerMode
        imageViewport: flickableArea
    }

    StandaloneViewerInput {
        id: inputState
        viewer: viewerMode
        imageViewport: flickableArea
    }

    Keys.onPressed: event => inputState.handlePressed(event)
    Keys.onReleased: event => inputState.handleReleased(event)

    StandaloneViewerSurface {
        id: viewerSurface
        anchors.fill: parent
        viewer: viewerMode
        shell: viewerMode.shell
        hostWindow: viewerMode.hostWindow
        titleBarItem: viewerMode.titleBarItem
        topChrome: topPanel
        rightChrome: rightPanel
        devicePixelRatio: viewerMode.devicePixelRatio
    }
    StandaloneViewerChrome {
        id: viewerChrome
        anchors.fill: parent
        viewer: viewerMode
        shell: viewerMode.shell
        hostWindow: viewerMode.hostWindow
        titleBarItem: viewerMode.titleBarItem
        imageViewport: flickableArea
        viewportContainer: flickableAreaContainer
        sphereLoader: sphericViewerLoader
        minimizeButton: viewerMode.minimizeButton
        maximizeButton: viewerMode.maximizeButton
        closeButton: viewerMode.closeButton
        quickWindowKitEnabled: viewerMode.quickWindowKitEnabled
        devicePixelRatio: viewerMode.devicePixelRatio
    }
}
