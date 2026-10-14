pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    signal sourcePrepared(url source)
    signal sourcePresented(url source)
    property url source
    property bool animate: false
    property bool showCheckerboard: false
    property real devicePixelRatio: 1
    property int sourceStatus: Image.Null
    property url firstSource
    property url secondSource
    property int displayedSlot: -1
    property int pendingSlot: -1
    readonly property url displayedSource: displayedSlot === 0 ? firstSource
                                          : displayedSlot === 1 ? secondSource : ""

    function prepareSource() {
        if (String(source) === "") {
            displayedSlot = -1
            pendingSlot = -1
            firstSource = ""
            secondSource = ""
            sourceStatus = Image.Null
            return
        }
        if (String(source) === String(displayedSource)) {
            sourcePrepared(source)
            pendingSlot = -1
            if (displayedSlot === 0) secondSource = ""
            else firstSource = ""
            sourceStatus = Image.Ready
            sourcePresented(source)
            return
        }
        sourceStatus = Image.Loading
        pendingSlot = displayedSlot === 0 ? 1 : 0
        if (pendingSlot === 0) firstSource = source
        else secondSource = source
    }

    function updateSurface(slot, surface) {
        if (!surface || pendingSlot !== slot || String(surface.source) !== String(source)) return
        if (surface.sourceStatus === Image.Error) {
            sourceStatus = Image.Error
        } else if (surface.sourceStatus === Image.Ready) {
            sourcePrepared(source)
            displayedSlot = slot
            pendingSlot = -1
            sourceStatus = Image.Ready
            if (slot === 0) secondSource = ""
            else firstSource = ""
            sourcePresented(source)
        }
    }

    onSourceChanged: prepareSource()
    Component.onCompleted: prepareSource()

    // Invisible opacity keeps Shapes eligible for asynchronous preparation.
    // Only the completed scene becomes visible; the old scene stays intact.
    Loader {
        id: first
        anchors.fill: parent
        active: String(root.firstSource) !== ""
        opacity: root.displayedSlot === 0 ? 1 : 0
        sourceComponent: Component {
            GalleryVectorImage {
                source: root.firstSource
                animate: root.animate && root.displayedSlot === 0
                showCheckerboard: root.showCheckerboard
                devicePixelRatio: root.devicePixelRatio
                onSourceStatusChanged: root.updateSurface(0, this)
            }
        }
        onLoaded: root.updateSurface(0, item)
    }
    Loader {
        id: second
        anchors.fill: parent
        active: String(root.secondSource) !== ""
        opacity: root.displayedSlot === 1 ? 1 : 0
        sourceComponent: Component {
            GalleryVectorImage {
                source: root.secondSource
                animate: root.animate && root.displayedSlot === 1
                showCheckerboard: root.showCheckerboard
                devicePixelRatio: root.devicePixelRatio
                onSourceStatusChanged: root.updateSurface(1, this)
            }
        }
        onLoaded: root.updateSurface(1, item)
    }
}
