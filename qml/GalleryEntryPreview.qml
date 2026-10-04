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
        entry.panelRoot.controller.thumbnailsEnabled
        && (entry.imageIdUrl !== "" || (entry.isImage
            && (entry.visualModel.imageDimensionsKnown
                || (entry.model && entry.model.fullSize
                    && entry.model.fullSize.width > 0 && entry.model.fullSize.height > 0))))
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

    IconImage {
        id: fallbackIcon
        objectName: preview.activePresentation
                    ? "galleryFallbackIcon-" + preview.entry.viewIndex : ""
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
        // A delegate can complete before MasonryLayout attaches it to its
        // window. At that point Qt uses the maximum screen DPR (for example
        // 2 instead of this panel's 1.75) and keeps the oversized raster after
        // attachment. Start the image request only with the owning window.
        source: !preview.activePresentation || !Window.window ? "" : lucideSource
                ? preview.entry.sourceColorIconAtSize(
                      modelIconSource, width, effectiveIconColor)
                : (systemFileSource
                   ? preview.entry.systemFileFallbackSource(width) : "")
        anchors.centerIn: parent
        readonly property real nominalIconSize:
            preview.entry.detailsMode || preview.entry.columnsMode
            ? Math.max(0, Math.min(parent.width, preview.entry.height
                       - 2 * preview.entry.panelRoot.metrics.detailsIconVerticalPadding))
            : preview.entry.iconsMode
            ? Math.max(0, Math.min(parent.width, parent.height))
            : Math.max(0, Math.min(parent.width, parent.height) * 0.55)
        width: preview.entry.snapIconExtent(nominalIconSize)
        height: width
        // Qt applies the window DPR to sourceSize before requesting provider
        // pixels. Supplying physical dimensions here applies DPR twice.
        sourceSize: Qt.size(Math.round(width * preview.entry.renderDpr),
                                Math.round(height * preview.entry.renderDpr))
        color: effectiveIconColor
        fillMode: Image.PreserveAspectFit
        smooth: false
        mipmap: false
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
                source: preview.entry.sourceColorIconAtSize(
                            fallbackIcon.modelIconSource, width)
                fillMode: Image.PreserveAspectFit
                smooth: false
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
            readonly property point pixelGridOffset:
                preview.entry.iconPixelOffset(videoPlayGlyph)
            anchors.centerIn: parent
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
            transform: Translate {
                x: videoPlayGlyph.pixelGridOffset.x
                y: videoPlayGlyph.pixelGridOffset.y
            }
        }
    }
}
