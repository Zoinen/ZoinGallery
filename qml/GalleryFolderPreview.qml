pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ZoinGallery.Native 1.0

Item {
    id: preview
    objectName: "galleryFolderPreview-" + entry.viewIndex
    required property var entry
    readonly property var panelRoot: entry.panelRoot
    readonly property var childModel: panelRoot.controller.directoryPreviewModelAt(entry.viewIndex)
    readonly property real dpr: entry.renderDpr
    property int readyImages: 0
    readonly property bool hasUsablePreview: readyImages > 0
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
        x: preview.snap(preview.sceneOrigin.x + 3) - preview.sceneOrigin.x
        y: preview.snap(preview.sceneOrigin.y + 3) - preview.sceneOrigin.y
        width: Math.max(0, preview.snap(preview.width - x - 3))
        height: Math.max(0, preview.snap(preview.height - y - 3))
        Rectangle {
            id: tab
            objectName: "folderPreviewTab"
            width: preview.snap(Math.min(110, content.width * 0.44))
            height: preview.snap(24)
            radius: 4
            color: frame.color
            border.width: 1 / preview.dpr
            border.color: frame.border.color
        }
        Rectangle {
            id: frame
            objectName: "folderPreviewFrame"
            y: preview.snap(10)
            width: content.width
            height: Math.max(0, title.y - y - preview.snap(4))
            radius: 4
            color: preview.panelRoot.directoryBackgroundColor
            border.width: 1 / preview.dpr
            border.color: preview.panelRoot.separatorColor
        }
        Label {
            id: title
            objectName: "folderPreviewTitle"
            x: preview.snap(2)
            y: content.height - height
            width: Math.max(0, content.width - preview.snap(4))
            height: preview.snap(implicitHeight)
            padding: 0
            text: preview.panelRoot.quickSearchFormatter.styledText(preview.entry.effectiveDisplayName, preview.entry.entryId, 0)
            textFormat: preview.panelRoot.quickSearchFormatter.matchForEntry(preview.entry.entryId) ? Text.StyledText : Text.PlainText
            color: preview.entry.itemTextColor
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignTop
        }
        GalleryViewportItem {
            id: children
            objectName: "folderPreviewGrid"
            x: frame.x + preview.snap(3)
            y: frame.y + preview.snap(3)
            width: Math.max(0, frame.width - preview.snap(3) * 2)
            height: Math.max(0, frame.height - preview.snap(3) * 2)
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
