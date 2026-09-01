pragma ComponentBehavior: Bound

import QtQuick

GalleryEntryDelegateBase {
    id: entry

    GalleryEntryActions {
        anchors.fill: parent
        z: 4
        entry: entry
    }
}
