pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: visual

    required property GalleryEntryDelegateBase entry
    readonly property real paintedHeight: content.paintedHeight
    readonly property bool folderPreviewReady: Boolean(folderLoader.item && folderLoader.item.hasUsablePreview)

    MasonryEntryDelegate {
        id: content
        anchors.fill: parent
        entry: visual.entry
        visible: !visual.entry.folderPreviewActive
    }

    Loader {
        id: folderLoader
        anchors.fill: parent
        active: visual.entry.folderPreviewRequested
        sourceComponent: Component {
            GalleryFolderPreview { entry: visual.entry }
        }
    }
}
