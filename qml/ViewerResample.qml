pragma ComponentBehavior: Bound

import QtQuick

ViewerResampleEffect {
    id: root

    // Input is a texture-providing Image, with its decoded physical sourceSize.
    // Only the final pass follows viewport geometry. Half-size levels remain
    // unchanged during pan/zoom and ShaderEffectSource caches their rendering.
    property var imageSource: null
    viewportSize: Qt.size(width, height)
    // Identity belongs to the selected texture, which may already be a half-
    // or quarter-size level. The final vertex stage resolves resting geometry
    // against the actual framebuffer; motion keeps continuous coordinates.
    pixelAlignedIdentity: pixelAligned
        && selectedPixelSize.width > 0 && selectedPixelSize.height > 0
        && Math.abs(selectedPixelSize.width - viewportSize.width) < 0.01
        && Math.abs(selectedPixelSize.height - viewportSize.height) < 0.01

    readonly property size imagePixelSize: imageSource
        ? imageSource.sourceSize : Qt.size(0, 0)
    readonly property string imageKey: imageSource
        ? imageSource.source.toString() : ""
    readonly property int requiredLevels: levelForSize(imagePixelSize,
                                                       viewportSize)
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

    readonly property size selectedPixelSize: source && source !== imageSource
        ? source.textureSize : imagePixelSize

    source: {
        root.pyramidRevision
        const level = requiredLevels > 0
            ? pyramid.itemAt(requiredLevels - 1) : null
        return level ? level.texture : imageSource
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
                return previous ? previous.texture : root.imageSource
            }

            ViewerResampleEffect {
                id: reduction
                width: level.pixelSize.width
                height: level.pixelSize.height
                source: level.inputTexture
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
