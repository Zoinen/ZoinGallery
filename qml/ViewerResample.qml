pragma ComponentBehavior: Bound

import QtQuick

ViewerResampleEffect {
    id: root

    // Input is a texture-providing Image, with its decoded physical sourceSize.
    // Only the final pass follows viewport geometry. Half-size levels remain
    // unchanged during pan/zoom and ShaderEffectSource caches their rendering.
    property var imageSource: null
    property var videoFrameSource: null
    property bool cacheVideoPresentation: true
    property bool cacheVideoConversion: true
    property real presentationGeometryRevision: 0
    readonly property real presentationDpr: Window.window
        ? Window.window.devicePixelRatio : 0
    readonly property bool presentationCacheEligible: {
        // A live layer retains the finished filter result, not a lower-quality
        // approximation. Only admit exact 1:1 opaque, resting composition.
        // Unknown transforms, fades and screen/backing-DPR mismatches use the
        // original direct path. Never allocate an unbounded zoom-sized cache.
        if (!visible || hardwareSampling || !cacheVideoPresentation || !videoFrameSource
                || !videoFrameSource.hasFrame || !pixelAligned
                || presentationDpr <= 0 || width <= 0 || height <= 0)
            return false
        let dependency = presentationGeometryRevision
        let ancestor = root
        while (ancestor) {
            dependency += ancestor.x + ancestor.y + ancestor.width
                + ancestor.height + ancestor.scale + ancestor.rotation
            if (ancestor.opacity !== 1)
                return false
            ancestor = ancestor.parent
        }
        if (!Number.isFinite(dependency))
            return false
        const dpr = presentationDpr
        // These reads also invalidate eligibility when the window is resized.
        dependency += Window.window.width + Window.window.height
        if (!windowProjectionMatches(dpr))
            return false
        const w = width * dpr
        const h = height * dpr
        const integral = value => Math.abs(value - Math.round(value)) < 0.0001
        if (!integral(w) || !integral(h) || w > 4096 || h > 4096
                || w * h > 16 * 1024 * 1024
                || Math.abs(viewportSize.width - w) > 0.0001
                || Math.abs(viewportSize.height - h) > 0.0001)
            return false
        const origin = root.mapToItem(null, 0, 0)
        const unitX = root.mapToItem(null, 1, 0)
        const unitY = root.mapToItem(null, 0, 1)
        return integral(origin.x * dpr) && integral(origin.y * dpr)
            && Math.abs(unitX.x - origin.x - 1) < 0.0001
            && Math.abs(unitX.y - origin.y) < 0.0001
            && Math.abs(unitY.x - origin.x) < 0.0001
            && Math.abs(unitY.y - origin.y - 1) < 0.0001
    }
    layer.enabled: presentationCacheEligible
    layer.live: true
    layer.textureSize: Qt.size(Math.round(width * presentationDpr),
                               Math.round(height * presentationDpr))
    layer.smooth: false
    layer.mipmap: false
    layer.samples: 0
    layer.format: ShaderEffectSource.RGBA8
    // Qt's ordinary layer quad does not apply our resting framebuffer-grid
    // correction when logical window size * DPR has been rounded. Composite
    // exact cache texels through the same vertex correction instead.
    layer.effect: Component {
        ViewerResampleEffect {
            objectName: "galleryViewerVideoCacheComposite"
            viewportSize: root.viewportSize
            pixelAligned: true
            pixelAlignedIdentity: true
            nearestNeighbor: true
        }
    }
    viewportSize: Qt.size(width, height)
    // Identity belongs to the selected texture, which may already be a half-
    // or quarter-size level. The final vertex stage resolves resting geometry
    // against the actual framebuffer; motion keeps continuous coordinates.
    pixelAlignedIdentity: pixelAligned
        && selectedPixelSize.width > 0 && selectedPixelSize.height > 0
        && Math.abs(selectedPixelSize.width - viewportSize.width) < 0.01
        && Math.abs(selectedPixelSize.height - viewportSize.height) < 0.01

    readonly property size imagePixelSize: videoFrameSource
        && videoFrameSource.hasFrame ? videoFrameSource.frameSize
        : (imageSource ? imageSource.sourceSize : Qt.size(0, 0))
    readonly property string imageKey: videoFrameSource
        && videoFrameSource.hasFrame ? "video-frame-source"
        : (imageSource ? imageSource.source.toString() : "")
    readonly property int naturalLevels: levelForSize(imagePixelSize, viewportSize)
    readonly property bool preferDirectVideoSampling: {
        if (!videoFrameSource || !videoFrameSource.hasFrame || naturalLevels !== 1)
            return false
        // A single pyramid level adds a full render pass. For moderate video
        // reductions a single wider filter is cheaper; retain pyramids for
        // deeper reductions where they substantially shrink the filter footprint.
        const scaleX = imagePixelSize.width / viewportSize.width
        const scaleY = imagePixelSize.height / viewportSize.height
        return scaleX <= 2.6 && scaleY <= 2.6
    }
    readonly property int requiredLevels: hardwareSampling || nearestNeighbor
        || preferDirectVideoSampling ? 0 : naturalLevels
    property int retainedLevels: 0
    property int pyramidRevision: 0

    function levelForSize(inputSize, outputSize) {
        if (inputSize.width <= 0 || inputSize.height <= 0
                || outputSize.width <= 0 || outputSize.height <= 0)
            return 0
        let w = inputSize.width
        let h = inputSize.height
        let level = 0
        // A 1-pixel axis no longer needs reduction (e.g. a scanline image).
        while (level < 24 && (w > 1 || h > 1)) {
            const nextW = Math.max(1, Math.floor(w / 2))
            const nextH = Math.max(1, Math.floor(h / 2))
            if ((w > 1 && nextW < outputSize.width)
                    || (h > 1 && nextH < outputSize.height))
                break
            w = nextW
            h = nextH
            ++level
        }
        return level
    }

    function retainRequiredLevels() {
        retainedLevels = Math.max(retainedLevels, requiredLevels)
    }

    function resetPyramid() {
        retainedLevels = 0
        retainRequiredLevels()
    }

    onImageSourceChanged: resetPyramid()
    onImageKeyChanged: resetPyramid()
    onImagePixelSizeChanged: resetPyramid()
    onRequiredLevelsChanged: retainRequiredLevels()
    Component.onCompleted: retainRequiredLevels()

    readonly property size selectedPixelSize: {
        const textureSize = source && source !== imageSource
            ? source.textureSize : null
        return textureSize && textureSize.width > 0 && textureSize.height > 0
            ? textureSize : imagePixelSize
    }

    source: {
        root.pyramidRevision
        const level = requiredLevels > 0
            ? pyramid.itemAt(requiredLevels - 1) : null
        return level ? level.texture : imageSource
    }
    videoSource: requiredLevels > 0 || !videoFrameSource
        || !videoFrameSource.hasFrame ? null : videoFrameSource
    // Conversion storage is float32, never an encoded/quantized surrogate.
    // Keep it source-sized and bounded. Unlike the presentation cache, source
    // conversion is independent of scene motion, opacity and pixel alignment.
    // Native/nearest and unsupported devices retain the direct path. Only the
    // active raw-video pass reserves the shared conversion: the final effect
    // for direct sampling, or the first reduction for a quality pyramid.
    readonly property ViewerResampleEffect linearVideoCacheConsumer: {
        root.pyramidRevision
        const first = requiredLevels > 0 ? pyramid.itemAt(0) : null
        return first ? first.resampler : root
    }
    readonly property bool linearVideoCacheRequested: cacheVideoConversion
        && !hardwareSampling && !referenceSampling && videoFrameSource && videoFrameSource.hasFrame
        && isFinite(viewportSize.width) && isFinite(viewportSize.height)
        && viewportSize.width > 0 && viewportSize.height > 0
        && !nearestNeighbor
        && imagePixelSize.width > 0 && imagePixelSize.height > 0
        && imagePixelSize.width <= 4096 && imagePixelSize.height <= 4096
        && imagePixelSize.width * imagePixelSize.height <= 9 * 1024 * 1024
        && imagePixelSize.width >= viewportSize.width * 1.05
        && imagePixelSize.height >= viewportSize.height * 1.05
    readonly property bool linearVideoCacheEligible: linearVideoCacheRequested
        && linearVideoCacheConsumer
        && linearVideoCacheConsumer.linearVideoCacheSupported
        && linearVideoCacheConsumer.linearVideoCacheSize.width === imagePixelSize.width
        && linearVideoCacheConsumer.linearVideoCacheSize.height === imagePixelSize.height
    requestLinearVideoCache: linearVideoCacheRequested && requiredLevels === 0
    linearVideoSource: conversionLoader.status === Loader.Ready && conversionLoader.item
        ? conversionLoader.item.texture : null

    Loader {
        id: conversionLoader
        active: root.linearVideoCacheEligible
        visible: false
        sourceComponent: Component {
            Item {
                readonly property alias texture: conversion
                ViewerResampleEffect {
                    id: conversion
                    objectName: "galleryViewerVideoLinearConversion"
                    width: root.imagePixelSize.width
                    height: root.imagePixelSize.height
                    viewportSize: root.imagePixelSize
                    videoSource: root.videoFrameSource
                    convertVideoToLinear: true
                    intermediate: true
                }
            }
        }
    }

    Repeater {
        id: pyramid
        model: root.retainedLevels
        onItemAdded: root.pyramidRevision++
        onItemRemoved: root.pyramidRevision++

        delegate: Item {
            id: level
            required property int index
            visible: false

            readonly property alias texture: levelTexture
            readonly property alias resampler: reduction
            readonly property size inputPixelSize: Qt.size(
                Math.max(1, Math.floor(root.imagePixelSize.width
                                      / Math.pow(2, index))),
                Math.max(1, Math.floor(root.imagePixelSize.height
                                      / Math.pow(2, index))))
            readonly property size pixelSize: Qt.size(
                Math.max(1, Math.floor(root.imagePixelSize.width
                                     / Math.pow(2, index + 1))),
                Math.max(1, Math.floor(root.imagePixelSize.height
                                     / Math.pow(2, index + 1))))
            readonly property var inputTexture: {
                root.pyramidRevision
                const previous = index > 0 ? pyramid.itemAt(index - 1) : null
                if (previous)
                    return previous.texture
                return root.videoFrameSource && root.videoFrameSource.hasFrame
                    ? null : root.imageSource
            }

            ViewerResampleEffect {
                id: reduction
                objectName: "galleryViewerVideoReduction" + index
                width: level.pixelSize.width
                height: level.pixelSize.height
                source: level.inputTexture
                videoSource: index === 0 && root.videoFrameSource
                    && root.videoFrameSource.hasFrame
                    ? root.videoFrameSource : null
                requestLinearVideoCache: index === 0 && root.requiredLevels > 0
                    && root.linearVideoCacheRequested
                linearVideoSource: index === 0 ? root.linearVideoSource : null
                viewportSize: level.pixelSize
                // Preserve the exact two-source-pixel sampling grid on odd
                // axes; the trailing source pixel is outside this half level.
                sourceExtent: Qt.size(
                    Math.min(level.inputPixelSize.width, level.pixelSize.width * 2),
                    Math.min(level.inputPixelSize.height, level.pixelSize.height * 2))
                intermediate: true
            }

            ShaderEffectSource {
                id: levelTexture
                sourceItem: reduction
                sourceRect: Qt.rect(0, 0, level.pixelSize.width,
                                   level.pixelSize.height)
                textureSize: level.pixelSize
                format: ShaderEffectSource.RGBA8
                hideSource: true
                live: true
                mipmap: false
                smooth: true
            }
        }
    }
}
