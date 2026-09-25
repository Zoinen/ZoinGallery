pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content

    required property GalleryEntryDelegateBase entry
    readonly property alias previewHost: detailsIconSlot
    readonly property real paintedHeight: height

    // MasonryLayout aligns the viewport and brick edges in C++. These local
    // calculations are independent of scrolling and host/panel placement.
    function snap(value) {
        return entry.panelRoot.fileFieldPresentationHelper.detailsPixelExtent(value)
    }

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
            objectName: "galleryBaseName-" + content.entry.viewIndex
            x: content.snap(detailsIconSlot.x + detailsIconSlot.width
                            + content.entry.panelRoot.detailsRowSpacing)
            y: content.snap((parent.height - height) / 2)
            height: content.entry.panelRoot.fileFieldPresentationHelper.detailsPixelExtent(
                        implicitHeight)
            width: Math.max(0,
                content.entry.panelRoot.fileFieldPresentationHelper.detailsPixelExtent(
                    (extensionText.visible ? extensionText.x : sizeText.x)
                    - baseNameText.x
                    - content.entry.panelRoot.detailsRowSpacing))
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
            font.pixelSize:
                content.entry.panelRoot.detailsNameFontPixelSize
        }

        Text {
            id: extensionText
            objectName: "galleryExtension-" + content.entry.viewIndex
            x: content.snap(sizeText.x - content.entry.panelRoot.detailsRowSpacing - width)
            y: content.snap((parent.height - height) / 2)
            visible: content.entry.panelRoot.separateFileExtensions
                     && content.entry.displayExtension.length > 0
            width: content.snap(Math.min(
                content.entry.panelRoot.detailsExtensionMaximumWidth,
                Math.max(content.entry.panelRoot.detailsExtensionMinimumWidth,
                         implicitWidth)))
            height: content.snap(implicitHeight)
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

        Repeater {
            id: detailsFieldColumnsRepeater
            model: content.entry.panelRoot.fileFieldPresentationHelper.detailsFieldColumns()

            delegate: Text {
                id: detailsFieldText
                required property int index
                required property var modelData
                readonly property var column: modelData.column
                readonly property string fieldId:
                    String(column.role || column.id || "")
                readonly property int schemaIndex:
                    Number(modelData.schemaIndex)
                objectName: "galleryFileField-" + fieldId + "-"
                            + content.entry.viewIndex
                x: content.entry.panelRoot.fileFieldPresentationHelper.detailsColumnX(schemaIndex)
                   + content.entry.panelRoot.detailsHeaderCellInset
                width: Math.max(
                    0, content.entry.panelRoot.fileFieldPresentationHelper.detailsPixelExtent(
                        content.entry.panelRoot.fileFieldPresentationHelper.detailsColumnWidth(schemaIndex)
                        - content.entry.panelRoot.detailsHeaderCellInset * 2))
                height: content.entry.panelRoot.fileFieldPresentationHelper.detailsPixelExtent(
                            implicitHeight)
                y: content.snap((parent.height - height) / 2)
                text: content.entry.panelRoot.fileFieldPresentationHelper.formatDetailsField(
                          fieldId,
                          content.entry.visualModel.displayFields[fieldId])
                color: content.entry.itemMetadataColor
                horizontalAlignment:
                    column.alignment === "right"
                    ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideRight
                font.pixelSize:
                    content.entry.panelRoot.detailsSecondaryFontPixelSize
            }
        }

        Text {
            id: sizeText
            objectName: "gallerySize-" + content.entry.viewIndex
            x: content.entry.panelRoot.fileFieldPresentationHelper.detailsColumnX(1)
            width: content.entry.panelRoot.fileFieldPresentationHelper.detailsColumnWidth(1)
            y: content.snap((parent.height - height) / 2)
            height: content.snap(implicitHeight)
            text: content.entry.displaySize
            color: content.entry.itemMetadataColor
            horizontalAlignment: Text.AlignRight
            font.pixelSize:
                content.entry.panelRoot.detailsSecondaryFontPixelSize
        }
    }
}
