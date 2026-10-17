pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: host
    required property GalleryVectorSceneCache sceneCache
    property url source
    property real devicePixelRatio: 1
    property bool crop: false
    property bool showCheckerboard: false
    property bool inViewport: true
    property GalleryVectorImage surface
    property bool completed: false
    readonly property int sourceStatus: !surface ? Image.Null
        : surface.source.toString() === "" ? Image.Loading : surface.sourceStatus

    function attach() {
        const previous = surface
        surface = source.toString() !== "" ? sceneCache.acquire(source, host) : null
        if (previous) sceneCache.release(previous)
    }
    onSourceChanged: { if (completed) attach() }
    Component.onCompleted: {
        completed = true
        attach()
    }
    Component.onDestruction: {
        if (surface && sceneCache) sceneCache.release(surface)
    }
}
