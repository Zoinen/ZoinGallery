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
    readonly property real dpr: Math.max(0.01, viewport.devicePixelRatio)
    // Materialize the host transform revision as a local notifying property.
    // Accessing it only through the required `var` inside a JS helper can make
    // one component of a point binding retain its cached dependency set.
    readonly property real alignmentRevision: viewport.pixelAlignmentRevision
    property int alignmentEpoch: 0
    readonly property bool moving: viewport.viewportAnimationRunning
                                   || viewport.zoomScrollingAnimationRunning
                                   || viewport.directManipulationActive
                                   || viewport.externalTransformMoving
                                   || viewport.isRotating
    readonly property bool quarterTurn: Math.abs(viewport.rotationMode % 2) === 1
    readonly property bool centeredX: !viewport.isRotating && viewport.zoomScale <= 1
        && width <= viewport.width + 0.0001
        && Math.abs(x + width / 2 - viewport.width / 2) * dpr < 0.001
    readonly property bool centeredY: !viewport.isRotating && viewport.zoomScale <= 1
        && height <= viewport.height + 0.0001
        && Math.abs(y + height / 2 - viewport.height / 2) * dpr < 0.001
    function scheduleAlignmentRefresh() {
        alignmentRefreshTimer.restart()
    }

    onAlignmentRevisionChanged: scheduleAlignmentRefresh()
    Component.onCompleted: scheduleAlignmentRefresh()

    Timer {
        id: alignmentRefreshTimer
        interval: 0
        repeat: false
        onTriggered: root.alignmentEpoch++
    }

    function snapExtent(value) {
        return value > 0 ? Math.max(1, Math.round(value * dpr)) / dpr : 0
    }

    function geometryDependency(item) {
        // mapToItem does not create geometry dependencies. Track the complete
        // ancestor chain. The viewport supplies navigation's Translate value
        // explicitly because QQuickItem::transform is a non-notifying list.
        let dependency = alignmentEpoch
        let ancestor = item
        while (ancestor) {
            dependency += ancestor.x + ancestor.y + ancestor.width
                    + ancestor.height + ancestor.scale + ancestor.rotation
            ancestor = ancestor.parent
        }
        return dependency
    }

    function scenePoint(item, x, y) {
        // QML tracks point subproperties independently. Carry the synthetic
        // transform-list dependency into both coordinates so a Y-only
        // ancestor translation cannot leave the vertical correction stale.
        const dependency = geometryDependency(item)
        if (!Number.isFinite(dependency))
            return Qt.point(0, 0)
        const mapped = item.mapToItem(null, x, y)
        return mapped
    }

    function centerParity(horizontal) {
        // A one-pixel terminal extent deliberately leaves even source parity.
        if ((horizontal ? root.width : root.height) * dpr <= 1)
            return 1
        const sourceWidth = horizontal !== quarterTurn
        return Math.round((sourceWidth ? viewport.originalSize.width
                                      : viewport.originalSize.height) * dpr) % 2
    }

    function stableCenter(horizontal) {
        const viewportCenter = scenePoint(viewport,
                                          viewport.width / 2,
                                          viewport.height / 2)
        const center = horizontal ? viewportCenter.x : viewportCenter.y
        const halfParity = centerParity(horizontal) / 2
        return (Math.round(center * dpr - halfParity) + halfParity) / dpr
    }

    function imageExtent(value, sourceHorizontal) {
        if (moving)
            return value
        // At native scale, clipping a fractional viewport edge must not turn
        // a one-source-pixel-to-one-screen-pixel presentation into minification.
        if (viewport.zoomScale === 1)
            return snapExtent(value)
        const horizontal = sourceHorizontal !== quarterTurn
        if (!(horizontal ? centeredX : centeredY))
            return snapExtent(value)
        const pixels = value * dpr
        if (pixels <= 1)
            return value > 0 ? 1 / dpr : 0
        const parity = centerParity(horizontal)
        // Fixed parity keeps both integer-pixel edges symmetric around the
        // same center, including odd/even transitions during minification.
        const rounded = 2 * Math.round((pixels - parity) / 2) + parity
        const center = stableCenter(horizontal)
        const viewportOrigin = scenePoint(viewport, 0, 0)
        const viewportEnd = scenePoint(viewport, viewport.width, viewport.height)
        const first = horizontal ? viewportOrigin.x : viewportOrigin.y
        const last = horizontal ? viewportEnd.x : viewportEnd.y
        const available = 2 * Math.min(center - Math.min(first, last),
                                        Math.max(first, last) - center) * dpr
        const maximum = 2 * Math.floor((available - parity) / 2) + parity
        return Math.max(1, Math.min(Math.max(parity || 2, rounded), maximum)) / dpr
    }

    function alignedPosition(parentItem, x, y, width, height, rotation,
                             centerX, centerY, continuous) {
        // Include rotation about the item's center before choosing the nearest
        // physical-pixel origin, then express the correction in parent space.
        const radians = rotation * Math.PI / 180
        const originX = x + width / 2 - Math.cos(radians) * width / 2
                + Math.sin(radians) * height / 2
        const originY = y + height / 2 - Math.sin(radians) * width / 2
                - Math.cos(radians) * height / 2
        const scene = scenePoint(parentItem, originX, originY)
        const center = scenePoint(parentItem, x + width / 2, y + height / 2)
        const targetX = centerX ? scene.x + stableCenter(true) - center.x
                                : continuous ? scene.x : Math.round(scene.x * dpr) / dpr
        const targetY = centerY ? scene.y + stableCenter(false) - center.y
                                : continuous ? scene.y : Math.round(scene.y * dpr) / dpr
        const corrected = parentItem.mapFromItem(null,
                                                 targetX, targetY)
        return Qt.point(x + corrected.x - originX, y + corrected.y - originY)
    }

    opacity: viewport.imageTextureReady ? 1 : 0
    width: viewport.animatedEffectiveWidth * viewport.zoomScale
    height: viewport.animatedEffectiveHeight * viewport.zoomScale

    Item {
        id: unrotatedContent
        function alignedCoordinate(horizontal, epoch) {
            // Resolve each value-type component in its own binding. Transform
            // list entries do not notify mapToItem(), and QML can otherwise
            // retain one stale component after sequential X/Y updates.
            if (!Number.isFinite(epoch))
                return 0
            const aligned = root.alignedPosition(
                root, (root.width - width) / 2, (root.height - height) / 2,
                width, height, rotation, root.centeredX, root.centeredY,
                root.moving)
            return horizontal ? aligned.x : aligned.y
        }
        x: alignedCoordinate(true, root.alignmentEpoch)
        y: alignedCoordinate(false, root.alignmentEpoch)
        width: root.imageExtent(root.viewport.originalSize.width
                                * root.viewport.zoomScale, true)
        height: root.imageExtent(root.viewport.originalSize.height
                                 * root.viewport.zoomScale, false)
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
            mipmap: root.viewport.sphericTextureMipmapsEnabled
            asynchronous: true
            property int fromIndex: -1
            visible: false
        }

        ViewerResample {
            id: imageShader
            objectName: "galleryViewerImageShader"
            anchors.fill: parent
            // A completed native request is retained for later zooming. Fit
            // must still use its prepared tier when that tier covers output.
            readonly property bool preparedFitReady: root.viewport.zoomFitView
                && root.fromLevel === 1 && baseImage.status === Image.Ready
                && baseImage.sourceSize.width + 1 >= viewportSize.width
                && baseImage.sourceSize.height + 1 >= viewportSize.height
            imageSource: preparedFitReady || nativeImage.status !== Image.Ready
                         ? baseImage : nativeImage
            viewportSize: Qt.size(
                width * root.viewport.devicePixelRatio,
                height * root.viewport.devicePixelRatio)
            pixelAligned: !root.moving
            showCheckerboard: root.viewport.checkerboardEnabled
                             && root.viewport.imageTextureReady
            checkerboardSize: 4 * root.viewport.devicePixelRatio
            borderRadius: 0

            Image {
                id: cropImage
                objectName: "galleryViewerCropImage"
                cache: false
                mipmap: false
                property real unscaledX
                property real unscaledY
                property real unscaledWidth
                property real unscaledHeight
                function alignedCoordinate(horizontal, epoch) {
                    if (!Number.isFinite(epoch))
                        return 0
                    const aligned = root.alignedPosition(
                        parent, unscaledX * root.viewport.zoomScale,
                        unscaledY * root.viewport.zoomScale, width, height, 0,
                        false, false, root.moving)
                    return horizontal ? aligned.x : aligned.y
                }
                x: alignedCoordinate(true, root.alignmentEpoch)
                y: alignedCoordinate(false, root.alignmentEpoch)
                width: root.moving ? unscaledWidth * root.viewport.zoomScale
                    : root.snapExtent(unscaledWidth * root.viewport.zoomScale)
                height: root.moving ? unscaledHeight * root.viewport.zoomScale
                    : root.snapExtent(unscaledHeight * root.viewport.zoomScale)
                property int fromIndex: -1
                visible: false
            }

            ViewerResample {
                id: cropShader
                objectName: "galleryViewerCropShader"
                x: cropImage.x
                y: cropImage.y
                width: cropImage.width
                height: cropImage.height
                imageSource: cropImage
                pixelAligned: !root.moving
                viewportSize: Qt.size(width * root.dpr, height * root.dpr)
                showCheckerboard: imageShader.showCheckerboard
                checkerboardSize: imageShader.checkerboardSize
                checkerboardOffset: Qt.vector2d(x * root.dpr, y * root.dpr)
                visible: nativeImage.status !== Image.Ready
                         && cropImage.status === Image.Ready
                         && !root.viewport.zoomFitView
            }
        }
    }
}
