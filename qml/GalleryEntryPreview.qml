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
            preview.entry.panelRoot.iconResolver.resolve(
                        preview.entry.iconKey, preview.entry.iconPath,
                        preview.entry.largePreviewMode,
                        preview.entry.isFolder,
                        preview.entry.isImage,
                        preview.entry.effectiveDisplayName === "..")
        readonly property bool lucideSource:
            preview.entry.isLucideIconSource(modelIconSource)
        readonly property bool systemFileSource:
            preview.entry.isSystemFileIconSource(modelIconSource)
        source: lucideSource
                ? preview.entry.sourceColorIconAtSize(
                      modelIconSource, width, effectiveIconColor)
                : (systemFileSource
                   ? preview.entry.systemFileFallbackSource(width) : "")
        anchors.centerIn: parent
        readonly property real nominalIconSize:
            preview.entry.detailsMode || preview.entry.columnsMode
            ? Math.min(parent.width,
                       preview.entry.panelRoot.detailsIconSize)
            : preview.entry.iconsMode
            ? Math.max(0, Math.min(parent.width, parent.height))
            : Math.max(0, Math.min(parent.width, parent.height) * 0.55)
        width: preview.entry.snapIconExtent(nominalIconSize)
        height: width
        sourceSize: Qt.size(width, height)
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
                      && !preview.thumbnailReady))
                 && !preview.thumbnailReady
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
            preview.entry.imageIdUrl
        visible: thumbnail.source.toString() !== ""
                 && !(preview.entry.panelRoot.viewerTransitionActive
                      && preview.entry.panelRoot.viewerTransitionEntryId
                         === preview.entry.entryId)
    }

    Loader {
        id: previewContent
        readonly property bool sourceColorIconNeeded:
            !fallbackIcon.lucideSource
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
}
