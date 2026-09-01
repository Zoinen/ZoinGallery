pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    visible: false

    required property var viewport

    function clearNativeTier() {
        viewport.viewerImage2.source = ""
        viewport.viewerImage2.fromIndex = -1
        viewport.viewerImageCrop.source = ""
        viewport.viewerImageCrop.fromIndex = -1
    }

    function resetViewerImages() {
        delayedIdSetter.stop()
        viewport.sourceSizeFallbackPending = false
        viewport.originalSize = Qt.size(0, 0)
        viewport.image.source = ""
        viewport.image.fromIndex = -1
        viewport.image.fromLevel = -1
        clearNativeTier()
    }

    function remapImageIndex(oldIndex, newIndex) {
        if (oldIndex === newIndex)
            return
        if (viewport.image.fromIndex === oldIndex)
            viewport.image.fromIndex = newIndex
        if (viewport.viewerImage2.fromIndex === oldIndex)
            viewport.viewerImage2.fromIndex = newIndex
        if (viewport.viewerImageCrop.fromIndex === oldIndex)
            viewport.viewerImageCrop.fromIndex = newIndex
    }

    function applyOriginalSize(nextOriginalSize) {
        const sizeChanged = Math.abs(viewport.originalSize.width
                                     - nextOriginalSize.width) > 0.5
                || Math.abs(viewport.originalSize.height
                            - nextOriginalSize.height) > 0.5
        viewport.originalSize = nextOriginalSize
        if (sizeChanged && viewport.zoomFitView)
            viewport.zoomToFit(true)
    }

    function applyBaseTier(imageIdUrl, fromIndex, level) {
        const indexChanged = fromIndex !== viewport.image.fromIndex
        const replacesFitTier = level === 1
                && viewport.image.fromLevel === 0
        const sourceChanged = viewport.image.source !== imageIdUrl
        if ((level === 0 && !indexChanged)
                || (level === 1 && !indexChanged
                    && !replacesFitTier && !sourceChanged))
            return
        viewport.image.source = imageIdUrl
        viewport.image.fromIndex = fromIndex
        viewport.image.fromLevel = level
        if (viewport.viewerImage2.fromIndex !== fromIndex)
            clearNativeTier()
    }

    function nativeCropGeometry(targetX, targetY, scale, originalSize) {
        const x = -targetX / scale
        const y = -targetY / scale
        const width = viewport.width / scale
        const height = viewport.height / scale
        const originalWidth = originalSize.width / viewport.devicePixelRatio
        const originalHeight = originalSize.height / viewport.devicePixelRatio
        if (viewport.rotationMode === 1)
            return Qt.rect(y, originalHeight - (x + width), height, width)
        if (viewport.rotationMode === 2)
            return Qt.rect(originalWidth - (x + width),
                           originalHeight - (y + height), width, height)
        if (viewport.rotationMode === 3)
            return Qt.rect(originalWidth - (y + height), x, height, width)
        return Qt.rect(x, y, width, height)
    }

    function applyNativeTier(imageIdUrl, originalSize, fromIndex) {
        if (fromIndex === viewport.viewerImage2.fromIndex
                && viewport.viewerImage2.source === imageIdUrl)
            return

        const targetX = viewport.viewportAnimation.running
                ? viewport.xAnimation.to : viewport.image.x
        const targetY = viewport.viewportAnimation.running
                ? viewport.yAnimation.to : viewport.image.y
        const scale = viewport.zoomAnimation.to
        if (viewport.width > viewport.effectiveOriginalSize.width * scale
                || viewport.height
                   > viewport.effectiveOriginalSize.height * scale) {
            clearNativeTier()
        } else {
            const crop = nativeCropGeometry(targetX, targetY, scale,
                                            originalSize)
            viewport.viewerImageCrop.unscaledX = crop.x
            viewport.viewerImageCrop.unscaledY = crop.y
            viewport.viewerImageCrop.unscaledWidth = crop.width
            viewport.viewerImageCrop.unscaledHeight = crop.height
            viewport.viewerImageCrop.source = imageIdUrl + "/"
                    + Math.round(crop.x * viewport.devicePixelRatio) + ","
                    + Math.round(crop.y * viewport.devicePixelRatio) + ","
                    + Math.round(crop.width * viewport.devicePixelRatio) + ","
                    + Math.round(crop.height * viewport.devicePixelRatio)
            viewport.viewerImage2.fromIndex = fromIndex
        }
        delayedIdSetter.idToSet = imageIdUrl
        delayedIdSetter.restart()
    }

    function applyImageSize(originalSize) {
        if (!viewport.sourceSizeFallbackPending) {
            applyOriginalSize(Qt.size(
                originalSize.width / viewport.devicePixelRatio,
                originalSize.height / viewport.devicePixelRatio))
        } else if (viewport.viewerImageBase.status === Image.Ready
                   && viewport.viewerImageBase.sourceSize.width > 1
                   && viewport.viewerImageBase.sourceSize.height > 1) {
            applyOriginalSize(Qt.size(
                viewport.viewerImageBase.sourceSize.width
                    / viewport.devicePixelRatio,
                viewport.viewerImageBase.sourceSize.height
                    / viewport.devicePixelRatio))
            viewport.sourceSizeFallbackPending = false
        } else {
            applyOriginalSize(Qt.size(0, 0))
        }
    }

    function setImage(imageIdUrl, originalSize, fromIndex, level) {
        viewport.animateRotation = false
        delayedIdSetter.stop()
        viewport.sourceSizeFallbackPending = originalSize.width <= 1
                || originalSize.height <= 1
        if (level === 0 || level === 1)
            applyBaseTier(imageIdUrl, fromIndex, level)
        else if (level === 2)
            applyNativeTier(imageIdUrl, originalSize, fromIndex)
        applyImageSize(originalSize)
    }

    function applySimpleSource() {
        if (viewport.simpleSource.toString() === "") {
            resetViewerImages()
            viewport.appliedSimpleSourceIndex = -1
            viewport.simpleSourceMetadataKnown = false
            return
        }
        const indexChanged = viewport.appliedSimpleSourceIndex
                !== viewport.simpleSourceIndex
        if (indexChanged)
            viewport.simpleSourceMetadataKnown = false
        delayedIdSetter.stop()
        viewport.sourceSizeFallbackPending =
                viewport.simpleSourceOriginalSize.width <= 1
                || viewport.simpleSourceOriginalSize.height <= 1
        clearNativeTier()
        viewport.image.source = viewport.simpleSource
        viewport.image.fromIndex = viewport.simpleSourceIndex
        viewport.image.fromLevel = 0
        viewport.appliedSimpleSourceIndex = viewport.simpleSourceIndex
        if (viewport.simpleSourceOriginalSize.width <= 1
                || viewport.simpleSourceOriginalSize.height <= 1)
            return

        const metadataBecameKnown = !viewport.simpleSourceMetadataKnown
        const hadKnownSize = viewport.originalSize.width > 1
                && viewport.originalSize.height > 1
        applyOriginalSize(Qt.size(
            viewport.simpleSourceOriginalSize.width / viewport.devicePixelRatio,
            viewport.simpleSourceOriginalSize.height / viewport.devicePixelRatio))
        viewport.simpleSourceMetadataKnown = true
        if (indexChanged || metadataBecameKnown || !hadKnownSize)
            viewport.zoomToFit(true)
    }

    Connections {
        target: root.viewport.viewerModel
        function onViewerReset() { root.resetViewerImages() }
    }

    Timer {
        id: delayedIdSetter
        property string idToSet
        interval: root.viewport.animationDuration
        onTriggered: root.viewport.viewerImage2.source = idToSet
    }
}
