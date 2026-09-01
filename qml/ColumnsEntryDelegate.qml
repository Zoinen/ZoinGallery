pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height
    opacity: content.entry.hiddenEntry ? 0.5 : 1

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
            anchors.top: parent.top
            anchors.bottom: parent.bottom
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
        }

        Text {
            id: extensionLabel
            objectName: "galleryExtension-" + content.entry.viewIndex
            visible: content.entry.panelRoot.separateFileExtensions
                     && content.entry.displayExtension !== ""
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: textRow.extensionColumnWidth
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
        }

        Text {
            objectName: "gallerySize-" + content.entry.viewIndex
            visible: false
        }
    }
}
