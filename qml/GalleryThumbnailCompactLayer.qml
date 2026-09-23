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
        x: Math.round((parent.width - width) * layer.entry.renderDpr / 2)
           / layer.entry.renderDpr
        y: 0
        width: layer.entry.compactImageContentWidth
        height: parent.height
        source: layer.imageSource
        sourceSize: Qt.size(
            Math.max(1, Math.round(width * layer.entry.renderDpr)),
            Math.max(1, Math.round(height * layer.entry.renderDpr)))
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        cache: false
        smooth: true
        visible: source.toString() !== ""
    }
}
