pragma ComponentBehavior: Bound
import QtQuick
import ZoinGallery.Native 1.0

GallerySceneItem {
    id: cache
    accessibilityLeaf: true
    objectName: "galleryVectorSceneCache"
    property int idleLimit: 16
    property int generation: 0
    property var surfaces: []

    function acquire(source, host) {
        const key = source.toString()
        for (const surface of surfaces) {
            if (!surface.host && surface.generation === generation
                    && surface.requestedSource.toString() === key) {
                surface.host = host
                surface.parent = host
                surfaces = surfaces.filter(item => item !== surface)
                surfaces.push(surface)
                return surface
            }
        }
        // QObject ownership stays with the cache when the visual parent moves.
        const surface = factory.createObject(cache, {
            requestedSource: source, host: host, generation: generation
        })
        surface.parent = host
        surfaces.push(surface)
        admission.running = true
        return surface
    }

    function release(surface) {
        if (!surface) return
        surface.idleSize = Qt.size(surface.width, surface.height)
        surface.idleDpr = surface.devicePixelRatio
        surface.idleCrop = surface.crop
        surface.idleCheckerboard = surface.showCheckerboard
        surface.host = null
        surface.parent = cache
        // Pending requests must not parse after their consumer leaves view.
        if (surface.source.toString() === "" || surface.sourceStatus === Image.Error
                || surface.generation !== generation) {
            discard(surface)
        }
        let idle = surfaces.filter(item => !item.host)
        while (idle.length > idleLimit) discard(idle.shift())
    }

    function discard(surface) {
        surfaces = surfaces.filter(item => item !== surface)
        surface.visible = false
        surface.destroy()
    }

    function invalidate() {
        ++generation
        for (const surface of surfaces.slice()) {
            if (!surface.host) discard(surface)
        }
    }

    FrameAnimation {
        id: admission
        paused: !cache.enabled || !cache.visible
        // One synchronous VectorImage source assignment per animation frame.
        // Existing decode scheduling supplies only viewport/nearby sources.
        onTriggered: {
            let next = null
            for (const surface of cache.surfaces) {
                if (surface.host && surface.source.toString() === "") {
                    if (!next || (!(next.host.visible && next.host.inViewport)
                                  && surface.host.visible && surface.host.inViewport)) next = surface
                }
            }
            if (next) next.source = next.requestedSource
            else running = false
        }
    }

    Component {
        id: factory
        GalleryVectorImage {
            id: cachedSurface
            property Item host
            property url requestedSource
            property int generation
            property size idleSize: Qt.size(64, 64)
            property real idleDpr: 1
            property bool idleCrop: false
            property bool idleCheckerboard: false
            // Hold the materialized revision even before scene admission.
            VectorSourcePin { source: cachedSurface.requestedSource }
            width: host ? host.width : idleSize.width
            height: host ? host.height : idleSize.height
            visible: host !== null && host.visible
            devicePixelRatio: host ? host.devicePixelRatio : idleDpr
            crop: host ? host.crop : idleCrop
            showCheckerboard: host ? host.showCheckerboard : idleCheckerboard
            prepareWhenHidden: true
            uploadPriority: host && host.inViewport ? 100 : 200
        }
    }
}
