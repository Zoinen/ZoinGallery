pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

FocusScope {
    id: root

    readonly property alias flickableArea: viewerSurface.viewport
    readonly property alias transitionFrame: viewerSurface.transitionFrame
    readonly property alias viewerNavigationNeighborImage:
        viewerSurface.navigationNeighborImage
    readonly property alias sphericViewerLoader: viewerSurface.sphericLoader
    readonly property alias transitionAnimation:
        motionState.transitionAnimation
    readonly property alias transitionFinalizeTimer:
        motionState.transitionFinalizeTimer
    readonly property alias pinchCloseFinalizeTimer:
        motionState.pinchCloseFinalizeTimer
    readonly property alias pinchCloseProgressAnimation:
        motionState.pinchCloseProgressAnimation
    readonly property alias viewerNavigationOffsetAnimation:
        motionState.navigationOffsetAnimation
    readonly property alias decodeRequestTimer:
        motionState.decodeRequestTimer
    readonly property alias committedViewportTimer:
        motionState.committedViewportTimer
    readonly property alias previousViewportTimer:
        motionState.previousViewportTimer
    readonly property alias authorityTimer: motionState.authorityTimer
    readonly property alias viewerNavigationFinishTimer:
        motionState.navigationFinishTimer
    readonly property alias viewerWheelPanFinishTimer:
        motionState.wheelPanFinishTimer
    readonly property alias viewerNavigationGestureEndTimer:
        motionState.navigationGestureEndTimer
    readonly property alias viewerNavigationResidualQuietTimer:
        motionState.navigationResidualQuietTimer
    readonly property bool viewerNavigationAnimationRunning:
        viewerNavigationOffsetAnimation.running

    property var session: null
    property var sourcePanel: null
    property GalleryThemePalette theme: GalleryThemePalette {}
    property var hostCapabilities: ({})
    property real devicePixelRatio: 1.0
    property Item customContent: null
    property bool autoFocus: true
    property int animationDuration: 150

    readonly property bool transitioning: transitionAnimation.running
                                                || transitionFinalizeTimer.active
                                                || pinchCloseProgressAnimation.running
                                                || pinchCloseActive
    readonly property real fittedScale: flickableArea.fitZoomScale()
    readonly property real zoomFactor: fittedScale > 0.0001
                                               ? flickableArea.zoomScale / fittedScale
                                               : 1.0
    readonly property real minimumZoom: 1.0
    readonly property real maximumZoom: fittedScale > 0.0001
                                               ? flickableArea.maxZoomScale / fittedScale
                                               : 128.0
    readonly property real maximumPanX:
        Math.max(0, (flickableArea.image.width - transitionFrame.width) / 2)
    readonly property real maximumPanY:
        Math.max(0, (flickableArea.image.height - transitionFrame.height) / 2)
    readonly property real panX: flickableArea.image.x
                                        - (transitionFrame.width
                                           - flickableArea.image.width) / 2
    readonly property real panY: flickableArea.image.y
                                        - (transitionFrame.height
                                           - flickableArea.image.height) / 2

    property int presentedIndex: -1
    property string presentedEntryId: ""
    property url currentSourceValue: ""
    property int currentSourceLevelValue: -1
    property var currentSourcesValue: []
    property size currentOriginalSizeValue: Qt.size(0, 0)
    property int appliedPresentedIndex: -1
    property string appliedTierSignature: ""
    property int pendingAuthorityIndex: -1
    property string pendingAuthorityEntryId: ""
    property int pendingPreviousViewportAttempts: 0

    property real transitionProgress: 0
    property rect transitionSourceGeometry: Qt.rect(0, 0, 0, 0)
    property url transitionThumbnailSource: ""
    property bool transitionHasGeometry: false
    property bool completingClose: false
    property bool viewerContentVisible: true
    property bool returningFromPinch: false
    property bool pinchCloseActive: false
    property bool pinchCloseFinishingCommit: false
    property real pinchCloseProgress: 0
    // The single reveal value consumers should use for chrome/background
    // fading. During a normal expand/collapse it follows transitionProgress;
    // during an interactive pinch close it follows the gesture itself.
    readonly property real surfaceProgress: pinchCloseActive
                                                ? 1 - pinchCloseProgress
                                                : transitionProgress
    property rect pinchCloseStartGeometry: Qt.rect(0, 0, 0, 0)
    property rect pinchCloseTargetGeometry: Qt.rect(0, 0, 0, 0)

    onPinchCloseProgressChanged: applyPinchCloseProgress()

    // These are the exact held-key state flags used by the original
    // ViewerMode.  FlickableZoomable's FrameAnimation consumes them, so key
    // repeat timing from the window system never changes movement speed.
    property bool leftPressed: false
    property bool rightPressed: false
    property bool upPressed: false
    property bool downPressed: false
    property bool zoomInPressed: false
    property bool zoomOutPressed: false
    property bool controlPressed: false
    property bool shiftSelectionActive: false
    property int shiftSelectionAnchorIndex: -1
    property int shiftSelectionTargetIndex: -1
    property bool shiftSelectionAdds: true
    // The standalone viewer uses this to reveal its side chrome.  Embedded
    // hosts intentionally omit that chrome, but the key remains local and the
    // state is retained so the reusable component keeps ViewerMode's contract.
    property bool panelsVisible: false
    property bool sphericViewerMode: false
    onSphericViewerModeChanged: {
        if (sphericViewerMode)
            requestImage()
    }

    property real viewerNavigationOffsetX: 0
    property real viewerNavigationOverdrag: 0
    property real viewerNavigationVelocityX: 0
    property real viewerNavigationLastTime: 0
    property bool viewerNavigationActive: false
    property bool viewerNavigationRevealed: false
    property bool viewerNavigationCommitAfterAnimation: false
    property bool viewerNavigationGestureActive: false
    property bool viewerNavigationGestureCommitted: false
    property bool viewerNavigationGestureHasPhase: false
    property bool viewerNavigationSuppressMomentum: false
    property int viewerNavigationDirection: 0
    property int viewerNavigationTargetIndex: -1
    property url viewerNavigationTargetSource: ""
    property int viewerNavigationTargetSourceLevel: -1
    property size viewerNavigationTargetOriginalSize: Qt.size(0, 0)
    property var pendingCommittedViewport: null
    property int pendingCommittedViewportAttempts: 0

    readonly property bool viewerNavigationTargetHasSize:
        viewerNavigationTargetOriginalSize.width > 1
        && viewerNavigationTargetOriginalSize.height > 1
        && transitionFrame.width > 0 && transitionFrame.height > 0
    readonly property size viewerNavigationTargetDisplayOriginalSize:
        viewerNavigationTargetHasSize
        ? Qt.size(viewerNavigationTargetOriginalSize.width / devicePixelRatio,
                  viewerNavigationTargetOriginalSize.height / devicePixelRatio)
        : Qt.size(0, 0)
    readonly property size viewerNavigationTargetEffectiveOriginalSize:
        viewerNavigationTargetHasSize
        ? (flickableArea.rotationMode % 2 === 1
           ? Qt.size(viewerNavigationTargetDisplayOriginalSize.height,
                     viewerNavigationTargetDisplayOriginalSize.width)
           : viewerNavigationTargetDisplayOriginalSize)
        : Qt.size(0, 0)
    readonly property bool viewerNavigationTargetKeepsZoom:
        viewerNavigationTargetHasSize && !flickableArea.zoomFitView
    readonly property real viewerNavigationTargetAspect:
        viewerNavigationTargetHasSize
        ? viewerNavigationTargetEffectiveOriginalSize.width
          / viewerNavigationTargetEffectiveOriginalSize.height : 1
    readonly property bool viewerNavigationTargetFitToHeight:
        viewerNavigationTargetHasSize
        ? viewerNavigationTargetAspect
          <= transitionFrame.width / transitionFrame.height : false
    readonly property real viewerNavigationTargetScale:
        !viewerNavigationTargetHasSize ? 1
        : viewerNavigationTargetKeepsZoom ? flickableArea.zoomScale
        : viewerNavigationTargetFitToHeight
          ? transitionFrame.height
            / viewerNavigationTargetEffectiveOriginalSize.height
          : transitionFrame.width
            / viewerNavigationTargetEffectiveOriginalSize.width
    readonly property real viewerNavigationTargetDisplayWidth:
        viewerNavigationTargetHasSize
        ? viewerNavigationTargetEffectiveOriginalSize.width
          * viewerNavigationTargetScale : transitionFrame.width
    readonly property real viewerNavigationTargetDisplayHeight:
        viewerNavigationTargetHasSize
        ? viewerNavigationTargetEffectiveOriginalSize.height
          * viewerNavigationTargetScale : transitionFrame.height
    readonly property real viewerNavigationTargetPreservedImageX:
        viewerNavigationTargetDisplayWidth < transitionFrame.width
        ? (transitionFrame.width - viewerNavigationTargetDisplayWidth) * 0.5
        : Math.min(0, Math.max(flickableArea.image.x,
                              transitionFrame.width
                              - viewerNavigationTargetDisplayWidth))
    readonly property real viewerNavigationTargetLeftAlignedImageX:
        viewerNavigationTargetDisplayWidth < transitionFrame.width
        ? (transitionFrame.width - viewerNavigationTargetDisplayWidth) * 0.5
        : 0
    readonly property real viewerNavigationTargetRightAlignedImageX:
        viewerNavigationTargetDisplayWidth < transitionFrame.width
        ? (transitionFrame.width - viewerNavigationTargetDisplayWidth) * 0.5
        : transitionFrame.width - viewerNavigationTargetDisplayWidth
    readonly property real viewerNavigationTargetFinalImageX:
        viewerNavigationTargetKeepsZoom
        ? (viewerNavigationDirection < 0
           ? viewerNavigationTargetRightAlignedImageX
           : viewerNavigationDirection > 0
             ? viewerNavigationTargetLeftAlignedImageX
             : viewerNavigationTargetPreservedImageX)
        : viewerNavigationTargetPreservedImageX
    readonly property real viewerNavigationTargetFinalImageY:
        viewerNavigationTargetDisplayHeight < transitionFrame.height
        ? (transitionFrame.height - viewerNavigationTargetDisplayHeight) * 0.5
        : Math.min(0, Math.max(flickableArea.image.y,
                              transitionFrame.height
                              - viewerNavigationTargetDisplayHeight))
    readonly property real viewerNavigationTargetTravelDistance:
        viewerNavigationDirection < 0
        ? Math.max(1, viewerNavigationTargetDisplayWidth
                   + viewerNavigationTargetFinalImageX)
        : Math.max(1, transitionFrame.width)

    readonly property real viewerNavigationProgress:
        Math.min(1, Math.abs(viewerNavigationOffsetX)
                 / Math.max(1, width * 0.5))
    readonly property real viewerNavigationCoverProgress:
        Math.min(1, Math.abs(viewerNavigationOffsetX)
                 / viewerNavigationTargetTravelDistance)
    readonly property real viewerNavigationTargetOpacity:
        viewerNavigationDirection < 0 ? 1 : viewerNavigationProgress
    readonly property real viewerNavigationCurrentOpacity:
        viewerNavigationDirection < 0 && viewerNavigationTargetIndex !== -1
        ? 1 - viewerNavigationCoverProgress : 1
    readonly property real viewerNavigationCurrentOffsetX:
        viewerNavigationDirection < 0 && viewerNavigationTargetIndex !== -1
        ? 0 : viewerNavigationOffsetX
    readonly property real viewerNavigationTargetImageX:
        viewerNavigationDirection < 0
        ? -viewerNavigationTargetDisplayWidth
          + Math.min(Math.max(viewerNavigationOffsetX, 0),
                     viewerNavigationTargetTravelDistance)
        : viewerNavigationTargetFinalImageX
    readonly property real viewerNavigationTargetImageY:
        viewerNavigationTargetFinalImageY
    readonly property real viewerNavigationOverdragThreshold:
        Math.min(48, width * 0.08)
    readonly property real viewerNavigationCommitThreshold:
        Math.min(120, Math.max(viewerNavigationOverdragThreshold * 1.35,
                              width * 0.12))

    signal navigationRequested(string entryId, int sourceIndex)
    signal selectionRequested(string mode, var entryIds)
    signal fullscreenToggleRequested()
    signal sphereScrollingMouseCursorRequested(bool set, bool idle,
                                               real rotation)
    signal closeCompleted()
    // Compatibility alias.  Its semantics are intentionally completion-time:
    // embedders must keep the viewer alive until the reverse animation ends.
    signal closeRequested()

    readonly property color requestedBackgroundColor:
        theme && theme.viewerBackground !== undefined
        ? theme.viewerBackground : "#090a0c"
    readonly property color backgroundColor: requestedBackgroundColor
    readonly property color foregroundColor:
        theme && theme.text !== undefined ? theme.text : "#f3f4f6"

    function validGeometry(geometry) {
        return geometry !== undefined && geometry !== null
                && geometry.width > 1 && geometry.height > 1
    }

    function lerp(first, second, progress) {
        return first + (second - first) * progress
    }

    function entryIdAt(index) {
        return catalogState.entryIdAt(index)
    }
    function indexForEntryId(entryId) {
        return catalogState.indexForEntryId(entryId)
    }
    function previousViewport() {
        return catalogState.previousViewport()
    }
    function setPreviousState(previousEntryId, returnEntryId, locked,
                              viewport) {
        catalogState.setPreviousState(previousEntryId, returnEntryId,
                                      locked, viewport)
    }
    function clearPendingPreviousViewport() {
        catalogState.clearPendingPreviousViewport()
    }
    function isTildeKey(event) {
        return catalogState.isTildeKey(event)
    }
    function effectiveSizeFromOriginalSize(originalSize) {
        return catalogState.effectiveSizeFromOriginalSize(originalSize)
    }
    function fitScaleForEffectiveSize(size) {
        return catalogState.fitScaleForEffectiveSize(size)
    }
    function rememberViewportForPreviousImageSwitch(targetIndex) {
        catalogState.rememberViewportForPreviousImageSwitch(targetIndex)
    }
    function applyPendingPreviousImageViewport() {
        return catalogState.applyPendingPreviousImageViewport()
    }
    function originalSizeAt(index) {
        return catalogState.originalSizeAt(index)
    }
    function sourceTiersAt(index) {
        return catalogState.sourceTiersAt(index)
    }
    function refreshCurrentSource(forceIndexChange, previousFitMode) {
        catalogState.refreshCurrentSource(forceIndexChange, previousFitMode)
    }
    function refreshNeighborSource() {
        catalogState.refreshNeighborSource()
    }
    function requestIndex(index, scaleRatio) {
        catalogState.requestIndex(index, scaleRatio)
    }
    function requestImage() {
        catalogState.requestImage()
    }
    function setPresentedIndex(index, awaitAuthority) {
        return catalogState.setPresentedIndex(index, awaitAuthority)
    }
    function recordPresentedTransition(fromEntryId, toEntryId,
                                       preservePendingViewport) {
        catalogState.recordPresentedTransition(
                    fromEntryId, toEntryId, preservePendingViewport)
    }
    function emitNavigation(index, preservePendingViewport) {
        return catalogState.emitNavigation(index, preservePendingViewport)
    }
    function adjacentIndex(fromIndex, direction) {
        return catalogState.adjacentIndex(fromIndex, direction)
    }
    function navigate(direction) {
        return catalogState.navigate(direction)
    }
    function togglePreviousImageLock(index) {
        catalogState.togglePreviousImageLock(index)
    }
    function switchToPreviousImage() {
        return catalogState.switchToPreviousImage()
    }
    function togglePreviousImage() {
        return catalogState.togglePreviousImage()
    }
    function navigateToEnd(direction) {
        return catalogState.navigateToEnd(direction)
    }

    GalleryViewerCatalogController {
        id: catalogState
        viewer: root
        imageViewport: root.flickableArea
        frame: root.transitionFrame
        motion: motionState
    }

    function requestCurrentSelection(mode) {
        return selectionState.requestCurrentSelection(mode)
    }
    function beginShiftSelection() {
        selectionState.beginShiftSelection()
    }
    function updateShiftNavigationSelection(targetIndex) {
        selectionState.updateShiftNavigationSelection(targetIndex)
    }
    function finishShiftSelection() {
        selectionState.finishShiftSelection()
    }
    function cancelShiftSelection() {
        selectionState.cancelShiftSelection()
    }

    GalleryViewerSelectionController {
        id: selectionState
        viewer: root
    }

    function setZoom(value, focusX, focusY) {
        const bounded = Math.max(minimumZoom,
                                 Math.min(maximumZoom, value))
        const targetScale = flickableArea.clampZoomScale(
                    flickableArea.fitZoomScale() * bounded)
        if (Number.isFinite(focusX) && Number.isFinite(focusY))
            flickableArea.setZoomScaleAt(targetScale, focusX, focusY)
        else
            flickableArea.zoomToScale(targetScale, false)
        decodeRequestTimer.start()
    }

    function zoomIn() { setZoom(zoomFactor * 1.25) }
    function zoomOut() { setZoom(zoomFactor / 1.25) }
    function resetView() {
        flickableArea.zoomToFit(true)
        decodeRequestTimer.start()
    }
    function clampPan() { flickableArea.fitViewerImageInViewportBounds() }

    function scheduleDecodeRequest() { decodeRequestTimer.start() }

    function updateHeldKeyMotion() {
        const speed = controlPressed ? 0.06 : 1
        flickableArea.startZoomScrollingAnimation(
                    leftPressed ? speed : rightPressed ? -speed : 0,
                    upPressed ? speed : downPressed ? -speed : 0,
                    zoomInPressed ? speed : zoomOutPressed ? -speed : 0)
        if (!leftPressed && !rightPressed && !upPressed && !downPressed
                && !zoomInPressed && !zoomOutPressed)
            decodeRequestTimer.start()
    }

    function clearHeldKeys() {
        leftPressed = false
        rightPressed = false
        upPressed = false
        downPressed = false
        zoomInPressed = false
        zoomOutPressed = false
        controlPressed = false
        cancelShiftSelection()
        flickableArea.startZoomScrollingAnimation(0, 0, 0)
    }

    function ownsZoomKey(event) {
        if (customContent)
            return false
        return event.key === Qt.Key_Plus
                || event.key === Qt.Key_Equal
                || event.key === Qt.Key_Minus
                || event.key === Qt.Key_0
    }

    function ownsKey(event) {
        // A full-area viewer is a modal keyboard surface.  Known keys are
        // dispatched below; unknown/function/text/paste keys are deliberately
        // accepted as no-ops instead of escaping into an embedding commander.
        return !customContent
    }

    function setPanelTransition(active) {
        transitionState.setPanelTransition(active)
    }
    function captureTransitionTarget() {
        return transitionState.captureTransitionTarget()
    }
    function beginOpen() {
        transitionState.beginOpen()
    }
    function finishOpen() {
        transitionState.finishOpen()
    }
    function finishClose() {
        transitionState.finishClose()
    }
    function requestClose() {
        transitionState.requestClose()
    }
    function closeViewer() {
        transitionState.requestClose()
    }
    function currentViewerImageGeometry() {
        return transitionState.currentViewerImageGeometry()
    }
    function beginPinchClose() {
        return transitionState.beginPinchClose()
    }
    function cancelPinchCloseDuringGesture() {
        transitionState.cancelPinchCloseDuringGesture()
    }
    function completePinchCloseReturn() {
        transitionState.completePinchCloseReturn()
    }
    function applyPinchCloseProgress() {
        transitionState.applyPinchCloseProgress()
    }
    function updatePinchClose(progress) {
        transitionState.updatePinchClose(progress)
    }
    function finishPinchClose(commit) {
        transitionState.finishPinchClose(commit)
    }
    function completePinchCloseCommit() {
        transitionState.completePinchCloseCommit()
    }
    function completeTransition() {
        transitionState.completeTransition()
    }

    GalleryViewerTransitionController {
        id: transitionState
        viewer: root
        imageViewport: root.flickableArea
        motion: motionState
    }

    function resetViewerNavigation() {
        navigationState.resetViewerNavigation()
    }
    function beginViewerNavigationGesture(forceNew, hasPhase) {
        navigationState.beginViewerNavigationGesture(forceNew, hasPhase)
    }
    function continueViewerNavigationGesture(hasPhase) {
        navigationState.continueViewerNavigationGesture(hasPhase)
    }
    function endViewerNavigationGesture(clearCommitted) {
        navigationState.endViewerNavigationGesture(clearCommitted)
    }
    function startViewerNavigationResidualSuppression() {
        navigationState.startViewerNavigationResidualSuppression()
    }
    function clearViewerNavigationResidualSuppression() {
        navigationState.clearViewerNavigationResidualSuppression()
    }
    function hiddenNavigationOffset(overdrag) {
        return navigationState.hiddenNavigationOffset(overdrag)
    }
    function beginViewerNavigation(direction) {
        navigationState.beginViewerNavigation(direction)
    }
    function applyViewerNavigationDelta(deltaX) {
        navigationState.applyViewerNavigationDelta(deltaX)
    }
    function finishViewerNavigation() {
        navigationState.finishViewerNavigation()
    }
    function commitViewerNavigation() {
        navigationState.commitViewerNavigation()
    }
    function applyPendingCommittedViewport() {
        navigationState.applyPendingCommittedViewport()
    }
    function finishViewerNavigationAnimationNow() {
        navigationState.finishViewerNavigationAnimationNow()
    }
    function wheelDeltaPixels(pixelDelta, angleDelta) {
        return navigationState.wheelDeltaPixels(pixelDelta, angleDelta)
    }
    function isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY,
                                      angleDeltaX, angleDeltaY, phase,
                                      hasPixelDelta, nativeMomentum,
                                      nativePhase, nativeMomentumPhase) {
        return navigationState.isLegacyWheelImageSwitch(
                    pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                    phase, hasPixelDelta, nativeMomentum, nativePhase,
                    nativeMomentumPhase)
    }
    function panZoomedImageFromWheel(deltaX, deltaY, recordVelocity) {
        return navigationState.panZoomedImageFromWheel(
                    deltaX, deltaY, recordVelocity)
    }
    function scheduleWheelPanFallbackFinish() {
        navigationState.scheduleWheelPanFallbackFinish()
    }
    function handleViewerWheel(pixelDeltaX, pixelDeltaY,
                               angleDeltaX, angleDeltaY, phase, modifiers,
                               buttons, hasPixelDelta, inverted, source,
                               deviceType, nativeMomentum, nativePhase,
                               nativeMomentumPhase) {
        navigationState.handleViewerWheel(
                    pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                    phase, modifiers, buttons, hasPixelDelta, inverted,
                    source, deviceType, nativeMomentum, nativePhase,
                    nativeMomentumPhase)
    }

    GalleryViewerNavigation {
        id: navigationState
        viewer: root
        viewport: root.flickableArea
        motion: motionState
        neighborImage: root.viewerNavigationNeighborImage
    }

    GalleryViewerSurface {
        id: viewerSurface
        anchors.fill: parent
        viewer: root
    }

    GalleryViewerInput {
        id: inputState
        viewer: root
        viewport: root.flickableArea
    }

    Keys.onPressed: event => inputState.handlePressed(event)
    Keys.onReleased: event => inputState.handleReleased(event)

    GalleryViewerMotion {
        id: motionState
        viewer: root
        viewport: root.flickableArea
    }

    GalleryViewerSessionReconciler {
        viewer: root
        viewport: root.flickableArea
        motion: motionState
    }

    onSessionChanged: {
        presentedIndex = session ? session.currentIndex : -1
        presentedEntryId = session && presentedIndex >= 0
                ? session.entryIdAt(presentedIndex) : ""
        appliedPresentedIndex = -1
        appliedTierSignature = ""
        refreshCurrentSource(true, true)
        Qt.callLater(requestImage)
    }
    onDevicePixelRatioChanged: decodeRequestTimer.start()
    onWidthChanged: decodeRequestTimer.start()
    onHeightChanged: decodeRequestTimer.start()

    Component.onCompleted: {
        if (!customContent) {
            presentedIndex = session ? session.currentIndex : -1
            presentedEntryId = session && presentedIndex >= 0
                    ? session.entryIdAt(presentedIndex) : ""
            refreshCurrentSource(appliedPresentedIndex !== presentedIndex,
                                 flickableArea.zoomFitView)
            Qt.callLater(beginOpen)
        }
    }
    Component.onDestruction: setPanelTransition(false)
}
