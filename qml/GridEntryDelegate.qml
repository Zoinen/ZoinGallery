pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height
    FontMetrics {
        id: captionMetrics
        font: content.entry.panelRoot.iconLabelFont
    }

    Text {
        id: label
        GalleryLayoutDebugZone {
            objectName: "galleryGridTextZone-" + content.entry.viewIndex
            visible: content.entry.debugLayoutZones
        }
        readonly property var quickSearchMatch:
            content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                content.entry.entryId)
        readonly property point pixelCorrection:
            content.entry.iconPixelOffset(label)
        objectName: "galleryGridLabel-" + content.entry.viewIndex
        x: content.entry.cellHorizontalInset
        y: content.entry.effectivePreviewRect.y
           + content.entry.effectivePreviewRect.height + content.entry.cellCaptionSpacing
        width: content.entry.cellContentWidth
        height: Math.round(2 * Math.ceil(captionMetrics.height)
                           * content.entry.renderDpr) / content.entry.renderDpr
        text: content.entry.panelRoot.quickSearchFormatter.styledElidedText(
                  content.entry.iconLabelText,
                  content.entry.effectiveDisplayName,
                  content.entry.entryId)
        textFormat: quickSearchMatch ? Text.StyledText : Text.PlainText
        font: content.entry.panelRoot.iconLabelFont
        color: content.entry.hiddenEntry
               ? Qt.rgba(content.entry.itemTextColor.r,
                         content.entry.itemTextColor.g,
                         content.entry.itemTextColor.b,
                         content.entry.itemTextColor.a * 0.5)
               : content.entry.itemTextColor
        elide: Text.ElideRight
        wrapMode: Text.Wrap
        maximumLineCount: 2
        clip: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        transform: Translate {
            x: label.pixelCorrection.x
            y: label.pixelCorrection.y
        }
    }
}
