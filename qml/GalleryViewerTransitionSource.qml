pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property GalleryPanel panel

    function currentItem() {
        if (!panel.controller || panel.controller.currentIndex < 0)
            return null
        const item = panel.galleryLayout.currentItem
        if (!item || item.viewIndex !== panel.controller.currentIndex)
            return null
        return item
    }

    function imageGeometry(targetItem) {
        const item = currentItem()
        if (!item || !item.thumbnailItem || !targetItem)
            return Qt.rect(0, 0, 0, 0)
        const imageItem = item.thumbnailItem
        const topLeft = targetItem.mapFromItem(imageItem, 0, 0)
        const bottomRight = targetItem.mapFromItem(
                    imageItem, imageItem.width, imageItem.height)
        const geometry = Qt.rect(
                    Math.min(topLeft.x, bottomRight.x),
                    Math.min(topLeft.y, bottomRight.y),
                    Math.abs(bottomRight.x - topLeft.x),
                    Math.abs(bottomRight.y - topLeft.y))
        const panelTopLeft = targetItem.mapFromItem(panel, 0, 0)
        const panelBottomRight = targetItem.mapFromItem(
                    panel, panel.width, panel.height)
        const panelLeft = Math.min(panelTopLeft.x, panelBottomRight.x)
        const panelTop = Math.min(panelTopLeft.y, panelBottomRight.y)
        const panelRight = Math.max(panelTopLeft.x, panelBottomRight.x)
        const panelBottom = Math.max(panelTopLeft.y, panelBottomRight.y)
        if (geometry.width <= 1 || geometry.height <= 1
                || geometry.x + geometry.width <= panelLeft
                || geometry.y + geometry.height <= panelTop
                || geometry.x >= panelRight || geometry.y >= panelBottom)
            return Qt.rect(0, 0, 0, 0)
        return geometry
    }

    function imageSource() {
        const item = currentItem()
        if (!item || !item.thumbnailItem)
            return ""
        return item.thumbnailItem.source.toString()
    }
}
