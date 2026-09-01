pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property var viewport
    property string source: ""
    property int fromIndex: -1
    property int fromLevel: -1

    readonly property alias unrotatedContent: unrotatedContent
    readonly property alias baseImage: baseImage
    readonly property alias nativeImage: nativeImage
    readonly property alias cropImage: cropImage
    readonly property alias shader: imageShader

    opacity: viewport.imageTextureReady ? 1 : 0
    width: viewport.animatedEffectiveWidth * viewport.zoomScale
    height: viewport.animatedEffectiveHeight * viewport.zoomScale

    Item {
        id: unrotatedContent
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: root.viewport.originalSize.width * root.viewport.zoomScale
        height: root.viewport.originalSize.height * root.viewport.zoomScale
        rotation: root.viewport.rotationMode * 90

        onRotationChanged: {
            if (root.viewport.isRotating)
                root.viewport.updatePinnedPosition()
        }

        Behavior on rotation {
            enabled: root.viewport.animateRotation
            RotationAnimation {
                duration: root.viewport.animationDuration
                direction: RotationAnimation.Shortest
                easing.type: Easing.InOutQuad
            }
        }

        Image {
            id: baseImage
            objectName: "galleryViewerBaseImage"
            anchors.fill: parent
            source: root.source
            cache: false
            mipmap: false
            asynchronous: false
            visible: false

            onStatusChanged: {
                if (status !== Image.Ready
                        || !root.viewport.sourceSizeFallbackPending
                        || sourceSize.width <= 1 || sourceSize.height <= 1)
                    return
                const preserveFit = root.viewport.zoomFitView
                root.viewport.applyOriginalSize(Qt.size(
                    sourceSize.width / root.viewport.devicePixelRatio,
                    sourceSize.height / root.viewport.devicePixelRatio))
                root.viewport.sourceSizeFallbackPending = false
                root.viewport.simpleSourceMetadataKnown = false
                if (!preserveFit)
                    root.viewport.fitViewerImageInViewportBounds()
            }
        }

        Image {
            id: nativeImage
            objectName: "galleryViewerNativeImage"
            anchors.fill: parent
            cache: false
            mipmap: true
            asynchronous: true
            property int fromIndex: -1
            visible: false
        }

        ShaderEffect {
            id: imageShader
            anchors.fill: parent
            property var source: nativeImage.status === Image.Ready
                                 ? nativeImage : baseImage
            property var viewportSize: Qt.size(
                width * root.viewport.devicePixelRatio,
                height * root.viewport.devicePixelRatio)
            property real sharpenAmount: root.viewport.zoomScale < 1 ? 1.5 : 0
            property bool showCheckerboard: root.viewport.checkerboardEnabled
                                                && root.viewport.imageTextureReady
            property int checkerboardSize: 4 * root.viewport.devicePixelRatio
            property int borderRadius: 0

            fragmentShader: "qrc:/ZoinGallery/resources/shader.frag.qsb"

            Image {
                id: cropImage
                cache: false
                property real unscaledX
                property real unscaledY
                property real unscaledWidth
                property real unscaledHeight
                x: unscaledX * root.viewport.zoomScale
                y: unscaledY * root.viewport.zoomScale
                width: unscaledWidth * root.viewport.zoomScale
                height: unscaledHeight * root.viewport.zoomScale
                property int fromIndex: -1
                visible: nativeImage.status !== Image.Ready
            }
        }
    }
}
