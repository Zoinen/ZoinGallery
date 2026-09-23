pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content

    required property GalleryEntryDelegateBase entry
    readonly property alias previewHost: detailsIconSlot
    readonly property real paintedHeight: height

    FontMetrics {
        id: nameMetrics
        font: baseNameText.font
    }

    Item {
        id: detailsRow
        anchors.fill: parent
        opacity: content.entry.hiddenEntry ? 0.5 : 1
        objectName: "galleryDetailsRow-" + content.entry.viewIndex

        Item {
            id: detailsIconSlot
            objectName: "galleryDetailsIconSlot-" + content.entry.viewIndex
            x: content.entry.effectivePreviewRect.x
            y: content.entry.effectivePreviewRect.y
            width: content.entry.effectivePreviewRect.width
            height: content.entry.effectivePreviewRect.height
        }

        Text {
            id: baseNameText
            readonly property point pixelCorrection: content.entry.iconPixelOffset(baseNameText)
            objectName: "galleryBaseName-" + content.entry.viewIndex
            anchors.left: detailsIconSlot.right
            anchors.leftMargin: content.entry.panelRoot.detailsRowSpacing
            width: Math.max(0, Math.floor(((extensionText.visible
                                           ? extensionText.x : sizeText.x)
                                          - content.entry.panelRoot.detailsRowSpacing - x)
                                         * content.entry.renderDpr) / content.entry.renderDpr)
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
            // Qt supports multi-line elision only at the right edge. Keep
            // middle elision for the compact, single-line presentation.
            maximumLineCount: Math.max(1, Math.floor((content.height - 4) / nameMetrics.height))
            wrapMode: maximumLineCount > 1 ? Text.Wrap : Text.NoWrap
            elide: maximumLineCount > 1 ? Text.ElideRight : Text.ElideMiddle
            height: Math.ceil(implicitHeight * content.entry.renderDpr) / content.entry.renderDpr
            transform: Translate {
                x: baseNameText.pixelCorrection.x
                y: baseNameText.pixelCorrection.y
            }
            font.pixelSize:
                content.entry.panelRoot.detailsNameFontPixelSize
        }

        Text {
            id: extensionText
            readonly property point pixelCorrection: content.entry.iconPixelOffset(extensionText)
            objectName: "galleryExtension-" + content.entry.viewIndex
            anchors.right: sizeText.left
            anchors.rightMargin: content.entry.panelRoot.detailsRowSpacing
            anchors.verticalCenter: parent.verticalCenter
            visible: content.entry.panelRoot.separateFileExtensions
                     && content.entry.displayExtension.length > 0
            height: Math.ceil(implicitHeight * content.entry.renderDpr) / content.entry.renderDpr
            width: Math.round(Math.min(
                content.entry.panelRoot.detailsExtensionMaximumWidth,
                Math.max(
                    content.entry.panelRoot.detailsExtensionMinimumWidth,
                    implicitWidth)) * content.entry.renderDpr) / content.entry.renderDpr
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
            transform: Translate {
                x: extensionText.pixelCorrection.x
                y: extensionText.pixelCorrection.y
            }
            font.pixelSize:
                content.entry.panelRoot.detailsSecondaryFontPixelSize
        }

        Text {
            id: sizeText
            readonly property point pixelCorrection: content.entry.iconPixelOffset(sizeText)
            objectName: "gallerySize-" + content.entry.viewIndex
            anchors.right: parent.right
            anchors.rightMargin: content.entry.panelRoot.detailsRowInset
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round(content.entry.panelRoot.detailsSizeColumnWidth
                              * content.entry.renderDpr) / content.entry.renderDpr
            height: Math.ceil(implicitHeight * content.entry.renderDpr) / content.entry.renderDpr
            text: content.entry.displaySize
            color: content.entry.itemMetadataColor
            horizontalAlignment: Text.AlignRight
            transform: Translate {
                x: sizeText.pixelCorrection.x
                y: sizeText.pixelCorrection.y
            }
            font.pixelSize:
                content.entry.panelRoot.detailsSecondaryFontPixelSize
        }
    }
}
