pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    readonly property GalleryLayoutDebugZone zoneOwner: parent
    objectName: zoneOwner.objectName + "Paint"
    anchors.fill: parent
    color: zoneOwner.color
    border.color: zoneOwner.borderColor
}
