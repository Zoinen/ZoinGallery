pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.impl

Item {
    id: preview

    required property GalleryEntryDelegateBase entry
    required property bool shaderThumbnail
    required property bool activePresentation
    readonly property alias thumbnailItem: thumbnail
    readonly property bool thumbnailHasSource:
        thumbnail.source.toString() !== ""
    readonly property bool thumbnailReady:
        thumbnailHasSource && previewContent.item
        && previewContent.item.sourceStatus === Image.Ready
    // Cached metadata establishes the image's geometry before its pixels (or
    // even its lazy ImageFile facade) arrive. Suppress icons from that point.
    readonly property bool thumbnailExpected:
        entry.panelRoot.controller.thumbnailsEnabled === true
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

    visible: activePresentation && !entry.folderPreviewActive
    onActivePresentationChanged: {
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
    clip: true

    Rectangle {
        id: previewBackdrop
        objectName: preview.activePresentation
                    ? "galleryThumbnailBackdrop-" + preview.entry.viewIndex
                    : ""
        readonly property bool enabledForPresentation:
            preview.entry.masonryMode || preview.entry.gridMode
        anchors.fill: parent
        radius: 4
        color: preview.entry.panelRoot.previewBackdropColor
        visible: enabledForPresentation
                 && !preview.thumbnailReady
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
    }

    Loader {
        id: fallbackIcon
        // IconImage's empty source can fall back to its preceding URL. Own
        // its lifetime here instead: only a new entry discards the old leaf;
        // resizing the same entry keeps both the leaf and its loaded pixels.
        active: preview.activePresentation
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
        visible: (lucideSource
                  || (systemFileSource
                      && !preview.thumbnailReady
                      && (!previewContent.item
                          || previewContent.item.sourceStatus
                             !== Image.Ready)))
                 && !preview.thumbnailExpected
                 && fallbackIcon.modelIconSource.toString() !== ""
                 && (!preview.entry.highlightMarker || lucideSource)
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        readonly property bool useAspectRaster: fullAreaIcon
                                               && rasterSource.toString().startsWith("image://")
        readonly property real rasterExtent: fullAreaIcon
                                                ? Math.max(width, height)
                                                : Math.min(width, height)
        readonly property url rasterSource: !Window.window ? "" : lucideSource
            ? preview.entry.sourceColorIconAtSize(modelIconSource, rasterExtent, effectiveIconColor)
            : (systemFileSource ? preview.entry.systemFileFallbackSource(rasterExtent) : "")
        sourceComponent: useAspectRaster ? aspectRasterComponent : tintedRasterComponent
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
            smooth: false
            mipmap: false
            retainWhileLoading: true
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
            smooth: false
            mipmap: false
            retainWhileLoading: true
            readonly property point pixelGridOffset:
                fallbackIcon.pixelGridOffset
        }
    }

    Item {
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
            preview.activePresentation
                && preview.entry.panelRoot.controller.thumbnailsEnabled
                && (!preview.entry.masonryMode || preview.entry.masonryGeometryReady)
                ? preview.entry.imageIdUrl : ""
        visible: thumbnail.source.toString() !== ""
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
    }

    Loader {
        id: previewContent
        active: preview.activePresentation
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
        sourceComponent: thumbnail.source.toString() !== ""
                         ? (preview.shaderThumbnail
                            ? shaderThumbnailComponent
                            : compactThumbnailComponent)
                         : (sourceColorIconNeeded
                            ? sourceColorIconComponent
                            : (markerNeeded
                               ? fallbackMarkerComponent : null))
    }

    Component {
        id: sourceColorIconComponent

        Item {
            readonly property int sourceStatus: sourceColorIcon.status

            Image {
                id: sourceColorIcon
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
                retainWhileLoading: true
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
        id: shaderThumbnailComponent
        GalleryThumbnailShaderLayer {
            entry: preview.entry
            imageSource: thumbnail.source
            activePresentation: preview.activePresentation
        }
    }

    Component {
        id: compactThumbnailComponent
        GalleryThumbnailCompactLayer {
            entry: preview.entry
            imageSource: thumbnail.source
            activePresentation: preview.activePresentation
        }
    }

    Item {
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
        visible: preview.videoThumbnailReady && badgeDiameter >= 28
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
        transform: Translate {
            x: videoPlayBadge.pixelGridOffset.x
            y: videoPlayBadge.pixelGridOffset.y
        }

        Rectangle {
            id: videoPlayBadgeCircle
            objectName: videoPlayBadge.visible
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
            objectName: videoPlayBadge.visible
                        ? "galleryVideoPlayIcon-"
                          + preview.entry.viewIndex : ""
            x: Math.round((parent.width - width) * preview.entry.renderDpr / 2)
               / preview.entry.renderDpr
            y: Math.round((parent.height - height) * preview.entry.renderDpr / 2)
               / preview.entry.renderDpr
            width: videoPlayBadge.glyphExtent
            height: width
            source: !videoPlayBadge.visible || !Window.window
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
