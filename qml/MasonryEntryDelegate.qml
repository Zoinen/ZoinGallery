pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height

    Item {
        id: labelSurface
        GalleryLayoutDebugZone {
            visible: content.entry.debugLayoutZones
            color: "#20ffaa55"
        }
        readonly property real dpr: content.entry.renderDpr
        readonly property real margin: content.entry.cellVerticalInset
        readonly property real captionTopSpacing: content.entry.isImage
            ? content.entry.cellCaptionSpacing : 0
        x: content.entry.cellHorizontalInset
        y: Math.round((parent.height - margin - height) * dpr) / dpr
        width: content.entry.cellContentWidth
        height: Math.ceil(masonryLabel.implicitHeight * dpr) / dpr + captionTopSpacing
        readonly property point pixelGridOffset:
            content.entry.iconPixelOffset(labelSurface)
        transform: Translate {
            x: labelSurface.pixelGridOffset.x
            y: labelSurface.pixelGridOffset.y
        }

        Rectangle {
            objectName: "galleryMasonryLabelBackground-" + content.entry.viewIndex
            anchors.fill: parent
            color: content.entry.highlightLabelBackground
            radius: 3
        }

        Text {
            id: masonryLabel
            GalleryLayoutDebugZone { visible: content.entry.debugLayoutZones }
            font: content.entry.panelRoot.iconLabelFont
            objectName: "galleryMasonryLabel-" + content.entry.viewIndex
            anchors.fill: parent
            anchors.topMargin: labelSurface.captionTopSpacing
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
