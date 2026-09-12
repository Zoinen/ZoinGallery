pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height
    opacity: content.entry.hiddenEntry ? 0.5 : 1

    function pixelOffset(item) {
        // Include every ancestor: horizontal paging and host placement can
        // change the scene origin without changing the leaf's local geometry.
        let revision = 0
        for (let parent = item.parent; parent; parent = parent.parent)
            revision += parent.x + parent.y + parent.width + parent.height
        const point = item.parent.mapToItem(null, item.x, item.y)
        const dpr = content.entry.renderDpr
        return Qt.point(Math.round(point.x * dpr) / dpr - point.x + revision * 0,
                        Math.round(point.y * dpr) / dpr - point.y)
    }

    Item {
        id: textRow
        x: content.entry.effectivePreviewRect.x
           + content.entry.effectivePreviewRect.width + 6
        width: Math.max(0, parent.width - x - 7)
        height: parent.height
        readonly property real gap: 4
        readonly property real sizeColumnWidth: 0
        readonly property real extensionColumnWidth: {
            if (!content.entry.panelRoot.separateFileExtensions)
                return 0
            const available = Math.max(0, width - sizeColumnWidth)
            const preferred = Math.max(40, available * 0.28)
            return Math.max(0, Math.min(112, preferred, available * 0.45))
        }

        Text {
            id: extensionMeasurement
            visible: false
            text: extensionLabel.text
            textFormat: extensionLabel.textFormat
            font: extensionLabel.font
        }

        Text {
            id: baseNameLabel
            objectName: "galleryBaseName-" + content.entry.viewIndex
            x: 0
            width: Math.max(0, (extensionLabel.visible
                               ? extensionLabel.x : parent.width)
                              - x - (extensionLabel.visible
                                     ? textRow.gap : 0))
            height: implicitHeight
            y: (parent.height - height) / 2
            text: content.entry.panelRoot.quickSearchFormatter.styledText(
                      content.entry.panelRoot.separateFileExtensions
                          ? content.entry.displayBaseName
                          : content.entry.displayBaseName
                            + (content.entry.displayExtension !== ""
                               ? "." + content.entry.displayExtension : ""),
                      content.entry.entryId, 0)
            textFormat:
                content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                    content.entry.entryId)
                ? Text.StyledText : Text.PlainText
            color: content.entry.itemTextColor
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: -1
            transform: Translate {
                x: content.pixelOffset(baseNameLabel).x
                y: content.pixelOffset(baseNameLabel).y
            }
        }

        Text {
            id: extensionLabel
            objectName: "galleryExtension-" + content.entry.viewIndex
            visible: content.entry.panelRoot.separateFileExtensions
                     && content.entry.displayExtension !== ""
            anchors.right: parent.right
            height: implicitHeight
            y: (parent.height - height) / 2
            width: Math.floor(textRow.extensionColumnWidth * content.entry.renderDpr) / content.entry.renderDpr
            text: content.entry.panelRoot.quickSearchFormatter.styledSuffix(
                      content.entry.displayExtension !== ""
                          ? "." + content.entry.displayExtension : "",
                      content.entry.displayBaseName,
                      content.entry.entryId, 0)
            textFormat:
                content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                    content.entry.entryId)
                ? Text.StyledText : Text.PlainText
            color: content.entry.itemTextColor
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideLeft
            font.pixelSize: -1
            transform: Translate {
                x: content.pixelOffset(extensionLabel).x
                y: content.pixelOffset(extensionLabel).y
            }
        }

        Text {
            objectName: "gallerySize-" + content.entry.viewIndex
            visible: false
        }
    }
}
