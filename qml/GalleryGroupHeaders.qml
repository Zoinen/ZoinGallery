pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: groupHeaders
    required property var panelRoot
    width: 0
    height: 0

    Repeater {
        model: groupHeaders.panelRoot.galleryLayout.visibleGroupHeaderGeometries
        delegate: Rectangle {
            id: galleryGroupHeader
            required property var modelData
            required property int index
            parent: groupHeaders.panelRoot.galleryLayout.viewport
            objectName: "galleryGroupHeader-"
                        + String(modelData.key || galleryGroupHeader.index)
            z: 2
            color: groupHeaders.panelRoot.backgroundColor
            readonly property real effectiveDpr:
                groupHeaders.panelRoot.devicePixelRatio > 0
                    ? groupHeaders.panelRoot.devicePixelRatio : 1
            x: Number(modelData.x || 0)
            y: Number(modelData.y || 0)
            width: Number(modelData.width || 0)
            height: Number(modelData.height || 0)

            Text {
                id: galleryGroupTitle
                objectName: "galleryGroupHeaderText-"
                             + String(galleryGroupHeader.modelData.key
                                       || galleryGroupHeader.index)
                text: String(galleryGroupHeader.modelData.title || "")
                textFormat: Text.PlainText
                font.pixelSize:
                    groupHeaders.panelRoot.detailsHeaderFontPixelSize
                color: groupHeaders.panelRoot.headerTextColor
                elide: Text.ElideRight
                x: 8 / galleryGroupHeader.effectiveDpr
                y: Math.round(Math.max(0, (parent.height - height) / 2)
                              * galleryGroupHeader.effectiveDpr)
                    / galleryGroupHeader.effectiveDpr
                width: Math.round(Math.max(
                    0, parent.width - x
                       - 8 / galleryGroupHeader.effectiveDpr)
                    * galleryGroupHeader.effectiveDpr)
                    / galleryGroupHeader.effectiveDpr
                height: Math.ceil(implicitHeight
                                  * galleryGroupHeader.effectiveDpr)
                    / galleryGroupHeader.effectiveDpr
            }

            Rectangle {
                objectName: "galleryGroupHeaderSeparator-"
                             + String(galleryGroupHeader.modelData.key
                                       || galleryGroupHeader.index)
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1 / galleryGroupHeader.effectiveDpr
                color: groupHeaders.panelRoot.separatorColor
            }
        }
    }
}
