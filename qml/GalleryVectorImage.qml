pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import ZoinGallery.Native 1.0

Item {
    id: root
    property url source
    property bool animate: false
    property bool prepareWhenHidden: false
    property bool showCheckerboard: false
    property bool crop: false
    property real devicePixelRatio: 1
    property double loadStartedAt: Date.now()
    property int sceneGenerationMs: 0
    property int sourceStatus: Image.Null
    readonly property real paintedWidth: vectorImage.width
    readonly property real paintedHeight: vectorImage.height
    readonly property real aspect: sourcePin.sourceSize.height > 0
        ? sourcePin.sourceSize.width / sourcePin.sourceSize.height : 1
    readonly property real fittedWidth: crop ? Math.max(width, height * aspect) : Math.min(width, height * aspect)
    readonly property real fittedHeight: crop ? Math.max(height, width / aspect) : Math.min(height, width / aspect)

    // Component compilation/incubation and shape preparation are separate
    // stages. Inspect pending Shapes only until their geometry is ready.
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
        } else if (sourcePin.failed || vectorImage.status === Loader.Error) {
            sourceStatus = Image.Error
        } else if (vectorImage.status === Loader.Ready && shapesReady(vectorImage.item)) {
            sourceStatus = Image.Ready
            sceneGenerationMs = Date.now() - loadStartedAt
        } else {
            sourceStatus = Image.Loading
            if (visible || prepareWhenHidden) {
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
        else if (!visible && !prepareWhenHidden) readinessTimer.stop()
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

    VectorSourcePin {
        id: sourcePin
        source: root.source
        prepareComponent: true
        onPreparedChanged: {
            readinessTimer.interval = 0
            readinessTimer.restart()
        }
    }

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
    Loader {
        id: vectorImage
        objectName: "galleryVectorSurface"
        x: Math.round((root.width - width) * root.devicePixelRatio / 2) / root.devicePixelRatio
        y: Math.round((root.height - height) * root.devicePixelRatio / 2) / root.devicePixelRatio
        width: Math.round(root.fittedWidth * root.devicePixelRatio) / root.devicePixelRatio
        height: Math.round(root.fittedHeight * root.devicePixelRatio) / root.devicePixelRatio
        asynchronous: true
        sourceComponent: sourcePin.component
        // The generated root exposes Qt's animation controls. Retain the
        // authored loop count and freeze panel/hidden scenes before painting.
        property QtObject animations: QtObject {
            property bool paused: !root.animate || !root.visible
            readonly property int loops: vectorImage.item ? vectorImage.item.loops : 1
        }
        onStatusChanged: root.resolveState()
        visible: root.sourceStatus !== Image.Error && root.sourceStatus !== Image.Null
    }
    Binding {
        target: vectorImage.item
        property: "paused"
        value: vectorImage.animations.paused
        when: vectorImage.status === Loader.Ready
    }
}
