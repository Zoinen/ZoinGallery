pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

Item {
    id: overlays

    required property GalleryPanelController controller
    required property GalleryThemePalette theme
    required property GalleryViewportItem layout
    required property bool localQuickSearchEnabled
    required property bool scrollBarsReady
    required property string presentationMode
    required property real detailsScrollBarWidth
    required property real devicePixelRatio

    signal seekRequested(real contentOffset)

    GalleryQuickSearchOverlay {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        width: Math.min(implicitWidth, Math.max(1, parent.width - 20))
        height: implicitHeight
        visible: overlays.localQuickSearchEnabled
                 && overlays.controller.quickSearchActive
        z: 20
        controller: overlays.controller
        backgroundColor: overlays.theme.dialogBackground
        borderColor: overlays.theme.separator
        textColor: overlays.theme.text
        mutedTextColor: overlays.theme.mutedText
        devicePixelRatio: overlays.devicePixelRatio
    }

    GalleryPanelChrome {
        anchors.fill: parent
        layout: overlays.layout
        theme: overlays.theme
        ready: overlays.scrollBarsReady
        presentationMode: overlays.presentationMode
        detailsScrollBarWidth: overlays.detailsScrollBarWidth
        onSeekRequested: contentOffset => overlays.seekRequested(contentOffset)
    }
}
