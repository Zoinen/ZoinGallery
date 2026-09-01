pragma ComponentBehavior: Bound

import QtQuick

// The middle-button auto-scroll gesture used by the original MasonryMode.
// Keeping the frame-rate math here makes the gesture available to every
// reusable presentation without giving each view its own subtly different
// scrolling implementation.
Item {
    id: root
    visible: false
    width: 0
    height: 0

    property var layout: null
    property var pointerSource: null
    property bool horizontal: false
    // MasonryMode historically used the available screen height as the speed
    // scale. Embedders provide that extent explicitly; the viewport is a
    // deterministic fallback for windowless tests.
    property real scrollExtent: 0
    property real startCoordinate: 0
    property bool scrollingStarted: false
    property bool scrollingMode: false
    readonly property bool animationRunning: autoScrollAnimation.running

    function pointerCoordinate() {
        if (!root.pointerSource)
            return 0
        return root.horizontal
                ? Number(root.pointerSource.mouseX || 0)
                : Number(root.pointerSource.mouseY || 0)
    }

    function start() {
        if (root.scrollingMode) {
            root.end()
            return
        }
        root.scrollingStarted = false
        root.scrollingMode = true
        root.startCoordinate = root.pointerCoordinate()
        autoScrollAnimation.start()
        if (root.layout)
            root.layout.setScrollingMode(true)
    }

    function end() {
        root.scrollingStarted = false
        root.scrollingMode = false
        autoScrollAnimation.stop()
        if (root.layout)
            root.layout.setScrollingMode(false)
    }

    FrameAnimation {
        id: autoScrollAnimation
        objectName: "autoScrollFrameAnimation"

        onTriggered: {
            if (!root.layout)
                return

            let distance = root.pointerCoordinate() - root.startCoordinate
            distance = Math.min(Math.max(0, distance - 25), distance + 25)
            const totalExtent = Math.max(
                        25, Number(root.scrollExtent || 0))
            let fraction = Math.abs(distance) / Math.max(1, totalExtent - 25)
            if (fraction > 0)
                fraction += 0.05

            const speed = frameTime * 30
            const increment = (Math.pow(fraction, 3) * totalExtent)
                    * speed * 2
            root.layout.contentY += increment
                    * (distance < 0 ? -1 : 1)

            if (distance !== 0 && !root.scrollingStarted)
                root.scrollingStarted = true

            if (distance > 0)
                root.layout.setScrollingMode(true, 1)
            else if (distance < 0)
                root.layout.setScrollingMode(true, -1)
            else
                root.layout.setScrollingMode(true)
        }
    }
}
