pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: Math.max(
        0, label.y + label.contentHeight + content.entry.cellVerticalInset)

    Text {
        id: label
        GalleryLayoutDebugZone { visible: content.entry.debugLayoutZones }
        readonly property point pixelCorrection:
            content.entry.iconPixelOffset(label)
        objectName: "galleryIconsLabel-" + content.entry.viewIndex
        x: content.entry.cellHorizontalInset
        y: content.entry.effectivePreviewRect.y
           + content.entry.effectivePreviewRect.height + content.entry.cellCaptionSpacing
        width: content.entry.cellContentWidth
        height: Math.max(0, Math.floor((parent.height - y - content.entry.cellVerticalInset
                                       - pixelCorrection.y + 0.000001)
                                      * content.entry.renderDpr)
                               / content.entry.renderDpr)
        text: content.entry.panelRoot.quickSearchFormatter.styledElidedText(
                  content.entry.iconLabelText,
                  content.entry.effectiveDisplayName,
                  content.entry.entryId)
        textFormat: content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                        content.entry.entryId)
                    ? Text.StyledText : Text.PlainText
        font: content.entry.panelRoot.iconLabelFont
        color: content.entry.hiddenEntry
               ? Qt.rgba(content.entry.itemTextColor.r,
                         content.entry.itemTextColor.g,
                         content.entry.itemTextColor.b,
                         content.entry.itemTextColor.a * 0.5)
               : content.entry.itemTextColor
        elide: Text.ElideNone
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 4
        clip: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        transform: Translate {
            x: label.pixelCorrection.x
            y: label.pixelCorrection.y
        }
    }
}
