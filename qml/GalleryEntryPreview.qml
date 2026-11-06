pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.Native 1.0

GallerySceneItem {
    id: preview
    accessibilityLeaf: true

    required property GalleryEntryDelegateBase entry
    required property bool shaderThumbnail
    required property bool activePresentation
    readonly property alias thumbnailItem: thumbnail
    readonly property alias fallbackState: fallbackIcon
    readonly property GalleryEntryVisualFactories visualFactories: entry.panelRoot.entryVisualFactories
    readonly property bool isVector: entry.visualModel.isVector
    readonly property bool motionActive: entry.motionActive
    readonly property bool thumbnailEnabled:
        activePresentation && entry.panelRoot.controller.thumbnailsEnabled
        && (!entry.masonryMode || entry.masonryGeometryReady)
    // Recycled renderers can survive until the deferred Loader exchange. Give
    // each renderer a typed source, never the replacement's untyped URL.
    readonly property url bitmapThumbnailSource:
        thumbnailEnabled && !entry.visualModel.isVector ? entry.imageIdUrl : ""
    readonly property url vectorThumbnailSource:
        thumbnailEnabled && entry.visualModel.isVector
        ? entry.visualModel.vectorSource : ""
    readonly property bool thumbnailHasSource:
        thumbnail.source.toString() !== ""
    readonly property bool thumbnailReady:
        thumbnailHasSource && previewContent.item
        && previewContent.item.sourceStatus === Image.Ready
    // Cached metadata establishes the image's geometry before its pixels (or
    // even its lazy ImageFile facade) arrive. Suppress icons from that point.
    readonly property bool thumbnailExpected:
        entry.panelRoot.controller.thumbnailsEnabled === true
        && (!preview.isVector || thumbnailReady)
        && (entry.imageIdUrl !== "" || (entry.isImage === true
            && (entry.visualModel.imageDimensionsKnown
                || (entry.model && entry.model.fullSize
                    && entry.model.fullSize.width > 0 && entry.model.fullSize.height > 0))) === true)
        && (!thumbnailHasSource || !previewContent.item
            || previewContent.item.sourceStatus !== Image.Error)
    readonly property bool videoThumbnailReady:
        thumbnailReady && entry.visualModel
        && entry.visualModel.isVideo === true

    sceneVisible: activePresentation && !entry.folderPreviewActive
    visible: (entry.masonryMode && nativeCullingAvailable) || sceneVisible
    onActivePresentationChanged: {
        if (fallbackIcon) fallbackIcon.updateComponent()
        if (previewContent) previewContent.updateComponent()
        if (entry.visualIdentityChanging && entry.panelRoot.benchmarkTracingEnabled) {
            entry.panelRoot.traceBenchmarkStage("[FIX] preview.identity-reset", {
                "entryId": entry.entryId, "row": entry.viewIndex
            })
        }
    }
    opacity: entry.hiddenEntry ? 0.5 : 1
    x: entry.effectivePreviewX
    y: entry.effectivePreviewY
    width: entry.effectivePreviewWidth
    height: entry.effectivePreviewHeight
    // Masonry's shader masks its quad; Details fits raster previews inside
    // their slots. Per-preview clips split the shared text batch root.
    // SVGs can paint outside their viewBox and still require a crop.
    clip: (!entry.masonryMode && !entry.detailsMode)
          || vectorThumbnailSource.toString() !== ""

    GallerySceneItem {
        id: previewBackdrop
        objectName: preview.activePresentation
                    ? "galleryThumbnailBackdrop-" + preview.entry.viewIndex
                    : ""
        readonly property bool enabledForPresentation:
            preview.entry.masonryMode
        property alias color: backdropSurface.color
        anchors.fill: parent
        sceneVisible: enabledForPresentation
                 && !preview.thumbnailReady
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        visible: (preview.entry.masonryMode && nativeCullingAvailable) || sceneVisible
        Rectangle {
            id: backdropSurface
            objectName: "galleryThumbnailBackdropSurface-" + preview.entry.viewIndex
            anchors.fill: parent
            radius: 4
            color: preview.entry.panelRoot.previewBackdropColor
        }
    }

    GallerySceneItem {
        id: fallbackGate
        objectName: "galleryFallbackDrawingGate-" + preview.entry.viewIndex
        anchors.fill: parent
        sceneVisible: (fallbackIcon.tintedSource
                  || (fallbackIcon.systemFileSource
                      && !preview.thumbnailReady
                      && (!previewContent.item
                          || previewContent.item.sourceStatus !== Image.Ready)
                      && !failedThumbnailIcon.ready))
                 && !preview.thumbnailExpected
                 && fallbackIcon.hasModelIconSource
                 && (preview.entry.isFolder || !preview.entry.highlightMarker
                     || fallbackIcon.lucideSource)
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        visible: (preview.entry.masonryMode && nativeCullingAvailable) || sceneVisible

        GalleryFallbackIconHost {
            id: fallbackIcon
            preview: preview
        }
    }

    GallerySceneItem {
        id: thumbnail
        objectName: preview.activePresentation
                    ? "galleryThumbnail-" + preview.entry.viewIndex : ""
        x: 0
        y: 0
        width: Math.round(parent.width * preview.entry.renderDpr)
               / preview.entry.renderDpr
        height: Math.round(parent.height * preview.entry.renderDpr)
                / preview.entry.renderDpr
        property url source:
            preview.isVector ? preview.vectorThumbnailSource
                             : preview.bitmapThumbnailSource
        sceneVisible: preview.thumbnailHasSource
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        visible: (preview.entry.masonryMode && nativeCullingAvailable) || sceneVisible
    }

    GalleryPreviewVisualHost {
        id: previewContent
        preview: preview
        property bool frameCreated: false
        active: preview.activePresentation || frameCreated
        onLoaded: frameCreated = true
        readonly property bool sourceColorIconNeeded:
            !fallbackIcon.tintedSource
            && !preview.thumbnailExpected
            && !preview.thumbnailHasSource
            && fallbackIcon.hasModelIconSource
        readonly property bool markerNeeded:
            !preview.thumbnailHasSource
            && !fallbackIcon.hasModelIconSource
        anchors.fill: parent
        asynchronous: true
        readonly property Component requestedComponent: preview.thumbnailHasSource
                         ? (preview.isVector ? preview.entry.panelRoot.entryVisualFactories.vectorThumbnail : preview.shaderThumbnail
                            ? preview.entry.panelRoot.entryVisualFactories.shaderThumbnail
                            : preview.entry.panelRoot.entryVisualFactories.compactThumbnail)
                         : (sourceColorIconNeeded
                            ? preview.visualFactories.fallbackSourceColor
                            : (markerNeeded
                               ? preview.visualFactories.fallbackMarker : null))
        property Component retainedComponent: null
        function updateComponent() {
            Qt.callLater(commitComponent)
        }
        function commitComponent() {
            if (preview.activePresentation) retainedComponent = requestedComponent
        }
        onRequestedComponentChanged: updateComponent()
        Component.onCompleted: updateComponent()
        sourceComponent: retainedComponent
    }

    GalleryPreviewLoader {
        id: failedThumbnailIcon
        preview: preview
        anchors.fill: parent
        // Keep the failed thumbnail alive so its Error status remains stable.
        // Swapping that renderer for the icon would reset the status and retry
        // the same unreadable thumbnail whenever the native icon becomes ready.
        active: preview.activePresentation && preview.thumbnailHasSource
                && previewContent.item
                && previewContent.item.sourceStatus === Image.Error
                && !fallbackIcon.lucideSource
                && fallbackIcon.hasModelIconSource
        readonly property bool ready: status === Loader.Ready && item
                                      && item.sourceStatus === Image.Ready
        sourceComponent: preview.visualFactories.fallbackSourceColor
        onLoaded: {
            if (preview.entry.panelRoot.benchmarkTracingEnabled)
                preview.entry.panelRoot.traceBenchmarkStage(
                    "[FIX] preview.thumbnail-icon-fallback", {
                        "entryId": preview.entry.entryId,
                        "row": preview.entry.viewIndex
                    })
        }
    }

    GalleryPreviewLoader {
        id: videoBadgeHost
        preview: preview
        enabled: false
        z: 3
        anchors.fill: parent
        active: preview.videoThumbnailReady
        sourceComponent: preview.visualFactories.videoBadge
    }
}
