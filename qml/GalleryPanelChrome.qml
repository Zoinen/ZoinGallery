pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

Item {
    id: chromeRoot

    required property GalleryViewportItem layout
    required property GalleryThemePalette theme
    property bool ready: true
    property string presentationMode: "masonry"
    property real detailsScrollBarWidth: 16
    readonly property point layoutOrigin:
        layout ? layout.mapToItem(chromeRoot, 0, 0) : Qt.point(0, 0)

    signal seekRequested(real contentOffset)

    GalleryScrollBar {
        id: verticalScroll
        objectName: "galleryPanelScrollBar"
        theme: chromeRoot.theme
        x: parent.width - width + 8
        y: chromeRoot.layoutOrigin.y
        height: chromeRoot.layout.height
        z: 10
        visible: chromeRoot.ready
                 && chromeRoot.presentationMode !== "columns"
                 && chromeRoot.layout.needScroll
        width: chromeRoot.layout.needScroll
               ? (chromeRoot.presentationMode === "details"
                  ? chromeRoot.detailsScrollBarWidth : 16) : 0
        orientation: Qt.Vertical

        onPositionChanged: {
            if (pressed) {
                chromeRoot.seekRequested(
                            position * chromeRoot.layout.contentHeight)
            }
        }

        function updateSize() {
            const extent = chromeRoot.layout.contentHeight
            size = extent > 0
                    ? Math.min(1, chromeRoot.layout.height / extent) : 1
        }

        Connections {
            target: chromeRoot.layout

            function onContentYChanged() {
                const extent = chromeRoot.layout.contentHeight
                verticalScroll.position = extent > 0
                        ? chromeRoot.layout.contentY / extent : 0
            }
            function onContentHeightChanged() { verticalScroll.updateSize() }
            function onHeightChanged() { verticalScroll.updateSize() }
        }

        Component.onCompleted: {
            const extent = chromeRoot.layout.contentHeight
            size = extent > 0
                    ? Math.min(1, chromeRoot.layout.height / extent) : 1
            position = extent > 0 ? chromeRoot.layout.contentY / extent : 0
        }
    }

    GalleryScrollBar {
        objectName: "galleryPanelColumnsScrollBar"
        theme: chromeRoot.theme
        x: chromeRoot.layoutOrigin.x
        y: parent.height - height
        width: chromeRoot.layout.width
        z: 10
        height: visible ? 16 : 0
        visible: chromeRoot.ready
                 && chromeRoot.presentationMode === "columns"
                 && chromeRoot.layout.needScroll
        orientation: Qt.Horizontal
        size: chromeRoot.layout.contentHeight > 0
              ? Math.min(1, chromeRoot.layout.width
                         / chromeRoot.layout.contentHeight) : 1
        position: chromeRoot.layout.contentHeight > 0
                  ? chromeRoot.layout.contentY
                    / chromeRoot.layout.contentHeight : 0
        onPositionChanged: {
            if (pressed) {
                chromeRoot.seekRequested(
                            position * chromeRoot.layout.contentHeight)
            }
        }
    }
}
