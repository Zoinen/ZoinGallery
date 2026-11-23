pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import ZoinGallery.PixelGrid 1.0

Item {
    id: root

    required property var session
    required property int index
    required property FlickableZoomable viewport
    required property real devicePixelRatio
    required property bool presenting
    required property color textColor
    required property color barColor
    property string iconPrefix: "qrc:/ZoinGallery/resources/lucide"
    property var labels: ({})
    readonly property color buttonTextColor: barColor.r * 0.213 + barColor.g * 0.715
                                             + barColor.b * 0.072 > 0.55 ? "#141518" : "#f3f4f6"

    readonly property var controller: session ? session.pageController ?? null : null
    readonly property bool active: controller ? controller.active : false
    readonly property bool multipage: active && (controller.count > 1 || controller.count < 0)
    readonly property url pageSource: active ? controller.source : ""
    readonly property real logicalDpi: {
        const dotsPerMillimeter = Screen.logicalPixelDensity
        return dotsPerMillimeter > 0 ? dotsPerMillimeter * 25.4 : 96
    }
    readonly property size pageSize: {
        if (!active)
            return Qt.size(0, 0)
        const size = controller.size
        if (!controller.scalable)
            return size
        // Qt PDF returns points. The viewport expects the 100% size in device pixels.
        const pixelsPerPoint = logicalDpi * devicePixelRatio / 72
        return Qt.size(size.width * pixelsPerPoint, size.height * pixelsPerPoint)
    }
    readonly property string error: {
        if (!active || controller.error === "")
            return ""
        return controller.error === "password" ? (labels.password || qsTr("Password required"))
                                               : (labels.render || qsTr("Cannot render this page"))
    }
    readonly property bool hasFrame: pageSource.toString() !== ""
    property bool ready: false

    signal frameChanged()

    function syncSource() {
        if (controller && ready) {
            session.presentPagedImageAt(index, Math.ceil(width * devicePixelRatio),
                                        Math.ceil(height * devicePixelRatio), presenting)
        }
    }

    function handleKey(event, down) {
        if (!multipage || event.modifiers !== Qt.AltModifier)
            return false
        let direction = 0
        if (event.key === Qt.Key_Space || event.key === Qt.Key_PageDown)
            direction = 1
        else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_PageUp)
            direction = -1
        if (!direction)
            return false
        if (down)
            controller.step(direction)
        event.accepted = true
        return true
    }

    function snap(value) {
        return Math.round(value * devicePixelRatio) / devicePixelRatio
    }

    function viewportMatchesPageSize() {
        const dpr = viewport.devicePixelRatio
        return viewport.originalSize.width === pageSize.width / dpr
            && viewport.originalSize.height === pageSize.height / dpr
    }

    function scheduleDetail() {
        if (active && controller.scalable && hasFrame)
            detailTimer.restart()
    }

    function refreshFrame() {
        frameChanged()
        scheduleDetail()
    }

    function requestDetail() {
        if (!presenting || !active || !controller.scalable || !hasFrame)
            return
        if (viewport.zoomFitView) {
            controller.requestDetail(Qt.size(0, 0), Qt.rect(0, 0, 0, 0))
            return
        }
        if (viewport.viewportAnimationRunning || viewport.zoomScrollingAnimationRunning
                || viewport.directManipulationActive || viewport.isRotating) {
            detailTimer.restart()
            return
        }
        const image = viewport.unrotatedContent
        const dpr = devicePixelRatio
        const fullWidth = Math.round(image.width * dpr)
        const fullHeight = Math.round(image.height * dpr)
        const first = viewport.mapToItem(image, 0, 0)
        const last = viewport.mapToItem(image, viewport.width, viewport.height)
        const left = Math.max(0, Math.floor(Math.min(first.x, last.x) * dpr))
        const top = Math.max(0, Math.floor(Math.min(first.y, last.y) * dpr))
        const right = Math.min(fullWidth, Math.ceil(Math.max(first.x, last.x) * dpr))
        const bottom = Math.min(fullHeight, Math.ceil(Math.max(first.y, last.y) * dpr))
        controller.requestDetail(Qt.size(fullWidth, fullHeight),
                                 Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top)))
    }

    onWidthChanged: syncSource()
    onHeightChanged: syncSource()
    onDevicePixelRatioChanged: {
        syncSource()
        if (hasFrame)
            Qt.callLater(root.refreshFrame)
    }
    onPageSizeChanged: {
        if (!hasFrame)
            return
        if (controller.scalable && Qt.application.arguments.includes("--debug-viewer-transition"))
            console.debug("[FIX:pdf-dpi]", controller.size, "points at", logicalDpi,
                          "logical DPI and DPR", devicePixelRatio, "=>", pageSize, "pixels")
        Qt.callLater(root.refreshFrame)
    }
    onIndexChanged: syncSource()
    onSessionChanged: syncSource()
    onPresentingChanged: syncSource()
    Component.onCompleted: { ready = true; syncSource() }

    Connections {
        target: root.session
        ignoreUnknownSignals: true

        function onCatalogRevisionChanged() { root.syncSource() }
        function onViewerSourceAtChanged(index) {
            if (index === root.index)
                root.syncSource()
        }
    }

    Connections {
        target: root.controller
        function onChanged() {
            Qt.callLater(root.refreshFrame)
        }
    }

    Connections {
        target: root.viewport
        function onZoomScaleChanged() { root.scheduleDetail() }
        function onZoomFitViewChanged() { root.scheduleDetail() }
        function onRotationModeChanged() { root.scheduleDetail() }
    }

    Connections {
        target: root.viewport.image
        function onXChanged() { root.scheduleDetail() }
        function onYChanged() { root.scheduleDetail() }
    }

    Timer {
        id: detailTimer
        interval: 80
        onTriggered: root.requestDetail()
    }

    Image {
        parent: root.viewport.unrotatedContent
        objectName: "galleryPageDetailImage"
        readonly property rect region: root.active ? root.controller.detailRect : Qt.rect(0, 0, 0, 0)
        x: root.snap(region.x * parent.width)
        y: root.snap(region.y * parent.height)
        width: root.snap((region.x + region.width) * parent.width) - x
        height: root.snap((region.y + region.height) * parent.height) - y
        source: root.active ? root.controller.detailSource : ""
        cache: false
        visible: !root.viewport.zoomFitView && status === Image.Ready
        smooth: false
    }

    GalleryPageLabel {
        id: errorLabel
        objectName: "galleryPageError"
        anchors.fill: parent
        text: root.error
        color: root.textColor
        font.pixelSize: 14
        visible: root.presenting && text !== ""
    }

    PixelGridScope {
        id: pageBar
        objectName: "galleryPageBar"
        x: root.snap((root.width - width) / 2)
        y: Math.max(0, root.snap(root.height - height - 12))
        width: root.snap(2 * buttonWidth + countWidth + (root.active && root.controller.animated ? buttonWidth : 0))
        height: root.snap(28)
        visible: root.presenting && root.multipage && root.error === ""
        readonly property real buttonWidth: root.snap(30)
        readonly property real countWidth: root.snap(Math.max(62, pageCount.implicitWidth + 16))

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(root.barColor.r, root.barColor.g, root.barColor.b, 0.97)
            radius: root.snap(5)
        }

        component PageButton: Button {
            id: button
            width: pageBar.buttonWidth
            height: pageBar.height
            focusPolicy: Qt.TabFocus
            Accessible.name: description
            required property string description
            required property string iconObjectName
            padding: 0
            contentItem: Item {
                IconImage {
                    id: glyph
                    objectName: button.iconObjectName
                    Accessible.ignored: true
                    x: root.snap((parent.width - width) / 2)
                    y: root.snap((parent.height - height) / 2)
                    width: root.snap(16)
                    height: width
                    sourceSize: Qt.size(16, 16)
                    source: button.icon.source
                    color: root.buttonTextColor
                    fillMode: Image.PreserveAspectFit
                    smooth: false
                    opacity: button.enabled ? 1 : 0.45
                    transform: Translate {
                        x: glyph.PixelGridAlignment.offsetX
                        y: glyph.PixelGridAlignment.offsetY
                    }
                }
            }
            background: Rectangle {
                color: button.down || button.hovered || button.activeFocus
                       ? Qt.rgba(root.buttonTextColor.r, root.buttonTextColor.g,
                                 root.buttonTextColor.b, 0.16) : "transparent"
                radius: root.snap(5)
            }
        }

        PageButton {
            objectName: "galleryPreviousPage"
            iconObjectName: "galleryPreviousPageIcon"
            description: root.labels.previous || qsTr("Previous page or frame")
            icon.source: root.iconPrefix + "/chevron-left.svg"
            enabled: root.multipage && root.controller.index > 0
            onClicked: root.controller.step(-1)
        }

        GalleryPageLabel {
            id: pageCount
            objectName: "galleryPageCount"
            x: pageBar.buttonWidth
            width: pageBar.countWidth
            height: pageBar.height
            font.pixelSize: 12
            color: root.buttonTextColor
            text: root.multipage ? (root.controller.count < 0
                    ? String(root.controller.index + 1)
                    : qsTr("%1 / %2").arg(root.controller.index + 1).arg(root.controller.count)) : ""
        }

        PageButton {
            objectName: "galleryNextPage"
            iconObjectName: "galleryNextPageIcon"
            description: root.labels.next || qsTr("Next page or frame")
            x: pageBar.buttonWidth + pageBar.countWidth
            icon.source: root.iconPrefix + "/chevron-right.svg"
            enabled: root.multipage && (root.controller.count < 0
                      || root.controller.index + 1 < root.controller.count)
            onClicked: root.controller.step(1)
        }

        PageButton {
            objectName: "galleryPagePlayback"
            iconObjectName: "galleryPagePlaybackIcon"
            x: 2 * pageBar.buttonWidth + pageBar.countWidth
            visible: root.multipage && root.controller.animated
            readonly property bool playing: root.multipage && root.controller.playing
            description: playing ? (root.labels.pause || qsTr("Pause"))
                                 : (root.labels.play || qsTr("Play"))
            icon.source: root.iconPrefix + (playing ? "/pause.svg" : "/play.svg")
            onClicked: root.controller.togglePlayback()
        }
    }
}
