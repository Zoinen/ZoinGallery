pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

Item {
    id: preview
    objectName: "galleryFolderPreview-" + entry.viewIndex

    required property var entry
    readonly property var panelRoot: entry.panelRoot
    readonly property bool listPresentation: panelRoot.listView
    readonly property var childModel:
        panelRoot.controller.directoryPreviewModelAt(entry.viewIndex)

    Rectangle {
        anchors.fill: parent
        anchors.margins: 3
        radius: 6
        color: preview.panelRoot.directoryBackgroundColor
        border.width: 1
        border.color: preview.panelRoot.separatorColor
    }

    Label {
        id: title
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        height: Math.max(20, implicitHeight)
        text: preview.panelRoot.quickSearchFormatter.styledText(
                  preview.entry.effectiveDisplayName,
                  preview.entry.entryId, 0)
        textFormat: preview.panelRoot.quickSearchFormatter.matchForEntry(
                        preview.entry.entryId)
                    ? Text.StyledText : Text.PlainText
        color: preview.entry.itemTextColor
        elide: Text.ElideMiddle
        verticalAlignment: Text.AlignVCenter
    }

    GalleryViewportItem {
        id: children
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: title.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 7
        anchors.topMargin: 4
        clip: true
        persistSettings: false
        model: preview.childModel
        spacing: preview.listPresentation ? 2 : 1
        targetHeight: Math.max(12, height)
        showTransparentGrid: preview.panelRoot.showTransparentGrid

        delegate: Component {
            BrickItem {
                property var model

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: preview.panelRoot.itemBackgroundColor
                    border.width: 1
                    border.color: preview.panelRoot.separatorColor
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: model ? model.imageIdUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        cache: false
                    }
                }
            }
        }
    }
}
