pragma ComponentBehavior: Bound

import QtQuick

ShaderEffect {
    id: root

    // Input is a texture-providing Image, with its decoded physical sourceSize.
    // Only the final pass follows viewport geometry. Half-size levels remain
    // unchanged during pan/zoom and ShaderEffectSource caches their rendering.
    property var imageSource: null
    property size viewportSize: Qt.size(width, height)
    property size sourceExtent: Qt.size(0, 0)
    property vector2d checkerboardOffset: Qt.vector2d(0, 0)
    property bool showCheckerboard: false
    property int checkerboardSize: 4
    property real borderRadius: 0
    readonly property bool intermediate: false
    // Native 1:1 presentation may fetch exact texels once its scene geometry
    // has settled on the physical pixel grid. Keep this false during motion.
    property bool pixelAlignedIdentity: false

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

    property var source: {
        root.pyramidRevision
        const level = requiredLevels > 0
            ? pyramid.itemAt(requiredLevels - 1) : null
        return level ? level.texture : imageSource
    }
    fragmentShader: "qrc:/ZoinGallery/resources/viewer_resample.frag.qsb"

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

            ShaderEffect {
                id: reduction
                width: level.pixelSize.width
                height: level.pixelSize.height
                property var source: level.inputTexture
                property size viewportSize: level.pixelSize
                // Preserve the exact two-source-pixel sampling grid on odd
                // axes; the trailing source pixel is outside this half level.
                property size sourceExtent: Qt.size(
                    Math.min(level.inputPixelSize.width, level.pixelSize.width * 2),
                    Math.min(level.inputPixelSize.height, level.pixelSize.height * 2))
                property vector2d checkerboardOffset: Qt.vector2d(0, 0)
                property bool showCheckerboard: false
                property int checkerboardSize: 4
                property real borderRadius: 0
                property bool intermediate: true
                property bool pixelAlignedIdentity: false
                fragmentShader: root.fragmentShader
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
