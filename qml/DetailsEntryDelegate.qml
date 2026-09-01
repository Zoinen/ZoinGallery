pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content

    required property GalleryEntryDelegateBase entry
    readonly property alias previewHost: detailsIconSlot
    readonly property real paintedHeight: height

    Item {
        id: detailsRow
        anchors.fill: parent
        opacity: content.entry.hiddenEntry ? 0.5 : 1
        objectName: "galleryDetailsRow-" + content.entry.viewIndex

        Item {
            id: detailsIconSlot
            objectName: "galleryDetailsIconSlot-" + content.entry.viewIndex
            anchors.left: parent.left
            anchors.leftMargin: content.entry.panelRoot.detailsRowInset
            anchors.verticalCenter: parent.verticalCenter
            width: content.entry.panelRoot.detailsIconSlotSize
            height: content.entry.panelRoot.detailsIconSlotSize
        }

        Text {
            id: baseNameText
            objectName: "galleryBaseName-" + content.entry.viewIndex
            anchors.left: detailsIconSlot.right
            anchors.leftMargin: content.entry.panelRoot.detailsRowSpacing
            anchors.right: extensionText.visible
                           ? extensionText.left : sizeText.left
            anchors.rightMargin: content.entry.panelRoot.detailsRowSpacing
            anchors.verticalCenter: parent.verticalCenter
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
            font.pixelSize:
                content.entry.panelRoot.detailsNameFontPixelSize
        }

        Text {
            id: extensionText
            objectName: "galleryExtension-" + content.entry.viewIndex
            anchors.right: sizeText.left
            anchors.rightMargin: content.entry.panelRoot.detailsRowSpacing
            anchors.verticalCenter: parent.verticalCenter
            visible: content.entry.panelRoot.separateFileExtensions
                     && content.entry.displayExtension.length > 0
            width: Math.min(
                content.entry.panelRoot.detailsExtensionMaximumWidth,
                Math.max(
                    content.entry.panelRoot.detailsExtensionMinimumWidth,
                    implicitWidth))
            text: content.entry.panelRoot.quickSearchFormatter.styledSuffix(
                      content.entry.displayExtension,
                      content.entry.displayBaseName,
                      content.entry.entryId, 1)
            textFormat:
                content.entry.panelRoot.quickSearchFormatter.matchForEntry(
                    content.entry.entryId)
                ? Text.StyledText : Text.PlainText
            color: content.entry.itemTextColor
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            font.pixelSize:
                content.entry.panelRoot.detailsSecondaryFontPixelSize
        }

        Text {
            id: sizeText
            objectName: "gallerySize-" + content.entry.viewIndex
            anchors.right: parent.right
            anchors.rightMargin: content.entry.panelRoot.detailsRowInset
            anchors.verticalCenter: parent.verticalCenter
            width: content.entry.panelRoot.detailsSizeColumnWidth
            text: content.entry.displaySize
            color: content.entry.itemMetadataColor
            horizontalAlignment: Text.AlignRight
            font.pixelSize:
                content.entry.panelRoot.detailsSecondaryFontPixelSize
        }
    }
}
