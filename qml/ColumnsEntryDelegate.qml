pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height
    opacity: content.entry.hiddenEntry ? 0.5 : 1

    FontMetrics {
        id: nameMetrics
        font: baseNameLabel.font
    }

    function pixelOffset(item) {
        // The row observes its complete ancestor chain once. Labels only
        // need their local chain plus that shared scene-space origin.
        const origin = content.entry.iconSceneOrigin
        let revision = origin.x + origin.y
        for (let parent = item.parent; parent && parent !== content.entry; parent = parent.parent) {
            revision += parent.x + parent.y + parent.scale + parent.rotation
            if (parent.scale !== 1 || parent.rotation !== 0)
                revision += parent.width + parent.height + parent.transformOrigin
        }
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
            readonly property point pixelCorrection: content.pixelOffset(baseNameLabel)
            objectName: "galleryBaseName-" + content.entry.viewIndex
            x: 0
            width: Math.max(0, Math.round(
                ((extensionLabel.visible
                  ? extensionLabel.x : parent.width)
                 - x - (extensionLabel.visible ? textRow.gap : 0))
                * content.entry.renderDpr) / content.entry.renderDpr)
            height: Math.ceil(implicitHeight * content.entry.renderDpr)
                    / content.entry.renderDpr
            maximumLineCount: Math.max(1, Math.floor((content.height - 4) / nameMetrics.height))
            wrapMode: maximumLineCount > 1 ? Text.Wrap : Text.NoWrap
            elide: maximumLineCount > 1 ? Text.ElideRight : Text.ElideMiddle
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
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: -1
            transform: Translate {
                x: baseNameLabel.pixelCorrection.x
                y: baseNameLabel.pixelCorrection.y
            }
        }

        Text {
            id: extensionLabel
            readonly property point pixelCorrection: content.pixelOffset(extensionLabel)
            objectName: "galleryExtension-" + content.entry.viewIndex
            visible: content.entry.panelRoot.separateFileExtensions
                     && content.entry.displayExtension !== ""
            anchors.right: parent.right
            height: Math.ceil(implicitHeight * content.entry.renderDpr)
                    / content.entry.renderDpr
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
                x: extensionLabel.pixelCorrection.x
                y: extensionLabel.pixelCorrection.y
            }
        }

        Text {
            objectName: "gallerySize-" + content.entry.viewIndex
            visible: false
        }
    }
}
