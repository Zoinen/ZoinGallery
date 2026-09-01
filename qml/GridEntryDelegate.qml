pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height

    Text {
        id: label
        readonly property var quickSearchMatch:
            content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                content.entry.entryId)
        objectName: "galleryGridLabel-" + content.entry.viewIndex
        x: 4
        y: content.entry.effectivePreviewRect.y
           + content.entry.effectivePreviewRect.height + 3
        width: Math.max(0, parent.width - 8)
        height: Math.max(0, parent.height - y - 3)
        text: quickSearchMatch
              ? content.entry.panelRoot.quickSearchFormatter.styledTextForMatch(
                    content.entry.effectiveDisplayName,
                    quickSearchMatch, 0)
              : content.entry.effectiveDisplayName
        textFormat: quickSearchMatch ? Text.StyledText : Text.PlainText
        font: content.entry.panelRoot.iconLabelFont
        color: content.entry.hiddenEntry
               ? Qt.rgba(content.entry.itemTextColor.r,
                         content.entry.itemTextColor.g,
                         content.entry.itemTextColor.b,
                         content.entry.itemTextColor.a * 0.5)
               : content.entry.itemTextColor
        elide: Text.ElideMiddle
        wrapMode: Text.NoWrap
        maximumLineCount: 1
        clip: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
    }
}
