pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.Native 1.0

GallerySceneItem {
    id: layer

    required property GalleryEntryDelegateBase entry
    required property url imageSource
    required property bool activePresentation
    readonly property int sourceStatus: sourceImage.status
    // Parking or a coherent identity commit hides drawing without evicting
    // ready pixels. A new source is published once its presentation is ready.
    property url retainedImageSource: ""
    readonly property bool retainBitmapPixels:
        entry.panelRoot.controller.thumbnailsEnabled
        && !entry.visualModel.isVector
        && (entry.isImage || entry.visualModel.isVideo)
    onImageSourceChanged: {
        if (imageSource.toString() !== "" || !retainBitmapPixels)
            retainedImageSource = imageSource
    }
    onRetainBitmapPixelsChanged: {
        if (!retainBitmapPixels) retainedImageSource = ""
    }
    Component.onCompleted: retainedImageSource = imageSource
    sceneVisible: imageSource.toString() !== ""
    visible: (entry.masonryMode && nativeCullingAvailable) || sceneVisible

    GalleryThumbnailImage {
        id: sourceImage
        objectName: layer.activePresentation
                    ? "galleryThumbnailImage-" + layer.entry.viewIndex : ""
        property int xDiff:
            implicitWidth - layer.width * layer.entry.renderDpr
        property int yDiff:
            implicitHeight - layer.height * layer.entry.renderDpr
        property bool diffIsSmall:
            Math.abs(xDiff) < 4 && Math.abs(yDiff) < 4
        width: diffIsSmall ? implicitWidth / layer.entry.renderDpr
                           : layer.width
        height: diffIsSmall ? implicitHeight / layer.entry.renderDpr
                            : layer.height
        source: layer.retainedImageSource
        fillMode: diffIsSmall
                  ? Image.Pad
                  : (layer.entry.masonryMode
                     ? Image.PreserveAspectCrop
                     : Image.PreserveAspectFit)
        asynchronous: false
        // Managed thumbnail IDs include revision, size and a unique serial.
        cache: layer.entry.masonryMode
            && sourceImage.source.toString().startsWith("image://")
            && sourceImage.source.toString().indexOf("/zg-thumb-") >= 0
        visible: false
    }

    ShaderEffect {
        id: thumbnailShader
        objectName: layer.activePresentation
                    ? "galleryThumbnailShader-" + layer.entry.viewIndex : ""
        readonly property real textureAspect:
            sourceImage.implicitWidth > 0 && sourceImage.implicitHeight > 0
            ? sourceImage.implicitWidth / sourceImage.implicitHeight : 0
        // A previous zoom tier can remain ready while the new one loads.
        // Keep those pixels centered at their available resolution.
        readonly property real masonryScale:
            layer.entry.masonryMode && textureAspect > 0
            && layer.width > 0 && layer.height > 0
            ? Math.min(1, sourceImage.implicitWidth / (layer.width * layer.entry.renderDpr),
                          sourceImage.implicitHeight / (layer.height * layer.entry.renderDpr))
            : 1
        readonly property real fittedWidth:
            layer.entry.masonryMode || textureAspect <= 0
            ? layer.width * masonryScale
            : Math.min(layer.width, layer.height * textureAspect)
        readonly property real fittedHeight:
            layer.entry.masonryMode || textureAspect <= 0
            ? layer.height * masonryScale
            : Math.min(layer.height, layer.width / textureAspect)
        x: Math.round((layer.width - width) * layer.entry.renderDpr / 2)
           / layer.entry.renderDpr
        y: layer.entry.iconsMode ? layer.height - height
           : Math.round((layer.height - height) * layer.entry.renderDpr / 2)
             / layer.entry.renderDpr
        width: Math.round(fittedWidth * layer.entry.renderDpr) / layer.entry.renderDpr
        height: Math.round(fittedHeight * layer.entry.renderDpr) / layer.entry.renderDpr
        readonly property var pixelCorrection:
            layer.entry.iconPixelAlignment(thumbnailShader)
        transform: Translate { x: (thumbnailShader.pixelCorrection ? thumbnailShader.pixelCorrection.offsetX : 0); y: (thumbnailShader.pixelCorrection ? thumbnailShader.pixelCorrection.offsetY : 0) }
        property var source: sourceImage
        property size viewportSize: Qt.size(
            width * layer.entry.renderDpr,
            height * layer.entry.renderDpr)
        property real sharpenAmount: 1.5
        property bool showCheckerboard: layer.entry.panelRoot.showTransparentGrid
        property int checkerboardSize: 4 * layer.entry.renderDpr
        property real borderRadius: 4.1 * layer.entry.renderDpr
        fragmentShader: "qrc:/ZoinGallery/resources/shader.frag.qsb"
    }
}
