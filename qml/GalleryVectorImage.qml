pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import QtQuick.VectorImage
import ZoinGallery.Native 1.0

Item {
    id: root
    property url source
    property bool animate: false
    property bool showCheckerboard: false
    property bool crop: false
    property real devicePixelRatio: 1
    property double loadStartedAt: Date.now()
    property int sceneGenerationMs: 0
    property int sourceStatus: Image.Null
    readonly property real paintedWidth: vectorImage.width
    readonly property real paintedHeight: vectorImage.height
    readonly property real aspect: vectorImage.implicitHeight > 0
        ? vectorImage.implicitWidth / vectorImage.implicitHeight : 1
    readonly property real fittedWidth: crop ? width : Math.min(width, height * aspect)
    readonly property real fittedHeight: crop ? height : Math.min(height, width / aspect)

    // Qt 6.11 exposes asynchronous Shapes but no VectorImage.status. Inspect
    // their preparation state once per pending frame; stop polling at rest.
    function shapesReady(item) {
        let ready = !(item instanceof Shape) || item.status === Shape.Ready
        if (item instanceof Text) item.renderType = Text.CurveRendering
        for (let i = 0; i < item.children.length; ++i) {
            if (!shapesReady(item.children[i])) ready = false
        }
        return ready
    }
    function resolveState() {
        if (source.toString() === "") {
            sourceStatus = Image.Null
        } else if (vectorImage.implicitWidth <= 0 || vectorImage.implicitHeight <= 0) {
            sourceStatus = Image.Error
        } else if (shapesReady(vectorImage)) {
            sourceStatus = Image.Ready
            sceneGenerationMs = Date.now() - loadStartedAt
        } else {
            sourceStatus = Image.Loading
            if (visible) {
                readinessTimer.interval = 16
                readinessTimer.start()
            }
        }
    }
    onSourceChanged: {
        loadStartedAt = Date.now()
        sourceStatus = source.toString() === "" ? Image.Null : Image.Loading
        readinessTimer.interval = 0
        readinessTimer.restart()
    }
    onVisibleChanged: {
        if (visible && sourceStatus === Image.Loading) {
            readinessTimer.interval = 0
            readinessTimer.restart()
        }
        else if (!visible) readinessTimer.stop()
    }
    Component.onCompleted: {
        readinessTimer.interval = 0
        readinessTimer.restart()
    }
    Timer {
        id: readinessTimer
        interval: 16
        onTriggered: root.resolveState()
    }

    VectorSourcePin { source: root.source }

    ShaderEffect {
        objectName: "galleryVectorCheckerboard"
        x: vectorImage.x
        y: vectorImage.y
        width: vectorImage.width
        height: vectorImage.height
        visible: root.showCheckerboard && root.sourceStatus === Image.Ready
        property size viewportSize: Qt.size(width, height)
        fragmentShader: "qrc:/ZoinGallery/resources/vector_checkerboard.frag.qsb"
    }
    VectorImage {
        id: vectorImage
        objectName: "galleryVectorSurface"
        x: Math.round((root.width - width) * root.devicePixelRatio / 2) / root.devicePixelRatio
        y: Math.round((root.height - height) * root.devicePixelRatio / 2) / root.devicePixelRatio
        width: Math.round(root.fittedWidth * root.devicePixelRatio) / root.devicePixelRatio
        height: Math.round(root.fittedHeight * root.devicePixelRatio) / root.devicePixelRatio
        source: root.source
        fillMode: root.crop ? VectorImage.PreserveAspectCrop : VectorImage.Stretch
        preferredRendererType: VectorImage.CurveRenderer
        asynchronousShapes: true
        animations.paused: !root.animate || !root.visible
        visible: root.sourceStatus !== Image.Error && root.sourceStatus !== Image.Null
    }
}
