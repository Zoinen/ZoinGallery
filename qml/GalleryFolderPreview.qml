pragma ComponentBehavior: Bound
import QtQuick
import ZoinGallery.Native 1.0

Item {
    id: preview
    objectName: "galleryFolderPreview-" + entry.viewIndex
    required property var entry
    readonly property var panelRoot: entry.panelRoot
    readonly property var childModel: panelRoot.controller.directoryPreviewModelAt(entry.viewIndex)
    readonly property real dpr: entry.renderDpr
    readonly property real outerSpacing: panelRoot.itemSpacing
    readonly property real contentMargin: snap(outerSpacing / 2)
    readonly property real folderTopOffset: snap(Math.round(width / 20))
    readonly property bool darkTheme: Qt.styleHints.colorScheme !== Qt.Light
    readonly property color folderColor: darkTheme ? "#397db1" : "#397db2"
    property int readyImages: 0
    readonly property bool hasUsablePreview: readyImages > 0
        || Boolean(childModel && childModel.hasPublishedThumbnails)
    readonly property point sceneOrigin: {
        let dependency = entry.iconSceneOrigin.x + entry.iconSceneOrigin.y
        let ancestor = preview
        while (ancestor) {
            dependency += ancestor.x + ancestor.y + ancestor.scale + ancestor.rotation
            ancestor = ancestor.parent
        }
        return preview.mapToItem(null, dependency * 0, dependency * 0)
    }
    function snap(value) { return Math.round(value * dpr) / dpr }

    Item {
        id: content
        opacity: preview.hasUsablePreview ? 1 : 0
        x: preview.snap(preview.sceneOrigin.x + preview.contentMargin) - preview.sceneOrigin.x
        y: preview.snap(preview.sceneOrigin.y + preview.contentMargin) - preview.sceneOrigin.y
        width: Math.max(0, preview.snap(preview.width - x - preview.contentMargin))
        height: Math.max(0, preview.snap(preview.height - y - preview.contentMargin))
        Rectangle {
            id: tab
            objectName: "folderPreviewTab"
            width: preview.snap(Math.min(110, content.width * 0.44))
            height: preview.folderTopOffset + preview.snap(20)
            radius: 4
            color: frame.color
        }
        Rectangle {
            id: frame
            objectName: "folderPreviewFrame"
            y: preview.folderTopOffset
            width: content.width
            height: Math.max(0, content.height - y - title.height - preview.snap(preview.outerSpacing))
            radius: 4
            color: preview.folderColor
            Rectangle {
                objectName: "folderPreviewFill"
                x: 1 / preview.dpr
                y: 1 / preview.dpr
                width: Math.max(0, parent.width - 2 / preview.dpr)
                height: Math.max(0, parent.height - 2 / preview.dpr)
                radius: 4
                color: preview.darkTheme ? "#304051" : "#60b0eb"
            }
        }
        Text {
            id: title
            objectName: "folderPreviewTitle"
            x: preview.contentMargin
            y: content.height - height - preview.contentMargin
            width: Math.max(0, content.width - preview.contentMargin * 2)
            height: preview.snap(implicitHeight)
            text: preview.panelRoot.quickSearchFormatter.styledText(preview.entry.effectiveDisplayName, preview.entry.entryId, 0)
            textFormat: preview.panelRoot.quickSearchFormatter.matchForEntry(preview.entry.entryId) ? Text.StyledText : Text.PlainText
            color: preview.entry.itemTextColor
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 2
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
        }
        GalleryViewportItem {
            id: children
            objectName: "folderPreviewGrid"
            x: frame.x + preview.snap(2)
            y: frame.y + preview.snap(2)
            width: Math.max(0, frame.width - preview.snap(2) * 2)
            height: Math.max(0, frame.height - preview.snap(2) * 2)
            clip: true
            persistSettings: false
            containedPreview: true
            animateResizing: false
            devicePixelRatio: preview.dpr
            model: preview.childModel
            spacing: 1
            targetHeight: height
            delegate: Component {
                BrickItem {
                    id: cell
                    property var model
                    property bool masonryGeometryReady: false
                    Image {
                        id: thumbnail
                        objectName: "folderPreviewImage-" + cell.viewIndex
                        property bool countedReady: false
                        function updateReady() {
                            const ready = visible && status === Image.Ready && width > 0 && height > 0
                            if (ready === countedReady) return
                            preview.readyImages += ready ? 1 : -1
                            countedReady = ready
                        }
                        onStatusChanged: updateReady()
                        onWidthChanged: updateReady()
                        onHeightChanged: updateReady()
                        onVisibleChanged: updateReady()
                        Component.onDestruction: { if (countedReady) --preview.readyImages }
                        anchors.fill: parent
                        source: cell.masonryGeometryReady && cell.model ? cell.model.imageIdUrl : ""
                        fillMode: Image.Stretch
                        cache: false
                    }
                }
            }
        }
    }
}
