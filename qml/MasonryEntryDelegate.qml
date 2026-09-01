pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: content
    required property GalleryEntryDelegateBase entry
    readonly property Item previewHost: null
    readonly property real paintedHeight: height

    Item {
        id: labelSurface
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        height: masonryLabel.implicitHeight + 12

        Rectangle {
            anchors.fill: parent
            color: content.entry.highlightLabelBackground
            radius: 3
        }

        Text {
            id: masonryLabel
            objectName: "galleryMasonryLabel-" + content.entry.viewIndex
            anchors.fill: parent
            anchors.margins: 6
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
