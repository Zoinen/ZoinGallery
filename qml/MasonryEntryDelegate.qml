pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height

    Item {
        id: labelSurface
        readonly property real dpr: content.entry.renderDpr
        readonly property real margin: Math.round(4 * dpr) / dpr
        readonly property real padding: Math.round(6 * dpr) / dpr
        x: margin
        y: Math.round((parent.height - margin - height) * dpr) / dpr
        width: Math.max(0, Math.round((parent.width - 2 * margin) * dpr) / dpr)
        height: Math.ceil(masonryLabel.implicitHeight * dpr) / dpr + 2 * padding
        readonly property point pixelGridOffset:
            content.entry.iconPixelOffset(labelSurface)
        transform: Translate {
            x: labelSurface.pixelGridOffset.x
            y: labelSurface.pixelGridOffset.y
        }

        Rectangle {
            anchors.fill: parent
            color: content.entry.highlightLabelBackground
            radius: 3
        }

        Text {
            id: masonryLabel
            objectName: "galleryMasonryLabel-" + content.entry.viewIndex
            anchors.fill: parent
            anchors.margins: labelSurface.padding
            text: content.entry.panelRoot.quickSearchFormatter.styledText(
                      content.entry.effectiveDisplayName,
                      content.entry.entryId, 0)
            textFormat:
                content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                    content.entry.entryId)
                ? Text.StyledText : Text.PlainText
            color: content.entry.hiddenEntry
                   ? Qt.rgba(content.entry.itemTextColor.r,
                             content.entry.itemTextColor.g,
                             content.entry.itemTextColor.b,
                             content.entry.itemTextColor.a * 0.5)
                   : content.entry.itemTextColor
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
