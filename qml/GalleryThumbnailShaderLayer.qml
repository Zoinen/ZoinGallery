pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: layer

    required property GalleryEntryDelegateBase entry
    required property url imageSource
    required property bool activePresentation
    readonly property int sourceStatus: sourceImage.status

    Image {
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
        source: layer.imageSource
        fillMode: diffIsSmall
                  ? Image.Pad
                  : (layer.entry.masonryMode
                     ? Image.PreserveAspectCrop
                     : Image.PreserveAspectFit)
        asynchronous: false
        cache: false
        visible: false
    }

    ShaderEffect {
        id: thumbnailShader
        objectName: layer.activePresentation
                    ? "galleryThumbnailShader-" + layer.entry.viewIndex : ""
        readonly property real textureAspect:
            sourceImage.implicitWidth > 0 && sourceImage.implicitHeight > 0
            ? sourceImage.implicitWidth / sourceImage.implicitHeight : 0
        readonly property real fittedWidth:
            layer.entry.masonryMode || textureAspect <= 0
            ? layer.width
            : Math.min(layer.width, layer.height * textureAspect)
        readonly property real fittedHeight:
            layer.entry.masonryMode || textureAspect <= 0
            ? layer.height
            : Math.min(layer.height, layer.width / textureAspect)
        x: (layer.width - width) / 2
        y: (layer.height - height) / 2
        width: fittedWidth
        height: fittedHeight
        property var source: sourceImage
        property size viewportSize: Qt.size(
            width * layer.entry.renderDpr,
            height * layer.entry.renderDpr)
        property real sharpenAmount: 1.5
        property bool showCheckerboard: true
        property int checkerboardSize: 4 * layer.entry.renderDpr
        property real borderRadius: 4.1 * layer.entry.renderDpr
        fragmentShader: "qrc:/ZoinGallery/resources/shader.frag.qsb"
        visible: layer.imageSource.toString() !== ""
    }
}
