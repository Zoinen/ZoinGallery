pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: visual

    required property GalleryEntryDelegateBase entry
    readonly property real paintedHeight: content.paintedHeight

    MasonryEntryDelegate {
        id: content
        anchors.fill: parent
        entry: visual.entry
    }
}
