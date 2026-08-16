import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

FocusScope {
    id: root

    property var session: null
    property var sourcePanel: null
    property var theme: ({})
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
        return session && index >= 0 ? session.entryIdAt(index) : ""
    }

    function indexForEntryId(entryId) {
        if (!session || entryId === "")
            return -1
        if (typeof session.indexForEntryId === "function")
            return session.indexForEntryId(entryId)
        return -1
    }

    function previousViewport() {
        return session && session.viewerPreviousViewport
                ? session.viewerPreviousViewport : ({})
    }

    function setPreviousState(previousEntryId, returnEntryId, locked,
                              viewport) {
        if (!session
                || typeof session.setViewerPreviousState !== "function")
            return
        session.setViewerPreviousState(previousEntryId, returnEntryId, locked,
                                       viewport === undefined ? ({})
                                                              : viewport)
    }

    function clearPendingPreviousViewport() {
        if (!session)
            return
        setPreviousState(session.viewerPreviousEntryId,
                         session.viewerPreviousReturnEntryId,
                         session.viewerPreviousLocked, ({}))
    }

    function isTildeKey(event) {
        return event.key === Qt.Key_QuoteLeft
                || event.key === Qt.Key_AsciiTilde
                || event.key === 1025
    }

    function effectiveSizeFromOriginalSize(originalSize) {
        if (originalSize.width <= 1 || originalSize.height <= 1)
            return Qt.size(0, 0)
        const displaySize = Qt.size(
                    originalSize.width / devicePixelRatio,
                    originalSize.height / devicePixelRatio)
        return flickableArea.rotationMode % 2 === 1
                ? Qt.size(displaySize.height, displaySize.width)
                : displaySize
    }

    function fitScaleForEffectiveSize(size) {
        if (size.width <= 1 || size.height <= 1
                || transitionFrame.width <= 1
                || transitionFrame.height <= 1)
            return 1
        return size.width / size.height
                <= transitionFrame.width / transitionFrame.height
                ? transitionFrame.height / size.height
                : transitionFrame.width / size.width
    }

    function rememberViewportForPreviousImageSwitch(targetIndex) {
        clearPendingPreviousViewport()
        if (!session || targetIndex < 0 || flickableArea.zoomFitView
                || flickableArea.effectiveOriginalSize.width <= 1
                || flickableArea.effectiveOriginalSize.height <= 1)
            return

        const targetSize = effectiveSizeFromOriginalSize(
                    originalSizeAt(targetIndex))
        if (targetSize.width <= 1 || targetSize.height <= 1)
            return

        const sourceSize = flickableArea.effectiveOriginalSize
        const sourceFitScale = fitScaleForEffectiveSize(sourceSize)
        setPreviousState(session.viewerPreviousEntryId,
                         session.viewerPreviousReturnEntryId,
                         session.viewerPreviousLocked, {
            targetEntryId: entryIdAt(targetIndex),
            centerRatioX: ((transitionFrame.width / 2
                            - flickableArea.image.x)
                           / flickableArea.zoomScale) / sourceSize.width,
            centerRatioY: ((transitionFrame.height / 2
                            - flickableArea.image.y)
                           / flickableArea.zoomScale) / sourceSize.height,
            zoomToFitRatio: flickableArea.zoomScale / sourceFitScale
        })
        pendingPreviousViewportAttempts = 0
    }

    function applyPendingPreviousImageViewport() {
        if (!session)
            return false
        const viewport = previousViewport()
        const targetEntryId = viewport.targetEntryId === undefined
                ? "" : viewport.targetEntryId
        if (targetEntryId === "")
            return false
        if (targetEntryId !== presentedEntryId) {
            clearPendingPreviousViewport()
            return false
        }
        if (flickableArea.effectiveOriginalSize.width <= 1
                || flickableArea.effectiveOriginalSize.height <= 1) {
            if (++pendingPreviousViewportAttempts < 40)
                previousViewportTimer.restart()
            return false
        }

        const targetSize = flickableArea.effectiveOriginalSize
        const targetZoom = flickableArea.clampZoomScale(
                    fitScaleForEffectiveSize(targetSize)
                    * Number(viewport.zoomToFitRatio))
        const targetX = transitionFrame.width / 2
                - targetSize.width * Number(viewport.centerRatioX)
                  * targetZoom
        const targetY = transitionFrame.height / 2
                - targetSize.height * Number(viewport.centerRatioY)
                  * targetZoom
        flickableArea.setViewport(targetZoom, targetX, targetY)
        clearPendingPreviousViewport()
        pendingPreviousViewportAttempts = 0
        decodeRequestTimer.start()
        return true
    }

    function originalSizeAt(index) {
        if (!session || index < 0
                || typeof session.imageOriginalSizeAt !== "function")
            return Qt.size(0, 0)
        return session.imageOriginalSizeAt(index)
    }

    function sourceTiersAt(index) {
        if (!session || index < 0)
            return []
        if (typeof session.viewerSourcesAt === "function")
            return session.viewerSourcesAt(index)
        const fallback = session.viewerSourceAt(index)
        return fallback.toString() === "" ? []
                                         : [{ source: fallback, level: 0 }]
    }

    function refreshCurrentSource(forceIndexChange, previousFitMode) {
        if (!session || presentedIndex < 0) {
            currentSourceValue = ""
            currentSourceLevelValue = -1
            currentSourcesValue = []
            currentOriginalSizeValue = Qt.size(0, 0)
            appliedPresentedIndex = -1
            appliedTierSignature = ""
            return
        }

        const sources = sourceTiersAt(presentedIndex)
        currentSourcesValue = sources
        currentOriginalSizeValue = originalSizeAt(presentedIndex)
        if (sources.length > 0) {
            const best = sources[sources.length - 1]
            currentSourceValue = best.source
            currentSourceLevelValue = Number(best.level)
        } else {
            currentSourceValue = ""
            currentSourceLevelValue = -1
        }

        const indexChanged = forceIndexChange === true
                || appliedPresentedIndex !== presentedIndex
        const wasFit = previousFitMode === undefined
                ? flickableArea.zoomFitView : previousFitMode
        let signature = presentedIndex + ":"
                + currentOriginalSizeValue.width + "x"
                + currentOriginalSizeValue.height
        for (let signatureIndex = 0; signatureIndex < sources.length;
             ++signatureIndex) {
            const signatureTier = sources[signatureIndex]
            signature += "|" + Number(signatureTier.level) + ":"
                    + signatureTier.source.toString()
        }
        if (indexChanged || signature !== appliedTierSignature) {
            for (let sourceIndex = 0; sourceIndex < sources.length;
                 ++sourceIndex) {
                const tier = sources[sourceIndex]
                if (!tier || tier.source === undefined
                        || tier.source.toString() === "")
                    continue
                flickableArea.setImage(tier.source, currentOriginalSizeValue,
                                       presentedIndex, Number(tier.level))
            }
            appliedTierSignature = signature
        }

        if (sources.length > 0) {
            appliedPresentedIndex = presentedIndex
            if (indexChanged) {
                if (pendingCommittedViewport
                        && pendingCommittedViewport.index === presentedIndex) {
                    committedViewportTimer.restart()
                } else if (applyPendingPreviousImageViewport()) {
                    // Previous-image switching restores the same normalized
                    // center and zoom-to-fit ratio as ViewerMode.
                } else if (wasFit) {
                    flickableArea.zoomToFit(true)
                } else {
                    // ViewerMode preserves the numeric zoom on an index
                    // change and only brings the old x/y into the new image's
                    // legal bounds.  It never silently returns to Fit.
                    flickableArea.fitViewerImageInViewportBounds()
                }
            } else {
                applyPendingPreviousImageViewport()
            }
        }
    }

    function refreshNeighborSource() {
        if (!session || viewerNavigationTargetIndex < 0) {
            viewerNavigationTargetSource = ""
            viewerNavigationTargetSourceLevel = -1
            viewerNavigationTargetOriginalSize = Qt.size(0, 0)
            return
        }
        const sources = sourceTiersAt(viewerNavigationTargetIndex)
        const requiredLevel = flickableArea.zoomFitView
                && !sphericViewerMode ? 1 : 2
        let preparedSource = ""
        for (let sourceIndex = 0; sourceIndex < sources.length;
             ++sourceIndex) {
            const tier = sources[sourceIndex]
            if (tier && Number(tier.level) === requiredLevel
                    && tier.source !== undefined
                    && tier.source.toString() !== "")
                preparedSource = tier.source
        }
        // A level-0 masonry image and a tier prepared for another viewer mode
        // are useful fallbacks for the main tiered viewport, but are not valid
        // transition frames. Showing either here is the visible blur/native
        // flash which the original predecoded swipe avoided.
        viewerNavigationTargetSource = preparedSource
        viewerNavigationTargetSourceLevel = preparedSource.toString() === ""
                ? -1 : requiredLevel
        viewerNavigationTargetOriginalSize =
                originalSizeAt(viewerNavigationTargetIndex)
    }

    function requestIndex(index, scaleRatio) {
        if (customContent || !session || index < 0
                || width <= 0 || height <= 0)
            return
        const ratio = Math.max(1, scaleRatio === undefined ? 1 : scaleRatio)
        const requestedWidth = Math.ceil(width * devicePixelRatio * ratio)
        const requestedHeight = Math.ceil(height * devicePixelRatio * ratio)
        if (index === session.currentIndex)
            session.requestViewer(requestedWidth, requestedHeight)
        else
            session.requestViewerAt(index, requestedWidth, requestedHeight)
    }

    function requestImage() {
        if (customContent || !session || presentedIndex < 0)
            return
        if (flickableArea.zoomFitView && !sphericViewerMode) {
            requestIndex(presentedIndex, 1)
            return
        }
        // The original viewer uses a zero target as its native/full-size
        // sentinel.  Keeping that sentinel is important for the active
        // 16-image decode plan: every differently-sized neighbor receives its
        // own native tier rather than inheriting this image's dimensions.
        if (presentedIndex === session.currentIndex)
            session.requestViewer(0, 0)
        else
            session.requestViewerAt(presentedIndex, 0, 0)
    }

    function setPresentedIndex(index, awaitAuthority) {
        if (!session || index < 0)
            return false
        const nextEntryId = entryIdAt(index)
        if (nextEntryId === "")
            return false
        const previousIndex = presentedIndex
        const sameIdentity = presentedEntryId !== ""
                && presentedEntryId === nextEntryId
        const indexChanged = !sameIdentity
        const previousFitMode = flickableArea.zoomFitView
        if (sameIdentity && previousIndex !== index) {
            flickableArea.remapImageIndex(previousIndex, index)
            if (appliedPresentedIndex === previousIndex)
                appliedPresentedIndex = index
        }
        presentedIndex = index
        presentedEntryId = nextEntryId
        refreshCurrentSource(indexChanged, previousFitMode)
        requestImage()
        if (awaitAuthority) {
            pendingAuthorityIndex = index
            pendingAuthorityEntryId = nextEntryId
            authorityTimer.restart()
        } else {
            pendingAuthorityIndex = -1
            pendingAuthorityEntryId = ""
            authorityTimer.stop()
        }
        return true
    }

    function recordPresentedTransition(fromEntryId, toEntryId,
                                       preservePendingViewport) {
        if (!session || fromEntryId === "" || toEntryId === ""
                || fromEntryId === toEntryId)
            return
        const viewport = preservePendingViewport
                ? previousViewport() : ({})
        if (session.viewerPreviousLocked) {
            const lockedEntryId = session.viewerPreviousEntryId
            let returnEntryId = session.viewerPreviousReturnEntryId
            if (toEntryId !== lockedEntryId)
                returnEntryId = toEntryId
            else if (fromEntryId !== lockedEntryId)
                returnEntryId = fromEntryId
            setPreviousState(lockedEntryId, returnEntryId, true, viewport)
        } else {
            setPreviousState(fromEntryId, "", false, viewport)
        }
    }

    function emitNavigation(index, preservePendingViewport) {
        if (!session || index < 0)
            return false
        const entryId = session.entryIdAt(index)
        const sourceIndex = session.sourceIndexAt(index)
        if (entryId === "" || sourceIndex < 0)
            return false
        recordPresentedTransition(presentedEntryId, entryId,
                                  preservePendingViewport === true)
        setPresentedIndex(index, true)
        navigationRequested(entryId, sourceIndex)
        return true
    }

    function adjacentIndex(fromIndex, direction) {
        if (!session || fromIndex < 0)
            return fromIndex
        return session.adjacentImageIndex(fromIndex, direction)
    }

    function navigate(direction) {
        const target = adjacentIndex(presentedIndex, direction)
        if (target === presentedIndex || target < 0)
            return false
        return emitNavigation(target)
    }

    function togglePreviousImageLock(index) {
        if (!session || index < 0)
            return
        const currentEntryId = entryIdAt(index)
        if (currentEntryId === "")
            return
        const previousEntryId = session.viewerPreviousEntryId
        if (session.viewerPreviousLocked) {
            if (previousEntryId === currentEntryId) {
                setPreviousState(session.viewerPreviousReturnEntryId,
                                 "", false, ({}))
            } else {
                setPreviousState(currentEntryId, previousEntryId,
                                 true, ({}))
            }
            return
        }

        const returnEntryId = previousEntryId !== currentEntryId
                ? previousEntryId : ""
        setPreviousState(currentEntryId, returnEntryId, true, ({}))
    }

    function switchToPreviousImage() {
        if (!session || presentedIndex < 0)
            return false
        const previousEntryId = session.viewerPreviousEntryId
        const previousIndex = indexForEntryId(previousEntryId)
        if (previousIndex < 0)
            return false

        let targetIndex = previousIndex
        if (session.viewerPreviousLocked
                && presentedEntryId === previousEntryId) {
            targetIndex = indexForEntryId(
                        session.viewerPreviousReturnEntryId)
            if (targetIndex < 0)
                return false
        }

        rememberViewportForPreviousImageSwitch(targetIndex)
        return emitNavigation(targetIndex, true)
    }

    function togglePreviousImage() {
        if (!session || presentedIndex < 0)
            return false
        if (indexForEntryId(session.viewerPreviousEntryId) >= 0)
            return switchToPreviousImage()

        // ViewerMode's first tilde chooses the next image when possible, then
        // falls back to the previous image at the end of the catalog.
        let target = adjacentIndex(presentedIndex, 1)
        if (target === presentedIndex || target < 0)
            target = adjacentIndex(presentedIndex, -1)
        if (target === presentedIndex || target < 0)
            return false
        return emitNavigation(target)
    }

    function navigateToEnd(direction) {
        let target = presentedIndex
        let next = target
        do {
            target = next
            next = adjacentIndex(target, direction)
        } while (next >= 0 && next !== target)
        if (target === presentedIndex)
            return false
        return emitNavigation(target)
    }

    function requestCurrentSelection(mode) {
        const entryId = entryIdAt(presentedIndex)
        if (entryId === "")
            return false
        selectionRequested(mode, [entryId])
        return true
    }

    function beginShiftSelection() {
        if (shiftSelectionActive || !session || presentedIndex < 0)
            return
        shiftSelectionActive = true
        shiftSelectionAnchorIndex = presentedIndex
        shiftSelectionTargetIndex = presentedIndex
        shiftSelectionAdds = typeof session.isSelectedAt === "function"
                ? !session.isSelectedAt(presentedIndex) : true
    }

    function updateShiftNavigationSelection(targetIndex) {
        beginShiftSelection()
        if (shiftSelectionActive && targetIndex >= 0)
            shiftSelectionTargetIndex = targetIndex
    }

    function finishShiftSelection() {
        if (!shiftSelectionActive)
            return
        const anchor = shiftSelectionAnchorIndex
        const target = shiftSelectionTargetIndex
        shiftSelectionActive = false
        shiftSelectionAnchorIndex = -1
        shiftSelectionTargetIndex = -1
        if (!session || anchor < 0 || target < 0 || anchor === target)
            return
        const first = Math.min(anchor, target)
        const last = Math.max(anchor, target)
        const entryIds = []
        for (let index = first; index <= last; ++index) {
            const entryId = entryIdAt(index)
            if (entryId !== "")
                entryIds.push(entryId)
        }
        if (entryIds.length > 0)
            selectionRequested(shiftSelectionAdds ? "add" : "remove",
                               entryIds)
    }

    function cancelShiftSelection() {
        shiftSelectionActive = false
        shiftSelectionAnchorIndex = -1
        shiftSelectionTargetIndex = -1
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
        if (!sourcePanel)
            return
        sourcePanel.viewerTransitionActive = active
        sourcePanel.viewerTransitionEntryId = active
                ? entryIdAt(presentedIndex) : ""
    }

    function captureTransitionTarget() {
        transitionSourceGeometry = Qt.rect(0, 0, 0, 0)
        transitionThumbnailSource = ""
        transitionHasGeometry = false
        // The presentation can optimistically show a committed swipe while
        // f4 validates the stable-ID cursor action.  Until that authoritative
        // cursor arrives, the panel still points at the old tile; fading is
        // safer than collapsing the new image into the wrong entry.
        if (session && presentedIndex !== session.currentIndex)
            return false
        if (!sourcePanel
                || typeof sourcePanel.currentItemImageGeometry !== "function"
                || typeof sourcePanel.currentItemImageSource !== "function")
            return false
        const geometry = sourcePanel.currentItemImageGeometry(root)
        const source = sourcePanel.currentItemImageSource()
        if (!validGeometry(geometry) || source.toString() === "")
            return false
        transitionSourceGeometry = geometry
        transitionThumbnailSource = source
        transitionHasGeometry = true
        return true
    }

    function beginOpen() {
        if (customContent)
            return
        if (session && presentedIndex < 0)
            presentedIndex = session.currentIndex
        refreshCurrentSource()
        requestImage()
        viewerContentVisible = true
        completingClose = false
        returningFromPinch = false
        pinchCloseActive = false
        pinchCloseFinishingCommit = false
        pinchCloseProgress = 0
        captureTransitionTarget()
        if (currentSourceValue.toString() === ""
                && transitionThumbnailSource.toString() !== "") {
            flickableArea.setImage(transitionThumbnailSource,
                                   currentOriginalSizeValue,
                                   presentedIndex, 0)
            currentSourceValue = transitionThumbnailSource
            currentSourceLevelValue = 0
            appliedPresentedIndex = presentedIndex
        }
        setPanelTransition(true)
        transitionProgress = 0
        transitionAnimation.to = 1
        transitionAnimation.duration = animationDuration
        transitionFinalizeTimer.start()
        transitionAnimation.restart()
        if (autoFocus)
            forceActiveFocus()
    }

    function finishOpen() {
        transitionFinalizeTimer.stop()
        transitionProgress = 1
        setPanelTransition(false)
        transitionThumbnailSource = ""
        transitionHasGeometry = false
        flickableArea.zoomToFit(true)
    }

    function finishClose() {
        transitionFinalizeTimer.stop()
        pinchCloseFinalizeTimer.stop()
        transitionProgress = 0
        viewerContentVisible = false
        completingClose = false
        returningFromPinch = false
        pinchCloseActive = false
        pinchCloseFinishingCommit = false
        pinchCloseProgress = 0
        clearHeldKeys()
        setPanelTransition(false)
        if (session
                && typeof session.clearViewerPreviousState === "function")
            session.clearViewerPreviousState(false)
        closeCompleted()
        closeRequested()
    }

    function requestClose() {
        if (customContent || completingClose)
            return
        finishViewerNavigationAnimationNow()
        completingClose = true
        returningFromPinch = false
        pinchCloseActive = false
        pinchCloseFinishingCommit = false
        pinchCloseProgressAnimation.stop()
        pinchCloseFinalizeTimer.stop()
        clearHeldKeys()
        captureTransitionTarget()
        setPanelTransition(true)
        if (!flickableArea.zoomFitView)
            flickableArea.zoomToFit(true)
        transitionAnimation.to = 0
        transitionAnimation.duration = animationDuration
        transitionFinalizeTimer.start()
        transitionAnimation.restart()
    }

    function closeViewer() { requestClose() }

    function currentViewerImageGeometry() {
        const image = flickableArea.image
        if (image.width <= 1 || image.height <= 1)
            return Qt.rect(0, 0, 0, 0)
        return root.mapFromItem(flickableArea,
                                Qt.rect(image.x, image.y,
                                        image.width, image.height))
    }

    function beginPinchClose() {
        if (pinchCloseActive)
            return true
        if (customContent || completingClose || transitionProgress < 0.999)
            return false
        const startGeometry = currentViewerImageGeometry()
        if (!captureTransitionTarget())
            return false
        const targetGeometry = flickableArea.imageRectFittedInRect(
                    transitionSourceGeometry)
        if (!validGeometry(startGeometry) || !validGeometry(targetGeometry))
            return false
        transitionFinalizeTimer.stop()
        transitionAnimation.stop()
        pinchCloseProgressAnimation.stop()
        pinchCloseFinalizeTimer.stop()
        pinchCloseStartGeometry = startGeometry
        pinchCloseTargetGeometry = targetGeometry
        pinchCloseProgress = 0
        pinchCloseFinishingCommit = false
        pinchCloseActive = true
        setPanelTransition(true)
        flickableArea.setImageRect(startGeometry)
        return true
    }

    function cancelPinchCloseDuringGesture() {
        pinchCloseProgressAnimation.stop()
        pinchCloseFinalizeTimer.stop()
        pinchCloseActive = false
        pinchCloseFinishingCommit = false
        pinchCloseProgress = 0
        setPanelTransition(false)
    }

    function completePinchCloseReturn() {
        pinchCloseProgressAnimation.stop()
        pinchCloseFinalizeTimer.stop()
        pinchCloseActive = false
        pinchCloseFinishingCommit = false
        pinchCloseProgress = 0
        setPanelTransition(false)
        flickableArea.zoomToFit()
    }

    function applyPinchCloseProgress() {
        if (!pinchCloseActive || !validGeometry(pinchCloseStartGeometry)
                || !validGeometry(pinchCloseTargetGeometry))
            return
        // This is ViewerMode's original OutSine geometry interpolation.  The
        // viewport stays full-size; only the image rectangle moves.  Keeping
        // those two layers separate avoids the frame-resize jump/flicker.
        const eased = Math.sin(Math.max(0, Math.min(1, pinchCloseProgress))
                               * Math.PI / 2)
        flickableArea.setImageRect(Qt.rect(
            lerp(pinchCloseStartGeometry.x, pinchCloseTargetGeometry.x, eased),
            lerp(pinchCloseStartGeometry.y, pinchCloseTargetGeometry.y, eased),
            lerp(pinchCloseStartGeometry.width,
                 pinchCloseTargetGeometry.width, eased),
            lerp(pinchCloseStartGeometry.height,
                 pinchCloseTargetGeometry.height, eased)))
    }

    function updatePinchClose(progress) {
        if (customContent || completingClose || pinchCloseFinishingCommit)
            return
        const clamped = Math.max(0, Math.min(1, progress))
        if (clamped <= 0) {
            if (pinchCloseActive)
                cancelPinchCloseDuringGesture()
            return
        }
        if (!beginPinchClose())
            return
        pinchCloseProgressAnimation.stop()
        pinchCloseFinalizeTimer.stop()
        pinchCloseProgress = clamped
    }

    function finishPinchClose(commit) {
        if (!pinchCloseActive)
            return
        const targetGeometry = flickableArea.imageRectFittedInRect(
                    transitionSourceGeometry)
        if (commit && validGeometry(targetGeometry)) {
            pinchCloseTargetGeometry = targetGeometry
            pinchCloseFinishingCommit = true
            if (pinchCloseProgress >= 0.999) {
                pinchCloseProgress = 1
                completePinchCloseCommit()
                return
            }
            pinchCloseProgressAnimation.to = 1
            pinchCloseProgressAnimation.duration = animationDuration
            pinchCloseProgressAnimation.restart()
            pinchCloseFinalizeTimer.start()
            return
        }
        completePinchCloseReturn()
    }

    function completePinchCloseCommit() {
        pinchCloseProgressAnimation.stop()
        pinchCloseFinalizeTimer.stop()
        completingClose = true
        pinchCloseActive = false
        pinchCloseFinishingCommit = false
        transitionProgress = 0
        finishClose()
    }

    function completeTransition() {
        // A Qt Quick animation can be suspended with its window or interrupted
        // by a render-loop/exposure change. Never leave the reusable viewer at
        // a fractional frame: the panel would stay masked on open, and an
        // embedder would never receive closeCompleted() on close. The timer is
        // a terminal-state guard only; the normal NumberAnimation path still
        // supplies every visible frame and reaches this function first.
        transitionFinalizeTimer.stop()
        transitionAnimation.stop()
        if (completingClose) {
            transitionProgress = 0
            finishClose()
        } else if (returningFromPinch) {
            returningFromPinch = false
            setPanelTransition(false)
            finishOpen()
        } else {
            transitionProgress = 1
            finishOpen()
        }
    }

    function resetViewerNavigation() {
        viewerNavigationFinishTimer.stop()
        viewerNavigationOffsetAnimation.stop()
        viewerNavigationOffsetX = 0
        viewerNavigationOverdrag = 0
        viewerNavigationVelocityX = 0
        viewerNavigationLastTime = 0
        viewerNavigationActive = false
        viewerNavigationRevealed = false
        viewerNavigationCommitAfterAnimation = false
        viewerNavigationDirection = 0
        viewerNavigationTargetIndex = -1
        viewerNavigationTargetSource = ""
        viewerNavigationTargetSourceLevel = -1
        viewerNavigationTargetOriginalSize = Qt.size(0, 0)
    }

    function beginViewerNavigationGesture(forceNew, hasPhase) {
        if (hasPhase)
            viewerNavigationGestureEndTimer.stop()
        if (forceNew || !viewerNavigationGestureActive) {
            viewerNavigationResidualQuietTimer.stop()
            viewerNavigationGestureActive = true
            viewerNavigationGestureCommitted = false
            viewerNavigationSuppressMomentum = false
            viewerNavigationLastTime = 0
        }
        viewerNavigationGestureHasPhase = hasPhase
    }

    function continueViewerNavigationGesture(hasPhase) {
        beginViewerNavigationGesture(false, hasPhase)
        if (!hasPhase)
            viewerNavigationGestureEndTimer.restart()
    }

    function endViewerNavigationGesture(clearCommitted) {
        viewerNavigationGestureEndTimer.stop()
        viewerNavigationGestureActive = false
        viewerNavigationGestureHasPhase = false
        if (clearCommitted === undefined || clearCommitted) {
            viewerNavigationGestureCommitted = false
            viewerNavigationSuppressMomentum = false
            viewerNavigationResidualQuietTimer.stop()
        }
        viewerNavigationLastTime = 0
    }

    function startViewerNavigationResidualSuppression() {
        viewerNavigationSuppressMomentum = true
        viewerNavigationResidualQuietTimer.restart()
    }

    function clearViewerNavigationResidualSuppression() {
        if (!viewerNavigationSuppressMomentum)
            return
        if (viewerNavigationOffsetAnimation.running
                || viewerNavigationCommitAfterAnimation) {
            viewerNavigationResidualQuietTimer.restart()
            return
        }
        viewerNavigationSuppressMomentum = false
        if (!viewerNavigationGestureActive)
            viewerNavigationGestureCommitted = false
    }

    function hiddenNavigationOffset(overdrag) {
        return Math.min(overdrag * 0.35,
                        viewerNavigationOverdragThreshold * 0.5)
    }

    function beginViewerNavigation(direction) {
        const target = adjacentIndex(presentedIndex, direction)
        viewerNavigationActive = true
        viewerNavigationDirection = direction
        viewerNavigationTargetIndex = target !== presentedIndex ? target : -1
        viewerNavigationTargetSource = ""
        viewerNavigationTargetSourceLevel = -1
        flickableArea.cancelWheelPan()
        if (viewerNavigationTargetIndex >= 0) {
            if (!flickableArea.zoomFitView) {
                if (viewerNavigationTargetIndex === session.currentIndex)
                    session.requestViewer(0, 0)
                else
                    session.requestViewerAt(viewerNavigationTargetIndex, 0, 0)
            } else {
                requestIndex(viewerNavigationTargetIndex, 1)
            }
        }
        // requestViewerAt records the requested current geometry synchronously.
        // Read tiers only afterwards so an old native/resize plan can never
        // seed the transition surface.
        refreshNeighborSource()
    }

    function applyViewerNavigationDelta(deltaX) {
        if (Math.abs(deltaX) < 0.1)
            return
        const direction = deltaX < 0 ? 1 : -1
        if (!viewerNavigationActive
                || (viewerNavigationDirection !== direction
                    && !viewerNavigationRevealed)) {
            resetViewerNavigation()
            beginViewerNavigation(direction)
        }
        const signedDelta = -viewerNavigationDirection * deltaX
        viewerNavigationOverdrag = Math.max(
                    0, viewerNavigationOverdrag + signedDelta)
        if (viewerNavigationOverdrag <= 0.1) {
            resetViewerNavigation()
            return
        }
        if (viewerNavigationTargetIndex === -1) {
            viewerNavigationRevealed = false
            viewerNavigationOffsetX = -viewerNavigationDirection
                    * Math.min(viewerNavigationOverdrag * 0.25,
                               viewerNavigationOverdragThreshold * 0.6)
            return
        }
        if (viewerNavigationOverdrag < viewerNavigationOverdragThreshold) {
            viewerNavigationRevealed = false
            viewerNavigationOffsetX = viewerNavigationDirection < 0
                    ? viewerNavigationOverdrag
                    : -viewerNavigationDirection
                      * hiddenNavigationOffset(viewerNavigationOverdrag)
            return
        }
        viewerNavigationRevealed = true
        const visibleDistance = viewerNavigationDirection < 0
                ? viewerNavigationOverdrag
                : hiddenNavigationOffset(viewerNavigationOverdragThreshold)
                  + viewerNavigationOverdrag
                  - viewerNavigationOverdragThreshold
        const maximumOffset = viewerNavigationDirection < 0
                ? viewerNavigationTargetTravelDistance : width
        viewerNavigationOffsetX = -viewerNavigationDirection
                * Math.min(visibleDistance, maximumOffset)
    }

    function finishViewerNavigation() {
        viewerNavigationFinishTimer.stop()
        if (!viewerNavigationActive) {
            flickableArea.finishWheelPan()
            return
        }
        const signedVelocity = -viewerNavigationDirection
                * viewerNavigationVelocityX
        const shouldCommit = viewerNavigationTargetIndex !== -1
                && viewerNavigationRevealed
                && (viewerNavigationOverdrag
                    >= viewerNavigationCommitThreshold
                    || signedVelocity > 900)
        if (shouldCommit)
            viewerNavigationGestureCommitted = true
        const targetOffset = shouldCommit
                ? (viewerNavigationDirection < 0
                   ? viewerNavigationTargetTravelDistance
                   : -viewerNavigationDirection * width) : 0
        const fullDistance = viewerNavigationDirection < 0
                ? viewerNavigationTargetTravelDistance : width
        const remainingRatio = Math.min(
                    1, Math.abs(targetOffset - viewerNavigationOffsetX)
                       / Math.max(1, fullDistance))
        viewerNavigationCommitAfterAnimation = shouldCommit
        viewerNavigationOffsetAnimation.to = targetOffset
        viewerNavigationOffsetAnimation.duration = shouldCommit
                ? animationDuration * (1 + remainingRatio)
                : animationDuration
        viewerNavigationOffsetAnimation.restart()
    }

    function commitViewerNavigation() {
        const target = viewerNavigationTargetIndex
        const preserveViewport = viewerNavigationTargetKeepsZoom
        const targetScale = viewerNavigationTargetScale
        const targetX = viewerNavigationTargetFinalImageX
        const targetY = viewerNavigationTargetFinalImageY
        const targetSource = viewerNavigationTargetSource
        const targetSourceLevel = viewerNavigationTargetSourceLevel
        // QML value-type reads may retain a live wrapper for the source
        // property. resetViewerNavigation() clears that property below, so
        // take an independent size value for the prepared-frame handoff.
        const targetOriginalSize = Qt.size(
                    viewerNavigationTargetOriginalSize.width,
                    viewerNavigationTargetOriginalSize.height)
        const canAdoptFitTransition = flickableArea.zoomFitView
                && !sphericViewerMode && targetSourceLevel === 1
                && targetSource.toString() !== ""
                && targetOriginalSize.width > 1
                && targetOriginalSize.height > 1
                && viewerNavigationNeighborImage.status === Image.Ready
        viewerNavigationGestureCommitted = true
        startViewerNavigationResidualSuppression()
        resetViewerNavigation()
        if (target >= 0 && target !== presentedIndex) {
            if (canAdoptFitTransition) {
                // The prepared frame already visible at the end of the swipe
                // can become FlickableZoomable's base tier before the authority
                // round-trip. This prevents setPresentedIndex() from briefly
                // installing the target's masonry thumbnail at commit time.
                flickableArea.setImage(targetSource, targetOriginalSize,
                                       target, targetSourceLevel)
            }
            if (preserveViewport) {
                pendingCommittedViewport = {
                    index: target,
                    scale: targetScale,
                    x: targetX,
                    y: targetY
                }
                pendingCommittedViewportAttempts = 0
            }
            emitNavigation(target)
            if (preserveViewport)
                committedViewportTimer.restart()
        }
    }

    function applyPendingCommittedViewport() {
        if (!pendingCommittedViewport
                || pendingCommittedViewport.index !== presentedIndex) {
            pendingCommittedViewport = null
            return
        }
        if (flickableArea.effectiveOriginalSize.width <= 1
                || flickableArea.effectiveOriginalSize.height <= 1) {
            if (++pendingCommittedViewportAttempts < 40)
                committedViewportTimer.restart()
            else
                pendingCommittedViewport = null
            return
        }
        flickableArea.setViewport(pendingCommittedViewport.scale,
                                  pendingCommittedViewport.x,
                                  pendingCommittedViewport.y)
        pendingCommittedViewport = null
        decodeRequestTimer.start()
    }

    function finishViewerNavigationAnimationNow() {
        if (!viewerNavigationOffsetAnimation.running
                && !viewerNavigationCommitAfterAnimation)
            return
        viewerNavigationOffsetAnimation.stop()
        if (viewerNavigationCommitAfterAnimation)
            commitViewerNavigation()
        else {
            resetViewerNavigation()
            flickableArea.settlePan()
        }
    }

    function wheelDeltaPixels(pixelDelta, angleDelta) {
        return pixelDelta !== 0 ? pixelDelta : angleDelta / 120 * 80
    }

    function isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY,
                                      angleDeltaX, angleDeltaY, phase,
                                      hasPixelDelta, nativeMomentum,
                                      nativePhase, nativeMomentumPhase) {
        if (angleDeltaY === 0 || angleDeltaX !== 0 || nativeMomentum)
            return false
        if (!hasPixelDelta)
            return true
        const phaseFree = phase === ViewerWheelArea.NoScrollPhase
                && nativePhase === 0 && nativeMomentumPhase === 0
        return phaseFree && Math.abs(pixelDeltaY) > 0
                && Math.abs(pixelDeltaX) < Math.abs(pixelDeltaY) * 0.2
    }

    function panZoomedImageFromWheel(deltaX, deltaY, recordVelocity) {
        if (flickableArea.zoomFitView)
            return Qt.point(deltaX, deltaY)
        viewerWheelPanFinishTimer.stop()
        return flickableArea.panBy(deltaX, deltaY, recordVelocity)
    }

    function scheduleWheelPanFallbackFinish() {
        if (!flickableArea.zoomFitView)
            viewerWheelPanFinishTimer.restart()
    }

    function handleViewerWheel(pixelDeltaX, pixelDeltaY,
                               angleDeltaX, angleDeltaY, phase, modifiers,
                               buttons, hasPixelDelta, inverted, source,
                               deviceType, nativeMomentum, nativePhase,
                               nativeMomentumPhase) {
        const phaseAware = phase !== ViewerWheelArea.NoScrollPhase
        const effectivePhase = nativeMomentum
                && phase === ViewerWheelArea.ScrollBegin
                ? ViewerWheelArea.ScrollMomentum : phase
        let deltaX = wheelDeltaPixels(pixelDeltaX, angleDeltaX)
        const deltaY = wheelDeltaPixels(pixelDeltaY, angleDeltaY)

        if (nativeMomentum && !flickableArea.zoomFitView
                && !viewerNavigationActive
                && !viewerNavigationCommitAfterAnimation
                && !viewerNavigationGestureCommitted
                && !viewerNavigationSuppressMomentum) {
            panZoomedImageFromWheel(deltaX, deltaY, false)
            return
        }

        if (viewerNavigationSuppressMomentum
                && !viewerNavigationGestureActive) {
            const physicalRestart = !nativeMomentum
                    && effectivePhase !== ViewerWheelArea.ScrollMomentum
                    && effectivePhase !== ViewerWheelArea.ScrollEnd
            if (physicalRestart)
                clearViewerNavigationResidualSuppression()
            else {
                viewerNavigationResidualQuietTimer.restart()
                return
            }
        }

        if (nativeMomentum && !viewerNavigationGestureActive) {
            startViewerNavigationResidualSuppression()
            return
        }

        if (nativeMomentum && viewerNavigationGestureActive) {
            if (viewerNavigationActive) {
                finishViewerNavigation()
                endViewerNavigationGesture(false)
                startViewerNavigationResidualSuppression()
                return
            }
            if (!flickableArea.zoomFitView) {
                endViewerNavigationGesture(true)
                panZoomedImageFromWheel(deltaX, deltaY, false)
                return
            }
            endViewerNavigationGesture(false)
            startViewerNavigationResidualSuppression()
            return
        }

        if (effectivePhase === ViewerWheelArea.ScrollBegin) {
            finishViewerNavigationAnimationNow()
            beginViewerNavigationGesture(true, true)
            if (!flickableArea.zoomFitView)
                flickableArea.beginWheelPan()
            viewerNavigationFinishTimer.stop()
            viewerWheelPanFinishTimer.stop()
            return
        }

        if (effectivePhase === ViewerWheelArea.ScrollEnd) {
            if (!viewerNavigationGestureActive)
                return
            const hadNavigation = viewerNavigationActive
            if (hadNavigation)
                finishViewerNavigation()
            else if (!flickableArea.zoomFitView)
                scheduleWheelPanFallbackFinish()
            endViewerNavigationGesture(false)
            if (hadNavigation || flickableArea.zoomFitView)
                startViewerNavigationResidualSuppression()
            return
        }

        if (effectivePhase === ViewerWheelArea.ScrollMomentum
                && (viewerNavigationSuppressMomentum
                    || !viewerNavigationGestureActive)) {
            startViewerNavigationResidualSuppression()
            return
        }

        if (viewerNavigationCommitAfterAnimation)
            return

        if (isLegacyWheelImageSwitch(
                    pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                    phase, hasPixelDelta, nativeMomentum, nativePhase,
                    nativeMomentumPhase)) {
            finishViewerNavigationAnimationNow()
            endViewerNavigationGesture(true)
            flickableArea.cancelWheelPan()
            navigate(angleDeltaY < 0 ? 1 : -1)
            return
        }

        continueViewerNavigationGesture(phaseAware)
        if (viewerNavigationGestureCommitted)
            return

        const horizontalIntent = viewerNavigationActive
                || Math.abs(deltaX) >= Math.abs(deltaY) * 0.6
        if (!horizontalIntent) {
            if (!flickableArea.zoomFitView) {
                panZoomedImageFromWheel(0, deltaY, true)
                if (!phaseAware)
                    scheduleWheelPanFallbackFinish()
            }
            return
        }

        // A zoomed image consumes horizontal scrolling until its pan boundary;
        // only the unconsumed remainder starts the neighboring-image reveal.
        if (!viewerNavigationActive && !flickableArea.zoomFitView)
            deltaX = panZoomedImageFromWheel(deltaX, deltaY, true).x
        if (Math.abs(deltaX) < 0.1) {
            if (!phaseAware)
                scheduleWheelPanFallbackFinish()
            return
        }

        const now = Date.now()
        const elapsed = viewerNavigationLastTime
                ? Math.max(1, now - viewerNavigationLastTime) : 16
        viewerNavigationVelocityX = deltaX / elapsed * 1000
        viewerNavigationLastTime = now
        applyViewerNavigationDelta(deltaX)
        if (!phaseAware)
            viewerNavigationFinishTimer.restart()
    }

    Rectangle {
        id: viewerBackground
        objectName: "galleryViewerBackground"
        anchors.fill: parent
        color: root.backgroundColor
        // ViewerMode fades this surface against the thumbnails surface
        // underneath. Embedded hosts may deliberately supply a transparent
        // background so the shared window material remains visible.
        opacity: root.surfaceProgress
        visible: !root.customContent && opacity > 0
    }

    Item {
        id: transitionFrame
        objectName: "galleryViewerTransitionFrame"
        visible: !root.customContent && root.viewerContentVisible
        clip: true
        anchors.fill: parent
        opacity: root.transitionHasGeometry ? 1 : root.transitionProgress

        Item {
            id: currentImageContainer
            anchors.fill: parent
            opacity: root.viewerNavigationCurrentOpacity
            transform: Translate {
                x: root.viewerNavigationCurrentOffsetX
            }

            FlickableZoomable {
                id: flickableArea
                objectName: "galleryViewerViewport"
                // This is the original ViewerMode transition: animate the
                // actual FlickableZoomable container between the masonry tile
                // and the full viewer.  Pinch close leaves the container full
                // size and moves that same viewer image with setImageRect().
                x: root.pinchCloseActive ? 0
                   : root.transitionHasGeometry
                     ? root.lerp(root.transitionSourceGeometry.x, 0,
                                 root.transitionProgress) : 0
                y: root.pinchCloseActive ? 0
                   : root.transitionHasGeometry
                     ? root.lerp(root.transitionSourceGeometry.y, 0,
                                 root.transitionProgress) : 0
                width: root.pinchCloseActive ? root.width
                       : root.transitionHasGeometry
                         ? root.lerp(root.transitionSourceGeometry.width,
                                     root.width, root.transitionProgress)
                         : root.width
                height: root.pinchCloseActive ? root.height
                        : root.transitionHasGeometry
                          ? root.lerp(root.transitionSourceGeometry.height,
                                      root.height, root.transitionProgress)
                          : root.height
                active: !root.customContent && !root.completingClose
                animationDuration: root.animationDuration
                devicePixelRatio: root.devicePixelRatio
                topInset: 0
                checkerboardEnabled: true
                scrollBarTheme: root.theme
                pinchZoomEnabled: !root.sphericViewerMode
                hideVerticalScrollBar: root.viewerNavigationActive
                                       || viewerNavigationOffsetAnimation.running
                onZoomScaleChanged: decodeRequestTimer.start()
                onCloseRequested: root.requestClose()
                onMiddleClickRequested: root.fullscreenToggleRequested()
                onPinchZoomOutToThumbnailsProgressed:
                    progress => root.updatePinchClose(progress)
                onPinchZoomOutToThumbnailsFinished:
                    commit => root.finishPinchClose(commit)
            }

            // This is the same windowless panorama surface used by the
            // standalone ViewerMode.  It consumes FlickableZoomable's already
            // decoded texture instead of opening or decoding the file again.
            // Keeping it in the transitioning image container also preserves
            // the image-to-tile open/close geometry.
            Loader {
                id: sphericViewerLoader
                objectName: "gallerySphericViewerLoader"
                anchors.fill: flickableArea
                active: root.sphericViewerMode
                // ViewerMode fades the panorama while the same image geometry
                // contracts back into its tile.  The fallback close already
                // fades transitionFrame, so avoid multiplying opacity there.
                opacity: root.transitionHasGeometry
                         ? root.transitionProgress : 1

                sourceComponent: Component {
                    SphericViewer {
                        objectName: "gallerySphericViewer"
                        source: flickableArea.textureSource
                        originalSize: flickableArea.originalSize
                        easingType: Easing.OutSine

                        onCloseRequested: root.requestClose()
                        onSphereScrollingMouseCursorRequested:
                            (set, idle, rotation) =>
                                root.sphereScrollingMouseCursorRequested(
                                    set, idle, rotation)
                    }
                }
            }
        }

        Item {
            id: viewerNavigationNeighbor
            anchors.fill: parent
            z: root.viewerNavigationDirection < 0 ? 1 : -1
            opacity: root.viewerNavigationTargetOpacity
            visible: opacity > 0 && root.viewerNavigationActive
                     && root.viewerNavigationTargetIndex !== -1
                     && root.viewerNavigationTargetSource.toString() !== ""

            Item {
                id: viewerNavigationNeighborEffectiveBounds
                x: root.viewerNavigationTargetImageX
                y: root.viewerNavigationTargetImageY
                width: root.viewerNavigationTargetDisplayWidth
                height: root.viewerNavigationTargetDisplayHeight

                Item {
                    id: viewerNavigationNeighborUnrotatedContent
                    x: (parent.width - width) / 2
                    y: (parent.height - height) / 2
                    width: root.viewerNavigationTargetHasSize
                           ? root.viewerNavigationTargetDisplayOriginalSize.width
                             * root.viewerNavigationTargetScale : parent.width
                    height: root.viewerNavigationTargetHasSize
                            ? root.viewerNavigationTargetDisplayOriginalSize.height
                              * root.viewerNavigationTargetScale : parent.height
                    rotation: flickableArea.rotationMode * 90

                    Image {
                        id: viewerNavigationNeighborImage
                        objectName: "galleryViewerNavigationNeighborImage"
                        anchors.fill: parent
                        source: root.viewerNavigationTargetSource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        visible: false
                        mipmap: root.viewerNavigationTargetSourceLevel === 2
                    }

                    ShaderEffect {
                        objectName: "galleryViewerNavigationNeighborShader"
                        anchors.fill: parent

                        property var source: viewerNavigationNeighborImage
                        property var viewportSize: Qt.size(
                                                   width * root.devicePixelRatio,
                                                   height * root.devicePixelRatio)
                        property real sharpenAmount:
                            root.viewerNavigationTargetScale < 1 ? 1.5 : 0
                        property bool showCheckerboard:
                            flickableArea.checkerboardEnabled
                            && viewerNavigationNeighborImage.status === Image.Ready
                        property int checkerboardSize:
                            4 * root.devicePixelRatio
                        property int borderRadius: 0

                        fragmentShader: "qrc:/ZoinGallery/resources/shader.frag.qsb"
                    }
                }
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: !root.customContent
                 && root.currentSourceValue.toString() === ""
        visible: running && root.transitionProgress > 0.5
    }

    Label {
        anchors.centerIn: parent
        visible: !root.customContent && root.session
                 && root.presentedIndex < 0
        text: qsTr("Unable to load image")
        color: root.foregroundColor
    }

    ViewerWheelArea {
        anchors.fill: parent
        enabled: !root.customContent && !root.completingClose
        z: 3
        onWheelReceived:
            (pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase,
             modifiers, buttons, hasPixelDelta, inverted, source, deviceType,
             nativeMomentum, nativePhase, nativeMomentumPhase) => {
                root.handleViewerWheel(
                            pixelDeltaX, pixelDeltaY,
                            angleDeltaX, angleDeltaY, phase, modifiers,
                            buttons, hasPixelDelta, inverted, source,
                            deviceType, nativeMomentum, nativePhase,
                            nativeMomentumPhase)
            }
        onWheelForwarded: root.finishViewerNavigation()
        onZoomWheelReceived:
            (angleDeltaY, modifiers, buttons) => {
                if (root.sphericViewerMode && sphericViewerLoader.item) {
                    sphericViewerLoader.item.handleZoomWheel(
                                angleDeltaY, modifiers, buttons)
                } else {
                    flickableArea.handleZoomWheel(
                                angleDeltaY, modifiers, buttons)
                }
            }
    }

    Keys.onPressed: event => {
        if (!root.ownsKey(event)) {
            event.accepted = false
            return
        }
        const altPressed = Boolean(event.modifiers & Qt.AltModifier)
        const controlModifier = Boolean(event.modifiers & Qt.ControlModifier)
        const shiftModifier = Boolean(event.modifiers & Qt.ShiftModifier)
        let navigated = false

        if (event.key === Qt.Key_Shift && !event.isAutoRepeat) {
            root.beginShiftSelection()
            event.accepted = true
            return
        }
        // Synthetic events and some platform backends attach Shift to the
        // navigation key without delivering a separate Shift press first.
        if (shiftModifier)
            root.beginShiftSelection()

        if (event.key === Qt.Key_Backslash) {
            root.requestCurrentSelection("toggle")
        } else if (event.key === Qt.Key_Insert) {
            root.requestCurrentSelection("add")
        } else if (event.key === Qt.Key_Delete) {
            root.requestCurrentSelection("remove")
        } else if ((!flickableArea.zoomFitView
                    && (event.key === Qt.Key_Left
                        || event.key === Qt.Key_Right
                        || event.key === Qt.Key_Up
                        || event.key === Qt.Key_Down))
                   || event.key === Qt.Key_Plus
                   || event.key === Qt.Key_Equal
                   || event.key === Qt.Key_Minus
                   || event.key === Qt.Key_Control) {
            // This branch deliberately has the same precedence and modifier
            // policy as ViewerMode: modified +/- still controls local zoom,
            // and auto-repeat never drives the continuous animation itself.
            if (event.isAutoRepeat) {
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Left)
                root.leftPressed = true
            else if (event.key === Qt.Key_Right)
                root.rightPressed = true
            else if (event.key === Qt.Key_Up)
                root.upPressed = true
            else if (event.key === Qt.Key_Down)
                root.downPressed = true
            else if (event.key === Qt.Key_Plus
                     || event.key === Qt.Key_Equal)
                root.zoomInPressed = true
            else if (event.key === Qt.Key_Minus)
                root.zoomOutPressed = true
            else if (event.key === Qt.Key_Control)
                root.controlPressed = true
            root.updateHeldKeyMotion()
        } else if ((event.key === Qt.Key_Left
                    || event.key === Qt.Key_PageUp
                    || event.key === Qt.Key_Backspace
                    || event.key === Qt.Key_Up) && !altPressed) {
            navigated = root.navigate(-1)
        } else if ((event.key === Qt.Key_Right
                    || event.key === Qt.Key_PageDown
                    || event.key === Qt.Key_Space
                    || event.key === Qt.Key_Down) && !altPressed) {
            navigated = root.navigate(1)
        } else if (event.key === Qt.Key_Home) {
            navigated = root.navigateToEnd(-1)
        } else if (event.key === Qt.Key_End) {
            navigated = root.navigateToEnd(1)
        } else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F
                   || event.key === Qt.Key_Clear
                   || ((event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) && altPressed)) {
            root.fullscreenToggleRequested()
        } else if (event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Return
                   || event.key === Qt.Key_Escape
                   || (event.key === Qt.Key_Up && altPressed)
                   || (event.key === Qt.Key_PageUp && controlModifier)) {
            root.requestClose()
        } else if (event.key === Qt.Key_Asterisk
                   || event.key === Qt.Key_9) {
            flickableArea.zoomTo100()
        } else if (event.key === Qt.Key_1 && controlModifier) {
            flickableArea.zoomTo100()
        } else if (event.key === Qt.Key_2 && controlModifier) {
            flickableArea.zoomToScale(0.5)
        } else if (event.key === Qt.Key_3 && controlModifier) {
            flickableArea.zoomToScale(0.25)
        } else if (event.key === Qt.Key_0 && controlModifier) {
            flickableArea.zoomToFit()
        } else if (event.key === Qt.Key_Z || event.key === Qt.Key_Slash
                   || event.key === Qt.Key_0) {
            flickableArea.toggleZoomToFit()
        } else if (event.key === Qt.Key_Tab) {
            root.panelsVisible = !root.panelsVisible
        } else if (root.isTildeKey(event)) {
            if (shiftModifier) {
                root.togglePreviousImageLock(root.presentedIndex)
                event.accepted = true
                return
            }
            navigated = root.togglePreviousImage()
        } else if (event.key === Qt.Key_S || event.key === Qt.Key_P) {
            root.sphericViewerMode = !root.sphericViewerMode
        } else if (event.key === Qt.Key_BracketRight) {
            flickableArea.rotate(1)
        } else if (event.key === Qt.Key_BracketLeft) {
            flickableArea.rotate(3)
        }

        if (navigated && shiftModifier)
            root.updateShiftNavigationSelection(root.presentedIndex)
        event.accepted = true
    }

    Keys.onReleased: event => {
        if (root.customContent) {
            event.accepted = false
            return
        }
        if (!root.ownsKey(event)) {
            event.accepted = false
            return
        }
        if (event.isAutoRepeat)
            return
        let heldMotionKey = true
        if (event.key === Qt.Key_Left)
            root.leftPressed = false
        else if (event.key === Qt.Key_Right)
            root.rightPressed = false
        else if (event.key === Qt.Key_Up)
            root.upPressed = false
        else if (event.key === Qt.Key_Down)
            root.downPressed = false
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
            root.zoomInPressed = false
        else if (event.key === Qt.Key_Minus)
            root.zoomOutPressed = false
        else if (event.key === Qt.Key_Control)
            root.controlPressed = false
        else if (event.key === Qt.Key_Shift) {
            root.finishShiftSelection()
            heldMotionKey = false
        }
        else
            heldMotionKey = false
        if (!heldMotionKey) {
            // Unknown and discrete command releases are modal too.  Letting a
            // release escape after its press was consumed can leave f4's key
            // state machine out of sync.
            event.accepted = true
            return
        }
        // ViewerMode only restarts/stops the continuous-motion loop while the
        // viewport is already outside Fit.  startZoomScrollingAnimation(),
        // even with a zero vector, deliberately clears zoomFitView; calling it
        // for a fitted arrow release would therefore turn a normal image
        // navigation key-up into an invisible zoom-state change.
        if (!flickableArea.zoomFitView) {
            root.updateHeldKeyMotion()
            if (event.key === Qt.Key_Control
                    && !root.leftPressed && !root.rightPressed
                    && !root.upPressed && !root.downPressed
                    && !root.zoomInPressed && !root.zoomOutPressed)
                flickableArea.onControlReleased()
        }
        event.accepted = true
    }

    NumberAnimation {
        id: transitionAnimation
        objectName: "galleryViewerTransitionAnimation"
        target: root
        property: "transitionProgress"
        easing.type: Easing.OutSine
        onFinished: root.completeTransition()
    }

    EventLoopTimer {
        id: transitionFinalizeTimer
        objectName: "galleryViewerTransitionFinalizeTimer"
        interval: Math.max(1, root.animationDuration + 75)
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: root.completeTransition()
    }

    EventLoopTimer {
        id: pinchCloseFinalizeTimer
        interval: root.animationDuration + 50
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: {
            if (root.pinchCloseFinishingCommit)
                root.completePinchCloseCommit()
        }
    }

    NumberAnimation {
        id: pinchCloseProgressAnimation
        target: root
        property: "pinchCloseProgress"
        easing.type: Easing.OutSine
        onFinished: {
            if (root.pinchCloseFinishingCommit)
                root.completePinchCloseCommit()
        }
    }

    NumberAnimation {
        id: viewerNavigationOffsetAnimation
        target: root
        property: "viewerNavigationOffsetX"
        easing.type: Easing.OutSine
        onFinished: {
            if (root.viewerNavigationCommitAfterAnimation)
                root.commitViewerNavigation()
            else {
                root.resetViewerNavigation()
                flickableArea.settlePan()
            }
        }
    }

    // Native-tier requests must still be debounced when the Quick animation
    // driver is suspended (for example while an embedded window is being
    // occluded or reparented). The shared runtime registers this QTimer-backed
    // type independently of the render loop.
    EventLoopTimer {
        id: decodeRequestTimer
        interval: 80
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: root.requestImage()
    }
    Timer {
        id: committedViewportTimer
        interval: 5
        onTriggered: root.applyPendingCommittedViewport()
    }
    EventLoopTimer {
        id: previousViewportTimer
        interval: 5
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: root.applyPendingPreviousImageViewport()
    }
    Timer {
        id: authorityTimer
        interval: 1500
        onTriggered: {
            if (root.session
                    && root.session.cursorEntryId !== root.presentedEntryId)
                root.setPresentedIndex(root.session.currentIndex, false)
            root.pendingAuthorityIndex = -1
            root.pendingAuthorityEntryId = ""
        }
    }
    Timer {
        id: viewerNavigationFinishTimer
        interval: 140
        onTriggered: root.finishViewerNavigation()
    }
    Timer {
        id: viewerWheelPanFinishTimer
        interval: 70
        onTriggered: flickableArea.finishWheelPan()
    }
    Timer {
        id: viewerNavigationGestureEndTimer
        interval: 350
        onTriggered: root.endViewerNavigationGesture()
    }
    Timer {
        id: viewerNavigationResidualQuietTimer
        interval: 180
        onTriggered: root.clearViewerNavigationResidualSuppression()
    }

    Connections {
        target: root.session
        ignoreUnknownSignals: true
        function onCurrentIndexChanged() {
            if (!root.session)
                return
            const nextEntryId = root.session.cursorEntryId
            const authorityConfirmed =
                    root.pendingAuthorityEntryId !== ""
                    && root.pendingAuthorityEntryId === nextEntryId
            if (authorityConfirmed) {
                root.pendingAuthorityIndex = -1
                root.pendingAuthorityEntryId = ""
                authorityTimer.stop()
            }
            if (!authorityConfirmed
                    && root.presentedEntryId !== nextEntryId)
                root.recordPresentedTransition(root.presentedEntryId,
                                               nextEntryId, false)
            root.resetViewerNavigation()
            root.setPresentedIndex(root.session.currentIndex, false)
        }
        function onViewerSourceChanged() { root.refreshCurrentSource() }
        function onViewerSourceAtChanged(index) {
            if (index === root.presentedIndex)
                root.refreshCurrentSource()
            if (index === root.viewerNavigationTargetIndex)
                root.refreshNeighborSource()
        }
        function onCatalogRevisionChanged() {
            if (root.session.currentIndex < 0) {
                root.presentedIndex = -1
                root.presentedEntryId = ""
                root.refreshCurrentSource()
                return
            }

            const remappedPresented =
                    root.indexForEntryId(root.presentedEntryId)
            if (remappedPresented < 0) {
                root.setPresentedIndex(root.session.currentIndex, false)
            } else {
                const previousIndex = root.presentedIndex
                if (previousIndex !== remappedPresented) {
                    flickableArea.remapImageIndex(previousIndex,
                                                  remappedPresented)
                    if (root.appliedPresentedIndex === previousIndex)
                        root.appliedPresentedIndex = remappedPresented
                    root.presentedIndex = remappedPresented
                }
                if (root.pendingAuthorityEntryId !== "")
                    root.pendingAuthorityIndex = root.indexForEntryId(
                                root.pendingAuthorityEntryId)
                root.refreshCurrentSource()
                root.requestImage()
            }
        }
        function onViewerPreviousStateChanged() {
            const viewport = root.previousViewport()
            if (viewport.targetEntryId === root.presentedEntryId)
                previousViewportTimer.restart()
        }
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
