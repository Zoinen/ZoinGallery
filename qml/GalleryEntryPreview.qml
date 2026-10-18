pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.impl
import ZoinGallery.Native 1.0

GallerySceneItem {
    id: preview
    accessibilityLeaf: true

    required property GalleryEntryDelegateBase entry
    required property bool shaderThumbnail
    required property bool activePresentation
    readonly property alias thumbnailItem: thumbnail
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

    function videoPlayIconSource(logicalSize) {
        const provider = entry.panelRoot.iconProvider
        if (provider && typeof provider.rasterizedLucideSource === "function"
                && typeof provider.lucideStrokeWidth === "function") {
            const size = Math.max(1, Math.round(Number(logicalSize) || 1))
            return provider.rasterizedLucideSource(
                        "play-filled", size, entry.renderDpr, "#ffffff",
                        provider.lucideStrokeWidth(size))
        }
        return "qrc:/ZoinGallery/resources/VideoPlayFilled.svg"
    }

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
    opacity: entry.hiddenEntry && !entry.detailsMode ? 0.5 : 1
    x: entry.effectivePreviewRect.x
    y: entry.effectivePreviewRect.y
    width: entry.effectivePreviewRect.width
    height: entry.effectivePreviewRect.height
    // Masonry's raster shader masks its quad already. A clip per tile splits
    // renderer batches, including the bounded content layered above the image.
    // SVGs can paint outside their viewBox and still require a crop.
    clip: !entry.masonryMode || vectorThumbnailSource.toString() !== ""

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
        sceneVisible: (fallbackIcon.lucideSource
                  || (fallbackIcon.systemFileSource
                      && !preview.thumbnailReady
                      && (!previewContent.item
                          || previewContent.item.sourceStatus !== Image.Ready)
                      && !failedThumbnailIcon.ready))
                 && !preview.thumbnailExpected
                 && fallbackIcon.modelIconSource.toString() !== ""
                 && (!preview.entry.highlightMarker || fallbackIcon.lucideSource)
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        visible: (preview.entry.masonryMode && nativeCullingAvailable) || sceneVisible

        Loader {
            id: fallbackIcon
            // Keep the primitive through identity resets, but clear its source.
            property bool frameCreated: false
            active: preview.activePresentation || frameCreated
            onLoaded: frameCreated = true
            readonly property Component requestedComponent:
                useAspectRaster ? aspectRasterComponent : tintedRasterComponent
            property Component retainedComponent: tintedRasterComponent
            function updateComponent() {
                Qt.callLater(commitComponent)
            }
            function commitComponent() {
                if (preview.activePresentation) retainedComponent = requestedComponent
            }
            onRequestedComponentChanged: updateComponent()
            Component.onCompleted: updateComponent()
            asynchronous: false
            readonly property color effectiveIconColor:
                preview.entry.fallbackIconColor
            property url modelIconSource:
                !preview.activePresentation ? "" : preview.entry.panelRoot.iconResolver.resolve(
                            preview.entry.iconKey, preview.entry.iconPath,
                            preview.entry.largePreviewMode,
                            preview.entry.isFolder,
                            preview.entry.isImage,
                            preview.entry.effectiveDisplayName === "..")
            readonly property bool lucideSource:
                preview.entry.isLucideIconSource(modelIconSource)
            readonly property bool systemFileSource:
                preview.entry.isSystemFileIconSource(modelIconSource)
            anchors.centerIn: parent
            readonly property real nominalIconSize:
                preview.entry.detailsMode || preview.entry.columnsMode
                ? Math.max(0, Math.min(parent.width,
                           lucideSource && preview.entry.panelRoot.density <= 30
                               ? preview.entry.panelRoot.detailsIconSize : Infinity,
                           preview.entry.height
                           - 2 * preview.entry.panelRoot.metrics.detailsIconVerticalPadding))
                : preview.entry.iconsMode || preview.entry.gridMode
                ? Math.max(0, Math.min(parent.width, parent.height))
                : Math.max(0, Math.min(parent.width, parent.height) * 0.55)
            readonly property bool fullAreaIcon: preview.entry.iconsMode || preview.entry.gridMode
            width: fullAreaIcon ? parent.width
                                          : preview.entry.snapIconExtent(nominalIconSize)
            height: fullAreaIcon ? parent.height : width
            readonly property point pixelGridOffset:
                preview.entry.iconPixelOffset(fallbackIcon)
            transform: Translate {
                x: fallbackIcon.pixelGridOffset.x
                y: fallbackIcon.pixelGridOffset.y
            }
            opacity: preview.entry.detailsMode || preview.entry.columnsMode
                     ? 1 : 0.78
            readonly property bool useAspectRaster: fullAreaIcon
                                                   && rasterSource.toString().startsWith("image://")
            readonly property real rasterExtent: fullAreaIcon
                                                    ? Math.max(width, height)
                                                    : Math.min(width, height)
            readonly property url rasterSource: !Window.window ? "" : lucideSource
                ? preview.entry.sourceColorIconAtSize(modelIconSource, rasterExtent, effectiveIconColor)
                : (systemFileSource ? preview.entry.systemFileFallbackSource(rasterExtent) : "")
            sourceComponent: retainedComponent
        }
    }

    Component {
        id: aspectRasterComponent
        Image {
            objectName: preview.activePresentation
                        ? "galleryFallbackIcon-" + preview.entry.viewIndex : ""
            anchors.fill: parent
            source: fallbackIcon.rasterSource
            // Qt multiplies provider requests by the window DPR. Request the
            // complete logical slot so its bitmap maps 1:1 to physical pixels.
            sourceSize: Qt.size(Math.round(width), Math.round(height))
            fillMode: Image.PreserveAspectFit
            verticalAlignment: preview.entry.iconsMode ? Image.AlignBottom : Image.AlignVCenter
            smooth: preview.motionActive
            mipmap: false
            retainWhileLoading: false
        }
    }

    Component {
        id: tintedRasterComponent
        IconImage {
            id: fallbackRaster
            objectName: preview.activePresentation
                        ? "galleryFallbackIcon-" + preview.entry.viewIndex : ""
            readonly property color effectiveIconColor:
                fallbackIcon.effectiveIconColor
            anchors.fill: parent
            // A delegate can complete before MasonryLayout attaches it to
            // its window. Request pixels only at the owning window's DPR.
            source: fallbackIcon.rasterSource
            // Qt applies DPR once to this logical sourceSize.
            // Icon providers render the requested rectangle directly. Keep
            // the raster square; PreserveAspectFit positions it in our slot.
            sourceSize: Qt.size(Math.round(fallbackIcon.rasterExtent),
                                Math.round(fallbackIcon.rasterExtent))
            color: fallbackIcon.effectiveIconColor
            fillMode: Image.PreserveAspectFit
            verticalAlignment: preview.entry.iconsMode ? Image.AlignBottom : Image.AlignVCenter
            smooth: preview.motionActive
            mipmap: false
            retainWhileLoading: false
            readonly property point pixelGridOffset:
                fallbackIcon.pixelGridOffset
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
        sceneVisible: thumbnail.source.toString() !== ""
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        visible: (preview.entry.masonryMode && nativeCullingAvailable) || sceneVisible
    }

    Loader {
        id: previewContent
        property bool frameCreated: false
        active: preview.activePresentation || frameCreated
        onLoaded: frameCreated = true
        readonly property bool sourceColorIconNeeded:
            !fallbackIcon.lucideSource
            && !preview.thumbnailExpected
            && thumbnail.source.toString() === ""
            && fallbackIcon.modelIconSource.toString() !== ""
        readonly property bool markerNeeded:
            thumbnail.source.toString() === ""
            && fallbackIcon.modelIconSource.toString() === ""
        anchors.fill: parent
        asynchronous: false
        readonly property Component requestedComponent: thumbnail.source.toString() !== ""
                         ? (preview.isVector ? vectorThumbnailComponent : preview.shaderThumbnail
                            ? shaderThumbnailComponent
                            : compactThumbnailComponent)
                         : (sourceColorIconNeeded
                            ? sourceColorIconComponent
                            : (markerNeeded
                               ? fallbackMarkerComponent : null))
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

    Component {
        id: sourceColorIconComponent

        Item {
            readonly property int sourceStatus: sourceColorIcon.status

            Image {
                id: sourceColorIcon
                property string readyEntryId: ""
                onStatusChanged: {
                    if (status === Image.Ready && preview.activePresentation)
                        readyEntryId = preview.entry.entryId
                }
                objectName: preview.activePresentation
                            ? "gallerySourceColorIcon-"
                              + preview.entry.viewIndex : ""
                anchors.centerIn: parent
                width: fallbackIcon.width
                height: fallbackIcon.height
                readonly property real rasterExtent: fallbackIcon.fullAreaIcon
                                                        ? Math.max(width, height)
                                                        : Math.min(width, height)
                source: preview.entry.sourceColorIconAtSize(
                            fallbackIcon.modelIconSource, rasterExtent)
                // Direct SVGs must derive height from their native aspect.
                // Specifying both dimensions can rasterize them stretched
                // before PreserveAspectFit ever sees the resulting texture.
                sourceSize: source.toString().startsWith("image://")
                    ? (fallbackIcon.fullAreaIcon
                    ? Qt.size(Math.round(width), Math.round(height))
                    : Qt.size(Math.round(rasterExtent), Math.round(rasterExtent)))
                    : Qt.size(Math.round(rasterExtent), 0)
                fillMode: Image.PreserveAspectFit
                verticalAlignment: preview.entry.iconsMode ? Image.AlignBottom : Image.AlignVCenter
                // Colored SVGs can have a rectangular canvas, so fitting the
                // loaded raster to the slot requires filtered sampling.
                smooth: true
                asynchronous: true
                cache: true
                retainWhileLoading: preview.activePresentation
                    && readyEntryId === preview.entry.entryId
                visible: !(preview.entry.panelRoot.viewerTransitionActive
                           && preview.entry.panelRoot.viewerTransitionEntryId
                              === preview.entry.entryId)
                readonly property point pixelGridOffset:
                    preview.entry.iconPixelOffset(sourceColorIcon)
                transform: Translate {
                    x: sourceColorIcon.pixelGridOffset.x
                    y: sourceColorIcon.pixelGridOffset.y
                }
            }
        }
    }

    Loader {
        id: failedThumbnailIcon
        anchors.fill: parent
        // Keep the failed thumbnail alive so its Error status remains stable.
        // Swapping that renderer for the icon would reset the status and retry
        // the same unreadable thumbnail whenever the native icon becomes ready.
        active: preview.activePresentation && preview.thumbnailHasSource
                && previewContent.item
                && previewContent.item.sourceStatus === Image.Error
                && !fallbackIcon.lucideSource
                && fallbackIcon.modelIconSource.toString() !== ""
        readonly property bool ready: status === Loader.Ready && item
                                      && item.sourceStatus === Image.Ready
        sourceComponent: sourceColorIconComponent
        onLoaded: {
            if (preview.entry.panelRoot.benchmarkTracingEnabled)
                preview.entry.panelRoot.traceBenchmarkStage(
                    "[FIX] preview.thumbnail-icon-fallback", {
                        "entryId": preview.entry.entryId,
                        "row": preview.entry.viewIndex
                    })
        }
    }

    Component {
        id: fallbackMarkerComponent

        Text {
            objectName: preview.activePresentation
                        ? "galleryFallbackMarker-"
                          + preview.entry.viewIndex : ""
            text: preview.entry.highlightMarker
                  || (preview.entry.isFolder
                      ? (preview.entry.effectiveDisplayName === ".."
                         ? "↰" : "▸")
                      : " ")
            color: preview.entry.fallbackIconColor
            font.pixelSize: preview.entry.panelRoot.detailsNameFontPixelSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component {
        id: vectorThumbnailComponent
        GalleryVectorPreview {
            id: vectorThumbnail
            inViewport: preview.entry.sceneVisible
            sceneCache: preview.entry.panelRoot.vectorSceneCache
            source: preview.vectorThumbnailSource
            onSourceStatusChanged: {
                if (sourceStatus !== Image.Error) return
                const row = preview.entry.viewIndex
                const failedSource = source
                Qt.callLater(() => {
                    if (preview.entry.panelRoot.session)
                        preview.entry.panelRoot.session.reportVectorFailure(row, failedSource)
                })
            }
            devicePixelRatio: preview.entry.renderDpr
            readonly property point pixelGridOffset: preview.entry.iconPixelOffset(vectorThumbnail)
            transform: Translate { x: vectorThumbnail.pixelGridOffset.x; y: vectorThumbnail.pixelGridOffset.y }
            crop: preview.entry.masonryMode
            showCheckerboard: preview.shaderThumbnail && preview.entry.panelRoot.showTransparentGrid
        }
    }

    Component {
        id: shaderThumbnailComponent
        GalleryThumbnailShaderLayer {
            entry: preview.entry
            imageSource: preview.bitmapThumbnailSource
            activePresentation: preview.activePresentation
        }
    }

    Component {
        id: compactThumbnailComponent
        GalleryThumbnailCompactLayer {
            entry: preview.entry
            imageSource: preview.bitmapThumbnailSource
            activePresentation: preview.activePresentation
        }
    }

    GallerySceneItem {
        id: videoPlayBadge
        objectName: preview.activePresentation
                    ? "galleryVideoPlayBadge-" + preview.entry.viewIndex : ""
        enabled: false
        z: 3
        readonly property real badgeDiameter: Math.round(
            Math.min(76, Math.min(parent.width, parent.height) * 0.35)
            * preview.entry.renderDpr) / preview.entry.renderDpr
        readonly property real glyphExtent: {
            const dpr = preview.entry.renderDpr
            const badgePixels = Math.round(width * dpr)
            const targetPixels = Math.max(1, Math.round(badgePixels * 0.46))
            const centeredPixels = targetPixels
                + ((badgePixels - targetPixels) % 2)
            return centeredPixels / dpr
        }
        readonly property point pixelGridOffset:
            preview.entry.iconPixelOffset(videoPlayBadge)
        x: Math.round((parent.width - width) * preview.entry.renderDpr / 2)
           / preview.entry.renderDpr
        y: Math.round((parent.height - height) * preview.entry.renderDpr / 2)
           / preview.entry.renderDpr
        width: badgeDiameter
        height: badgeDiameter
        sceneVisible: preview.videoThumbnailReady && badgeDiameter >= 28
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        visible: (preview.entry.masonryMode && nativeCullingAvailable) || sceneVisible
        transform: Translate {
            x: videoPlayBadge.pixelGridOffset.x
            y: videoPlayBadge.pixelGridOffset.y
        }

        Rectangle {
            id: videoPlayBadgeCircle
            objectName: videoPlayBadge.sceneVisible
                        ? "galleryVideoPlayBadgeCircle-"
                          + preview.entry.viewIndex : ""
            anchors.fill: parent
            radius: width / 2
            color: "#b80c0c0c"
            border.color: "#18ffffff"
            border.width: 1 / preview.entry.renderDpr
            antialiasing: true
        }

        Image {
            id: videoPlayGlyph
            objectName: videoPlayBadge.sceneVisible
                        ? "galleryVideoPlayIcon-"
                          + preview.entry.viewIndex : ""
            x: Math.round((parent.width - width) * preview.entry.renderDpr / 2)
               / preview.entry.renderDpr
            y: Math.round((parent.height - height) * preview.entry.renderDpr / 2)
               / preview.entry.renderDpr
            width: videoPlayBadge.glyphExtent
            height: width
            source: !videoPlayBadge.sceneVisible || !Window.window
                    ? "" : preview.videoPlayIconSource(width)
            sourceSize: Qt.size(Math.round(width * preview.entry.renderDpr),
                                Math.round(height * preview.entry.renderDpr))
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
            retainWhileLoading: true
            smooth: true
            mipmap: false
        }
    }
}
