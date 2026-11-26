pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as T

T.ScrollView {
    id: view
    property string menuObjectName: "dropdown"
    objectName: menuObjectName + "ScrollView"
    readonly property bool overflow: contentHeight > availableHeight
    readonly property real rowWidth: Math.max(0, availableWidth - (overflow ? ScrollBar.vertical.width : 0))
    implicitHeight: contentHeight
    contentWidth: availableWidth
    clip: true
    GalleryDropdownWheelHandler { viewport: view.contentItem }
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: T.ScrollBar {
        policy: view.overflow ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }
    Component.onCompleted: {
        contentItem.objectName = view.menuObjectName + "Viewport"
        contentItem.boundsBehavior = Flickable.StopAtBounds
    }
    function reveal(row) {
        if (!row || !contentItem || !contentItem.contentItem) return
        const top = row.mapToItem(contentItem.contentItem, 0, 0).y
        if (top < contentItem.contentY) contentItem.contentY = top
        else if (top + row.height > contentItem.contentY + contentItem.height)
            contentItem.contentY = top + row.height - contentItem.height
    }
    onOverflowChanged: if (overflow) console.debug("[FIX:menu-overflow] scrolling", objectName)
}
