pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: controller
    required property Item viewer
    required property FlickableZoomable imageViewport
    required property Item frame
    required property GalleryViewerMotion motion

    property string navigationEntryId: ""
    property string navigationOriginEntryId: ""
    property int navigationDirection: 0

    property var navigationSession: viewer.session
    onNavigationSessionChanged: {
        navigationEntryId = ""
        navigationOriginEntryId = ""
        navigationDirection = 0
    }

    property Connections viewerFailureConnection: Connections {
        target: controller.navigationSession
        ignoreUnknownSignals: true
        function onViewerRequestStateAtChanged(index) {
            if (index === controller.viewer.presentedIndex)
                controller.continueFailedNavigation()
        }
    }

    function continueFailedNavigation() {
        const failedEntryId = navigationEntryId
        if (navigationDirection === 0 || failedEntryId === ""
                || viewer.presentedEntryId !== failedEntryId
                || !viewer.session
                || typeof viewer.session.viewerDecodeFailedAt !== "function"
                || !viewer.session.viewerDecodeFailedAt(viewer.presentedIndex))
            return
        // Failure can arrive synchronously while requestImage is publishing
        // a navigation intent. Finish that intent before advancing again.
        Qt.callLater(function() {
            if (controller.navigationEntryId !== failedEntryId
                    || controller.viewer.presentedEntryId !== failedEntryId)
                return
            const next = controller.adjacentIndex(
                        controller.viewer.presentedIndex,
                        controller.navigationDirection)
            if (next >= 0 && next !== controller.viewer.presentedIndex) {
                controller.emitNavigation(next)
                return
            }
            const origin = controller.indexForEntryId(
                        controller.navigationOriginEntryId)
            controller.navigationDirection = 0
            controller.navigationEntryId = ""
            controller.navigationOriginEntryId = ""
            if (origin >= 0 && origin !== controller.viewer.presentedIndex
                    && !controller.viewer.session.viewerDecodeFailedAt(origin))
                controller.emitNavigation(origin)
            controller.navigationDirection = 0
            controller.navigationEntryId = ""
            controller.navigationOriginEntryId = ""
        })
    }

    property Connections videoPlaybackAvailabilityConnection: Connections {
        target: controller.viewer.session
        ignoreUnknownSignals: true
        function onVideoPlaybackAvailableChanged() {
            controller.refreshCurrentSource(true,
                                            controller.imageViewport.zoomFitView)
        }
    }

    function entryIdAt(index) {
        return viewer.session && index >= 0 ? viewer.session.entryIdAt(index) : ""
    }

    function indexForEntryId(entryId) {
        if (!viewer.session || entryId === "")
            return -1
        if (typeof viewer.session.indexForEntryId === "function")
            return viewer.session.indexForEntryId(entryId)
        return -1
    }

    function previousViewport() {
        return viewer.session && viewer.session.viewerPreviousViewport
                ? viewer.session.viewerPreviousViewport : ({})
    }

    function setPreviousState(previousEntryId, returnEntryId, locked,
                              viewport) {
        if (!viewer.session
                || typeof viewer.session.setViewerPreviousState !== "function")
            return
        viewer.session.setViewerPreviousState(previousEntryId, returnEntryId, locked,
                                       viewport === undefined ? ({})
                                                              : viewport)
    }

    function clearPendingPreviousViewport() {
        if (!viewer.session)
            return
        setPreviousState(viewer.session.viewerPreviousEntryId,
                         viewer.session.viewerPreviousReturnEntryId,
                         viewer.session.viewerPreviousLocked, ({}))
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
                    originalSize.width / viewer.devicePixelRatio,
                    originalSize.height / viewer.devicePixelRatio)
        return imageViewport.rotationMode % 2 === 1
                ? Qt.size(displaySize.height, displaySize.width)
                : displaySize
    }

    function fitScaleForEffectiveSize(size) {
        if (size.width <= 1 || size.height <= 1
                || frame.width <= 1
                || frame.height <= 1)
            return 1
        return size.width / size.height
                <= frame.width / frame.height
                ? frame.height / size.height
                : frame.width / size.width
    }

    function rememberViewportForPreviousImageSwitch(targetIndex) {
        clearPendingPreviousViewport()
        if (!viewer.session || targetIndex < 0 || imageViewport.zoomFitView
                || imageViewport.effectiveOriginalSize.width <= 1
                || imageViewport.effectiveOriginalSize.height <= 1)
            return

        const targetSize = effectiveSizeFromOriginalSize(
                    originalSizeAt(targetIndex))
        if (targetSize.width <= 1 || targetSize.height <= 1)
            return

        const sourceSize = imageViewport.effectiveOriginalSize
        const sourceFitScale = fitScaleForEffectiveSize(sourceSize)
        setPreviousState(viewer.session.viewerPreviousEntryId,
                         viewer.session.viewerPreviousReturnEntryId,
                         viewer.session.viewerPreviousLocked, {
            targetEntryId: entryIdAt(targetIndex),
            centerRatioX: ((frame.width / 2
                            - imageViewport.image.x)
                           / imageViewport.zoomScale) / sourceSize.width,
            centerRatioY: ((frame.height / 2
                            - imageViewport.image.y)
                           / imageViewport.zoomScale) / sourceSize.height,
            zoomToFitRatio: imageViewport.zoomScale / sourceFitScale
        })
        viewer.pendingPreviousViewportAttempts = 0
    }

    function applyPendingPreviousImageViewport() {
        if (!viewer.session)
            return false
        const viewport = previousViewport()
        const targetEntryId = viewport.targetEntryId === undefined
                ? "" : viewport.targetEntryId
        if (targetEntryId === "")
            return false
        if (targetEntryId !== viewer.presentedEntryId) {
            clearPendingPreviousViewport()
            return false
        }
        if (imageViewport.effectiveOriginalSize.width <= 1
                || imageViewport.effectiveOriginalSize.height <= 1) {
            if (++viewer.pendingPreviousViewportAttempts < 40)
                motion.previousViewportTimer.restart()
            return false
        }

        const targetSize = imageViewport.effectiveOriginalSize
        const targetZoom = imageViewport.clampZoomScale(
                    fitScaleForEffectiveSize(targetSize)
                    * Number(viewport.zoomToFitRatio))
        const targetX = frame.width / 2
                - targetSize.width * Number(viewport.centerRatioX)
                  * targetZoom
        const targetY = frame.height / 2
                - targetSize.height * Number(viewport.centerRatioY)
                  * targetZoom
        imageViewport.setViewport(targetZoom, targetX, targetY)
        clearPendingPreviousViewport()
        viewer.pendingPreviousViewportAttempts = 0
        motion.decodeRequestTimer.start()
        return true
    }

    function originalSizeAt(index) {
        if (!viewer.session || index < 0
                || typeof viewer.session.imageOriginalSizeAt !== "function")
            return Qt.size(0, 0)
        return viewer.session.imageOriginalSizeAt(index)
    }

    function sourceTiersAt(index) {
        if (!viewer.session || index < 0)
            return []
        if (typeof viewer.session.viewerSourcesAt === "function")
            return viewer.session.viewerSourcesAt(index)
        const fallback = viewer.session.viewerSourceAt(index)
        return fallback.toString() === "" ? []
                                         : [{ source: fallback, level: 0 }]
    }

    function refreshCurrentSource(forceIndexChange, previousFitMode) {
        if (!viewer.session || viewer.presentedIndex < 0) {
            viewer.currentIsVideoValue = false
            viewer.currentSourceValue = ""
            viewer.currentSourceLevelValue = -1
            viewer.currentSourcesValue = []
            viewer.currentVideoThumbnailSourceValue = ""
            viewer.currentVideoPosterSourceValue = ""
            viewer.currentVideoIdentityValue = ""
            viewer.currentOriginalSizeValue = Qt.size(0, 0)
            viewer.appliedPresentedIndex = -1
            viewer.appliedTierSignature = ""
            viewer.synchronizeVideoPlayback()
            viewer.refreshViewerRequestState()
            return
        }

        viewer.refreshViewerRequestState()
        viewer.currentIsVectorValue = typeof viewer.session.isVectorAt === "function"
                && viewer.session.isVectorAt(viewer.presentedIndex)
        if (viewer.currentIsVectorValue) viewer.sphericViewerMode = false
        viewer.currentIsVideoValue = typeof viewer.session.isVideoAt === "function"
                && viewer.session.videoPlaybackAvailable === true
                && viewer.session.isVideoAt(viewer.presentedIndex)
        if (viewer.currentIsVideoValue) {
            viewer.currentSourcesValue = []
            viewer.currentOriginalSizeValue = Qt.size(0, 0)
            const videoSource = typeof viewer.session.videoSourceAt === "function"
                    ? viewer.session.videoSourceAt(viewer.presentedIndex) : ({})
            const thumbnailSource = videoSource.thumbnailSource || ""
            const posterSource = videoSource.posterSource || ""
            viewer.currentVideoPosterSourceValue = posterSource
            viewer.currentVideoIdentityValue = videoSource.identity || ""
            viewer.currentVideoThumbnailSourceValue = thumbnailSource
            const previewSource = String(posterSource) !== ""
                    ? posterSource : thumbnailSource
            viewer.currentSourceValue = previewSource
            viewer.currentSourceLevelValue = previewSource.toString() === ""
                    ? -1 : 0
            if (String(previewSource) !== "") {
                imageViewport.setImage(previewSource, Qt.size(0, 0),
                                       viewer.presentedIndex, 0)
            } else {
                imageViewport.resetViewerImages()
            }
            // A first-frame poster can finish decoding after the panel-to-
            // viewer animation has begun. Upgrade its transition source and
            // the viewport together so the contact sheet is never left as
            // the moving image for the rest of the animation.
            if (String(posterSource) !== ""
                    && viewer.transitionHasGeometry
                    && !viewer.completingClose
                    && viewer.transitionProgress < 1)
                viewer.captureTransitionTarget(true)
            viewer.appliedPresentedIndex = viewer.presentedIndex
            viewer.appliedTierSignature = "video:" + viewer.presentedEntryId
            viewer.synchronizeVideoPlayback()
            return
        }

        viewer.currentVideoThumbnailSourceValue = ""
        viewer.currentVideoPosterSourceValue = ""
        viewer.currentVideoIdentityValue = ""
        const sources = sourceTiersAt(viewer.presentedIndex)
        viewer.currentSourcesValue = sources
        viewer.currentOriginalSizeValue = originalSizeAt(viewer.presentedIndex)
        if (sources.length > 0) {
            const best = sources[sources.length - 1]
            viewer.currentSourceValue = best.source
            viewer.currentSourceLevelValue = Number(best.level)
        } else {
            viewer.currentSourceValue = ""
            viewer.currentSourceLevelValue = -1
        }
        viewer.synchronizeVideoPlayback()

        const indexChanged = forceIndexChange === true
                || viewer.appliedPresentedIndex !== viewer.presentedIndex
        const wasFit = previousFitMode === undefined
                ? imageViewport.zoomFitView : previousFitMode
        let signature = viewer.presentedIndex + ":"
                + viewer.currentOriginalSizeValue.width + "x"
                + viewer.currentOriginalSizeValue.height
        for (let signatureIndex = 0; signatureIndex < sources.length;
             ++signatureIndex) {
            const signatureTier = sources[signatureIndex]
            signature += "|" + Number(signatureTier.level) + ":"
                    + signatureTier.source.toString()
        }
        if (indexChanged && sources.length === 0
                && !(viewer.currentIsVectorValue && imageViewport.vectorMode
                     && !viewer.session.viewerDecodeFailedAt(viewer.presentedIndex)))
            imageViewport.resetViewerImages()
        if (indexChanged || signature !== viewer.appliedTierSignature) {
            for (let sourceIndex = 0; sourceIndex < sources.length;
                 ++sourceIndex) {
                const tier = sources[sourceIndex]
                if (!tier || tier.source === undefined
                        || tier.source.toString() === "")
                    continue
                if (viewer.currentIsVectorValue)
                    imageViewport.setVectorImage(tier.source, viewer.currentOriginalSizeValue, viewer.presentedIndex)
                else imageViewport.setImage(tier.source, viewer.currentOriginalSizeValue,
                                       viewer.presentedIndex, Number(tier.level))
            }
            viewer.appliedTierSignature = signature
        }

        if (sources.length > 0) {
            viewer.appliedPresentedIndex = viewer.presentedIndex
            if (viewer.currentIsVectorValue && !imageViewport.imageTextureReady) {
                if (indexChanged) {
                    pendingVectorGeometryIndex = viewer.presentedIndex
                    pendingVectorFit = wasFit
                }
                return
            }
            pendingVectorGeometryIndex = -1
            applyCurrentViewport(indexChanged, wasFit)
        }
    }

    property int pendingVectorGeometryIndex: -1
    property bool pendingVectorFit: true
    property Connections vectorPresentationConnection: Connections {
        target: controller.imageViewport
        function onVectorSourcePresented(source) {
            if (controller.pendingVectorGeometryIndex !== controller.viewer.presentedIndex) return
            const wasFit = controller.pendingVectorFit
            controller.pendingVectorGeometryIndex = -1
            controller.applyCurrentViewport(true, wasFit)
        }
    }

    function applyCurrentViewport(indexChanged, wasFit) {
        if (indexChanged) {
            if (viewer.pendingCommittedViewport
                    && viewer.pendingCommittedViewport.index === viewer.presentedIndex) {
                viewer.applyPendingCommittedViewport()
            } else if (applyPendingPreviousImageViewport()) {
                // Restore the remembered center and zoom-to-fit ratio.
            } else if (wasFit) {
                imageViewport.zoomToFit(true)
            } else {
                imageViewport.fitViewerImageInViewportBounds()
            }
        } else {
            applyPendingPreviousImageViewport()
        }
    }

    function refreshNeighborSource() {
        if (!viewer.session || viewer.viewerNavigationTargetIndex < 0) {
            viewer.viewerNavigationTargetSource = ""
            viewer.viewerNavigationTargetSourceLevel = -1
            viewer.viewerNavigationTargetOriginalSize = Qt.size(0, 0)
            return
        }
        const sources = sourceTiersAt(viewer.viewerNavigationTargetIndex)
        const vector = typeof viewer.session.isVectorAt === "function"
                && viewer.session.isVectorAt(viewer.viewerNavigationTargetIndex)
        const requiredLevel = vector || imageViewport.zoomFitView
                && !viewer.sphericViewerMode ? 1 : 2
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
        viewer.viewerNavigationTargetSource = preparedSource
        viewer.viewerNavigationTargetSourceLevel = preparedSource.toString() === ""
                ? -1 : requiredLevel
        viewer.viewerNavigationTargetOriginalSize =
                originalSizeAt(viewer.viewerNavigationTargetIndex)
    }

    function requestIndex(index, scaleRatio) {
        if (viewer.customContent || !viewer.session || index < 0
                || viewer.width <= 0 || viewer.height <= 0)
            return
        if (typeof viewer.session.isImageAt === "function"
                && !viewer.session.isImageAt(index))
            return
        const ratio = Math.max(1, scaleRatio === undefined ? 1 : scaleRatio)
        const requestedWidth = Math.ceil(viewer.width * viewer.devicePixelRatio * ratio)
        const requestedHeight = Math.ceil(viewer.height * viewer.devicePixelRatio * ratio)
        if (index === viewer.session.currentIndex)
            viewer.session.requestViewer(requestedWidth, requestedHeight)
        else
            viewer.session.requestViewerAt(index, requestedWidth, requestedHeight)
    }

    function requestImage() {
        if (viewer.customContent || !viewer.session || viewer.presentedIndex < 0)
            return
        if (typeof viewer.session.isImageAt === "function"
                && !viewer.session.isImageAt(viewer.presentedIndex))
            return
        if (viewer.currentIsVector || imageViewport.zoomFitView && !viewer.sphericViewerMode
                && !viewer.nearestNeighbor) {
            requestIndex(viewer.presentedIndex, 1)
            viewer.refreshCurrentSource()
            return
        }
        // The original viewer uses a zero target as its native/full-size
        // sentinel.  Keeping that sentinel is important for the active
        // 16-image decode plan: every differently-sized neighbor receives its
        // own native tier rather than inheriting this image's dimensions.
        if (viewer.presentedIndex === viewer.session.currentIndex)
            viewer.session.requestViewer(0, 0)
        else
            viewer.session.requestViewerAt(viewer.presentedIndex, 0, 0)
        viewer.refreshCurrentSource()
    }

    function setPresentedIndex(index, awaitAuthority) {
        if (!viewer.session || index < 0)
            return false
        const nextEntryId = entryIdAt(index)
        if (nextEntryId === "")
            return false
        if (!awaitAuthority && nextEntryId !== navigationEntryId) {
            navigationEntryId = ""
            navigationOriginEntryId = ""
            navigationDirection = 0
        }
        const previousIndex = viewer.presentedIndex
        const sameIdentity = viewer.presentedEntryId !== ""
                && viewer.presentedEntryId === nextEntryId
        const indexChanged = !sameIdentity
        const previousFitMode = imageViewport.zoomFitView
        if (sameIdentity && previousIndex !== index) {
            imageViewport.remapImageIndex(previousIndex, index)
            if (viewer.appliedPresentedIndex === previousIndex)
                viewer.appliedPresentedIndex = index
        }
        viewer.presentedIndex = index
        viewer.presentedEntryId = nextEntryId
        refreshCurrentSource(indexChanged, previousFitMode)
        requestImage()
        continueFailedNavigation()
        if (awaitAuthority) {
            viewer.pendingAuthorityIndex = index
            viewer.pendingAuthorityEntryId = nextEntryId
            motion.authorityTimer.restart()
        } else {
            viewer.pendingAuthorityIndex = -1
            viewer.pendingAuthorityEntryId = ""
            motion.authorityTimer.stop()
        }
        return true
    }

    function recordPresentedTransition(fromEntryId, toEntryId,
                                       preservePendingViewport) {
        if (!viewer.session || fromEntryId === "" || toEntryId === ""
                || fromEntryId === toEntryId)
            return
        const viewport = preservePendingViewport
                ? previousViewport() : ({})
        if (viewer.session.viewerPreviousLocked) {
            const lockedEntryId = viewer.session.viewerPreviousEntryId
            let returnEntryId = viewer.session.viewerPreviousReturnEntryId
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
        if (!viewer.session || index < 0)
            return false
        const entryId = viewer.session.entryIdAt(index)
        const sourceIndex = viewer.session.sourceIndexAt(index)
        if (entryId === "" || sourceIndex < 0)
            return false
        if (viewer.presentedIndex !== index) {
            navigationDirection = index > viewer.presentedIndex ? 1 : -1
            if ((typeof viewer.session.viewerDecodeFailedAt !== "function"
                    || !viewer.session.viewerDecodeFailedAt(viewer.presentedIndex))
                    && (viewer.currentViewerRequestState === "ready"
                        || viewer.currentIsVideoValue
                        || viewer.currentSourcesValue.length > 0))
                navigationOriginEntryId = viewer.presentedEntryId
            navigationEntryId = entryId
        }
        recordPresentedTransition(viewer.presentedEntryId, entryId,
                                  preservePendingViewport === true)
        setPresentedIndex(index, true)
        viewer.navigationRequested(entryId, sourceIndex)
        return true
    }

    function adjacentIndex(fromIndex, direction) {
        if (!viewer.session || fromIndex < 0)
            return fromIndex
        if (typeof viewer.session.adjacentViewableIndex === "function")
            return viewer.session.adjacentViewableIndex(fromIndex, direction)
        return viewer.session.adjacentImageIndex(fromIndex, direction)
    }

    function navigate(direction) {
        const target = adjacentIndex(viewer.presentedIndex, direction)
        if (target === viewer.presentedIndex || target < 0)
            return false
        return emitNavigation(target)
    }

    function togglePreviousImageLock(index) {
        if (!viewer.session || index < 0)
            return
        const currentEntryId = entryIdAt(index)
        if (currentEntryId === "")
            return
        const previousEntryId = viewer.session.viewerPreviousEntryId
        if (viewer.session.viewerPreviousLocked) {
            if (previousEntryId === currentEntryId) {
                setPreviousState(viewer.session.viewerPreviousReturnEntryId,
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
        if (!viewer.session || viewer.presentedIndex < 0)
            return false
        const previousEntryId = viewer.session.viewerPreviousEntryId
        const previousIndex = indexForEntryId(previousEntryId)
        if (previousIndex < 0)
            return false

        let targetIndex = previousIndex
        if (viewer.session.viewerPreviousLocked
                && viewer.presentedEntryId === previousEntryId) {
            targetIndex = indexForEntryId(
                        viewer.session.viewerPreviousReturnEntryId)
            if (targetIndex < 0)
                return false
        }

        rememberViewportForPreviousImageSwitch(targetIndex)
        return emitNavigation(targetIndex, true)
    }

    function togglePreviousImage() {
        if (!viewer.session || viewer.presentedIndex < 0)
            return false
        if (indexForEntryId(viewer.session.viewerPreviousEntryId) >= 0)
            return switchToPreviousImage()

        // ViewerMode's first tilde chooses the next image when possible, then
        // falls back to the previous image at the end of the catalog.
        let target = adjacentIndex(viewer.presentedIndex, 1)
        if (target === viewer.presentedIndex || target < 0)
            target = adjacentIndex(viewer.presentedIndex, -1)
        if (target === viewer.presentedIndex || target < 0)
            return false
        return emitNavigation(target)
    }

    function navigateToEnd(direction) {
        let target = viewer.presentedIndex
        let next = target
        do {
            target = next
            next = adjacentIndex(target, direction)
        } while (next >= 0 && next !== target)
        if (target === viewer.presentedIndex)
            return false
        return emitNavigation(target)
    }


}
