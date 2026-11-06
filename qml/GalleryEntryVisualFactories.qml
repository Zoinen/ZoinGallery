pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.Native 1.0

// Factories belong to the panel; instances obtain their row from their host.
QtObject {
    readonly property Component masonry: Component {
        GalleryMasonryEntryDelegate {
            anchors.fill: parent
            entry: (parent as GalleryVisualHost).entry
        }
    }
    readonly property Component columns: Component {
        ColumnsEntryDelegate {
            anchors.fill: parent
            entry: (parent as GalleryVisualHost).entry
        }
    }
    readonly property Component details: Component {
        DetailsEntryDelegate {
            anchors.fill: parent
            entry: (parent as GalleryVisualHost).entry
        }
    }
    readonly property Component grid: Component {
        GridEntryDelegate {
            anchors.fill: parent
            entry: (parent as GalleryVisualHost).entry
        }
    }
    readonly property Component icons: Component {
        IconsEntryDelegate {
            anchors.fill: parent
            entry: (parent as GalleryVisualHost).entry
        }
    }
    readonly property Component folder: Component {
        GalleryFolderPreview {
            entry: (parent as GalleryVisualHost).entry
        }
    }
    readonly property Component vectorThumbnail: Component {
        GalleryVectorPreview {
            id: vectorThumbnail
            readonly property GalleryEntryPreview preview: (parent as GalleryPreviewVisualHost).preview
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
            readonly property var pixelGridOffset: preview.entry.iconPixelAlignment(vectorThumbnail)
            transform: Translate { x: (vectorThumbnail.pixelGridOffset ? vectorThumbnail.pixelGridOffset.offsetX : 0); y: (vectorThumbnail.pixelGridOffset ? vectorThumbnail.pixelGridOffset.offsetY : 0) }
            crop: preview.entry.masonryMode
            showCheckerboard: preview.shaderThumbnail && preview.entry.panelRoot.showTransparentGrid
        }
    }
    readonly property Component shaderThumbnail: Component {
        GalleryThumbnailShaderLayer {
            readonly property GalleryEntryPreview preview: (parent as GalleryPreviewVisualHost).preview
            sceneVisible: preview.thumbnailItem.sceneVisible && !preview.isVector
            entry: preview.entry
            imageSource: preview.bitmapThumbnailSource
            activePresentation: preview.activePresentation
        }
    }
    readonly property Component compactThumbnail: Component {
        GalleryThumbnailCompactLayer {
            readonly property GalleryEntryPreview preview: (parent as GalleryPreviewVisualHost).preview
            entry: preview.entry
            imageSource: preview.bitmapThumbnailSource
            activePresentation: preview.activePresentation
        }
    }

    readonly property Component fallbackAspect: Component {
        Image {
            readonly property GalleryEntryPreview preview: (parent as GalleryFallbackIconHost).preview
            readonly property GalleryFallbackIconHost fallbackIcon: preview.fallbackState
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

    readonly property Component fallbackTinted: Component {
        GalleryIconImage {
            id: fallbackRaster
            readonly property GalleryEntryPreview preview: (parent as GalleryFallbackIconHost).preview
            readonly property GalleryFallbackIconHost fallbackIcon: preview.fallbackState
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
            readonly property var pixelGridOffset:
                fallbackIcon.pixelGridOffset
        }
    }

    readonly property Component fallbackSourceColor: Component {

        GallerySceneItem {
            readonly property GalleryEntryPreview preview:
                parent instanceof GalleryPreviewLoader
                ? (parent as GalleryPreviewLoader).preview
                : (parent as GalleryPreviewVisualHost).preview
            readonly property GalleryFallbackIconHost fallbackIcon: preview.fallbackState
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
                readonly property string sizedSource: preview.entry.sourceColorIconAtSize(
                            fallbackIcon.modelIconSourceText, rasterExtent)
                source: sizedSource
                // Direct SVGs must derive height from their native aspect.
                // Specifying both dimensions can rasterize them stretched
                // before PreserveAspectFit ever sees the resulting texture.
                sourceSize: sizedSource.startsWith("image://")
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
                readonly property var pixelGridOffset:
                    preview.entry.iconPixelAlignment(sourceColorIcon)
                transform: Translate {
                    x: (sourceColorIcon.pixelGridOffset ? sourceColorIcon.pixelGridOffset.offsetX : 0)
                    y: (sourceColorIcon.pixelGridOffset ? sourceColorIcon.pixelGridOffset.offsetY : 0)
                }
            }
        }
    }

    readonly property Component fallbackMarker: Component {

        Text {
            id: fallbackMarker
            readonly property GalleryEntryPreview preview: (parent as GalleryPreviewVisualHost).preview
            readonly property var pixelGridOffset: preview.entry.iconPixelAlignment(fallbackMarker)
            transform: Translate {
                x: (fallbackMarker.pixelGridOffset ? fallbackMarker.pixelGridOffset.offsetX : 0)
                y: (fallbackMarker.pixelGridOffset ? fallbackMarker.pixelGridOffset.offsetY : 0)
            }
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


    readonly property Component videoBadge: Component {
        // Loader sizes this surface; the badge keeps its own circular extent.
        Item {
            id: videoBadgeSurface
            readonly property GalleryEntryPreview preview: (parent as GalleryPreviewLoader).preview
            GallerySceneItem {
                id: videoPlayBadge
                readonly property GalleryEntryPreview preview: videoBadgeSurface.preview
                function videoPlayIconSource(logicalSize) {
                    const provider = preview.entry.panelRoot.iconProvider
                    if (provider && typeof provider.rasterizedLucideSource === "function"
                            && typeof provider.lucideStrokeWidth === "function") {
                        const size = Math.max(1, Math.round(Number(logicalSize) || 1))
                        return provider.rasterizedLucideSource(
                                    "play-filled", size, preview.entry.renderDpr, "#ffffff",
                                    provider.lucideStrokeWidth(size))
                    }
                    return "qrc:/ZoinGallery/resources/VideoPlayFilled.svg"
                }
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
                readonly property var pixelGridOffset:
                    preview.entry.iconPixelAlignment(videoPlayBadge)
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
                    x: (videoPlayBadge.pixelGridOffset ? videoPlayBadge.pixelGridOffset.offsetX : 0)
                    y: (videoPlayBadge.pixelGridOffset ? videoPlayBadge.pixelGridOffset.offsetY : 0)
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
                            ? "" : videoPlayBadge.videoPlayIconSource(width)
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
    }
}
