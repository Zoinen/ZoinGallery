import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import ZoinGallery 1.0
import ZoinGallery.Native 1.0

Rectangle {
    id: panel

    signal closeRequested()
    signal imageActivated(int index)
    property bool transparentGrid: false
    property alias viewerTransitionActive: selectedMasonry.viewerTransitionActive
    readonly property int count: selectedMasonry.view.count
    property var contextGroup: null
    property string pendingFileAction: ""
    property var skippedMovePaths: []
    property bool skipAllMoveErrors: false
    property bool pendingFileActionSkippable: false
    property string pendingSkipPath: ""
    property string fileActionFeedback: ""
    property alias masonryMode: selectedMasonry

    color: Style.masonryViewBackground
    border.width: 1
    border.color: Style.masonryViewBackgroundBorder
    radius: 7
    clip: true

    function scheduleThumbnailReload() {
        if (visible && width > 0 && height > 0 && count > 0) {
            thumbnailReloadTimer.restart()
        }
    }

    onVisibleChanged: scheduleThumbnailReload()
    onWidthChanged: scheduleThumbnailReload()
    onHeightChanged: scheduleThumbnailReload()

    Shortcut {
        sequence: StandardKey.Undo
        enabled: panel.visible && fileListModel.canUndoSelectionGroupMove
        onActivated: panel.runFileAction("undo")
    }

    function resetForActiveGroup() {
        Qt.callLater(function() {
            selectedMasonry.view.contentY = 0
            if (panel.count > 0) {
                selectedMasonry.setCurrentIndex(0)
            }
            else {
                selectedMasonry.view.currentIndex = -1
            }
            scheduleThumbnailReload()
        })
    }

    function commitGroupRename() {
        if (renameAcceptButton.inactive || !contextGroup) {
            return
        }
        if (fileListModel.renameSelectionGroup(contextGroup.id, renameField.text)) {
            renamePopup.close()
        }
    }

    function runFileAction(action) {
        const result = action === "undo"
                ? fileListModel.undoLastSelectionGroupMove()
                : skipAllMoveErrors
                  ? fileListModel.moveActiveSelectionGroupToCurrentFolderSkippingAll(
                        skippedMovePaths)
                  : fileListModel.moveActiveSelectionGroupToCurrentFolderSkipping(
                        skippedMovePaths)
        if (result.success) {
            fileActionFeedback = result.message || ""
            fileActionFeedbackTimer.restart()
            skippedMovePaths = []
            skipAllMoveErrors = false
            pendingFileActionSkippable = false
            pendingSkipPath = ""
            fileActionErrorPopup.close()
            return
        }
        pendingFileAction = action
        pendingFileActionSkippable = Boolean(result.skippable)
        pendingSkipPath = result.skipPath || ""
        fileActionErrorTitle.text = result.title || "File operation failed"
        fileActionErrorMessage.text = result.message || "No files were changed."
        fileActionErrorPopup.open()
    }

    Connections {
        target: fileListModel

        function onActiveSelectionGroupChanged() {
            panel.resetForActiveGroup()
        }
    }

    Timer {
        id: thumbnailReloadTimer
        interval: 220
        onTriggered: {
            if (panel.visible && panel.width > 0 && panel.height > 0 && panel.count > 0) {
                selectedMasonry.view.reReadAndDecodeThumbnails()
            }
        }
    }

    Timer {
        id: copyFeedbackTimer
        interval: 1400
    }

    Timer {
        id: fileActionFeedbackTimer
        interval: 2400
    }

    Menu {
        id: groupMenu

        MenuItem {
            text: "Rename group"
            onTriggered: {
                if (panel.contextGroup) {
                    renameField.text = panel.contextGroup.name
                    renamePopup.open()
                    renameField.forceActiveFocus()
                    renameField.selectAll()
                }
            }
        }

        MenuItem {
            text: "Delete group"
            enabled: Boolean(panel.contextGroup && !panel.contextGroup.isDefault)
            onTriggered: {
                if (!panel.contextGroup) {
                    return
                }
                if (panel.contextGroup.storedCount > 0) {
                    deletePopup.open()
                }
                else {
                    fileListModel.removeSelectionGroup(panel.contextGroup.id)
                }
            }
        }
    }

    Popup {
        id: renamePopup
        width: Math.min(320, panel.width - 24)
        height: 142
        x: Math.round((panel.width - width) / 2)
        y: Math.round((panel.height - height) / 2)
        modal: true
        focus: true
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Style.popupBackground
            border.color: Style.popupBorder
            radius: 8
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: "Rename selection group"
                color: Style.text
                font.bold: true
            }

            TextField {
                id: renameField
                Layout.fillWidth: true
                maximumLength: 60
                onAccepted: panel.commitGroupRename()
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 6

                Button {
                    text: "Cancel"
                    onClicked: renamePopup.close()
                }

                Button {
                    id: renameAcceptButton
                    text: "Rename"
                    inactive: renameField.text.trim().length === 0
                    onClicked: panel.commitGroupRename()
                }
            }
        }
    }

    Popup {
        id: deletePopup
        width: Math.min(340, panel.width - 24)
        height: 154
        x: Math.round((panel.width - width) / 2)
        y: Math.round((panel.height - height) / 2)
        modal: true
        focus: true
        padding: 14
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Style.popupBackground
            border.color: Style.popupBorder
            radius: 8
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: panel.contextGroup
                      ? "Delete “" + panel.contextGroup.name + "”?"
                      : "Delete selection group?"
                color: Style.text
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: panel.contextGroup && panel.contextGroup.storedCount > 0
                      ? panel.contextGroup.storedCount + " selected item" +
                        (panel.contextGroup.storedCount === 1 ? "" : "s") +
                        " will be deselected."
                      : "This group is empty."
                color: Style.viewerSecondaryText
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 6

                Button {
                    text: "Cancel"
                    onClicked: deletePopup.close()
                }

                Button {
                    text: "Delete"
                    onClicked: {
                        if (panel.contextGroup &&
                                fileListModel.removeSelectionGroup(
                                    panel.contextGroup.id)) {
                            deletePopup.close()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: fileActionErrorPopup
        width: Math.min(390, panel.width - 24)
        height: Math.max(172, fileActionErrorContent.implicitHeight + 28)
        x: Math.round((panel.width - width) / 2)
        y: Math.round((panel.height - height) / 2)
        modal: true
        focus: true
        padding: 14
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Style.popupBackground
            border.color: Style.popupBorder
            radius: 8
        }

        ColumnLayout {
            id: fileActionErrorContent
            anchors.fill: parent
            spacing: 10

            Text {
                id: fileActionErrorTitle
                Layout.fillWidth: true
                color: Style.text
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                id: fileActionErrorMessage
                Layout.fillWidth: true
                color: Style.viewerSecondaryText
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: "What would you like to do?"
                color: Style.viewerSecondaryText
                font.pixelSize: 11
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 6

                Button {
                    text: "Cancel"
                    onClicked: fileActionErrorPopup.close()
                }

                Button {
                    text: "Skip"
                    visible: panel.pendingFileAction === "move" &&
                             panel.pendingFileActionSkippable &&
                             panel.pendingSkipPath.length > 0
                    onClicked: {
                        const nextSkippedPaths =
                                panel.skippedMovePaths.slice()
                        if (nextSkippedPaths.indexOf(
                                    panel.pendingSkipPath) < 0) {
                            nextSkippedPaths.push(panel.pendingSkipPath)
                        }
                        panel.skippedMovePaths = nextSkippedPaths
                        panel.runFileAction("move")
                    }
                }

                Button {
                    text: "Skip all"
                    visible: panel.pendingFileAction === "move" &&
                             panel.pendingFileActionSkippable &&
                             panel.pendingSkipPath.length > 0
                    onClicked: {
                        panel.skipAllMoveErrors = true
                        panel.runFileAction("move")
                    }
                }

                Button {
                    text: "Retry"
                    onClicked: panel.runFileAction(panel.pendingFileAction)
                }
            }
        }
    }

    Connections {
        target: selectedImagesModel

        function onThumbnailReloadRequested() {
            panel.scheduleThumbnailReload()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: 12
            Layout.rightMargin: 6
            spacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: fileListModel.activeSelectionGroupName
                    color: Style.text
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    text: panel.count + " image" + (panel.count === 1 ? "" : "s") +
                          (selectedImagesModel.unavailableCount > 0
                           ? "  •  " + selectedImagesModel.unavailableCount +
                             " unavailable"
                           : "") +
                          (selectedImagesModel.selectedCount > 0
                           ? "  •  " + selectedImagesModel.selectedCount + " selected"
                           : "")
                    color: Style.viewerSecondaryText
                    font.pixelSize: 11
                }
            }

            Button {
                implicitWidth: 32
                implicitHeight: 32
                icon.source: "qrc:/ZoinGallery/resources/Copy.svg"
                icon.width: 16
                icon.height: 16
                icon.color: Style.text
                inactive: selectedImagesModel.totalPathCount === 0
                onClicked: {
                    if (!inactive &&
                            fileListModel.copyActiveSelectionGroupPaths() > 0) {
                        copyFeedbackTimer.restart()
                    }
                }
                ToolTip.text: copyFeedbackTimer.running
                              ? "Paths copied"
                              : "Copy all full paths in this group, one per line"
            }

            Button {
                implicitWidth: 32
                implicitHeight: 32
                icon.source: "qrc:/ZoinGallery/resources/FolderIcon.svg"
                icon.width: 17
                icon.height: 17
                icon.color: Style.text
                inactive: selectedImagesModel.totalPathCount === 0
                onClicked: {
                    if (!inactive) {
                        panel.skippedMovePaths = []
                        panel.skipAllMoveErrors = false
                        panel.runFileAction("move")
                    }
                }
                ToolTip.text: fileActionFeedbackTimer.running
                              ? panel.fileActionFeedback
                              : "Move this group into a new folder in the current path"
            }

            Button {
                implicitWidth: 32
                implicitHeight: 30
                icon.source: "qrc:/ZoinGallery/resources/SelectionHistory.svg"
                icon.width: 17
                icon.height: 17
                icon.color: Style.text
                visible: fileListModel.canUndoSelectionGroupMove
                onClicked: panel.runFileAction("undo")
                ToolTip.text: fileActionFeedbackTimer.running
                              ? panel.fileActionFeedback
                              : "Undo last group move (Ctrl+Z)"
            }

            Button {
                text: "Deselect"
                implicitHeight: 30
                visible: selectedImagesModel.selectedCount > 0
                onClicked: selectedImagesModel.clearSelection()
                ToolTip.text: "Clear panel selection"
            }

            Button {
                implicitWidth: 32
                implicitHeight: 32
                icon.source: "qrc:/ZoinGallery/resources/PanelClose.svg"
                icon.width: 16
                icon.height: 16
                icon.color: Style.text
                onClicked: panel.closeRequested()
                ToolTip.text: "Close selected images panel"
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 42

            ListView {
                id: groupTabs
                anchors {
                    left: parent.left
                    right: addGroupButton.left
                    top: parent.top
                    bottom: parent.bottom
                    leftMargin: 8
                    rightMargin: 4
                }
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: fileListModel.selectionGroups

                delegate: Rectangle {
                    id: groupTab
                    required property var modelData

                    width: Math.max(92, Math.min(150, groupLabel.implicitWidth + 38))
                    height: 30
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    radius: 6
                    color: modelData.active
                           ? Qt.rgba(modelData.color.r, modelData.color.g,
                                     modelData.color.b, 0.17)
                           : groupMouse.containsMouse ? Style.lighter : "transparent"
                    border.width: modelData.active ? 2 : 1
                    border.color: modelData.active ? modelData.color : Style.lighter2
                    ToolTip.visible: groupMouse.containsMouse &&
                                         modelData.unavailableCount > 0
                    ToolTip.delay: 500
                    ToolTip.text: modelData.unavailableCount +
                                      " selected path" +
                                      (modelData.unavailableCount === 1 ? "" : "s") +
                                      " unavailable"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: groupTab.modelData.color
                        }

                        Text {
                            id: groupLabel
                            Layout.fillWidth: true
                            text: groupTab.modelData.name + "  " + groupTab.modelData.count
                            color: Style.text
                            elide: Text.ElideRight
                            font.pixelSize: 12
                        }
                    }

                    MouseArea {
                        id: groupMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                panel.contextGroup = groupTab.modelData
                                groupMenu.popup()
                            }
                            else {
                                fileListModel.activateSelectionGroup(groupTab.modelData.id)
                            }
                        }
                    }
                }
            }

            Button {
                id: addGroupButton
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 32
                implicitHeight: 32
                text: "+"
                font.pixelSize: 20
                inactive: !fileListModel.canAddSelectionGroup
                onClicked: {
                    if (!inactive) {
                        fileListModel.addSelectionGroup()
                    }
                }
                ToolTip.text: inactive ? "All selection colors are in use"
                                       : "Add selection group"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Style.lighter2
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            MasonryMode {
                id: selectedMasonry
                anchors.fill: parent

                primaryView: false
                selectionInteractionEnabled: true
                quickSearchEnabled: false
                masonryModel: selectedImagesModel
                selectionModel: selectedImagesModel
                selectionMapper: selectedImagesModel
                targetHeight: 112
                masonrySpacing: 8
                selectionAccentColor: fileListModel.activeSelectionGroupColor
                view.showTransparentGrid: panel.transparentGrid

                onCurrentSelectionToggleRequested: (index) => {
                    selectedImagesModel.toggleSelection(index)
                }
                onCurrentIndexActivated: (index) => panel.imageActivated(index)

                masonryDelegate: BrickItem {
                    id: selectedBrick

                    property var model
                    property int viewIndex: -1
                    property int sourceIndex: -1

                    Item {
                        id: dragItem
                        anchors.fill: parent
                        visible: !(viewIndex ===
                                   selectedMasonry.view.currentIndex &&
                                   selectedMasonry.viewerTransitionActive)

                        property var urls: []
                        property var previewItems: []
                        property int remainingCount: 0

                        Drag.active: selectedMouse.drag.active
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
                        Drag.mimeData: ({ "text/uri-list": urls })
                        Drag.onDragStarted: fileListModel.configureNativeDragCursors(dragItem)
                        Drag.onDragFinished: (dropAction) => {
                            fileListModel.finalizeExternalDrag(urls, dropAction)
                        }

                        function prepareDrag(singleItemOnly) {
                            urls = selectedImagesModel.dragUrlsForIndex(viewIndex, singleItemOnly)
                            const preview = selectedImagesModel.dragPreviewItemsForIndex(
                                viewIndex, 4, singleItemOnly)
                            previewItems = preview.items || []
                            remainingCount = preview.remainingCount || 0
                            dragPreview.visible = true
                            Qt.callLater(function() {
                                dragPreview.grabToImage(function(result) {
                                    dragItem.Drag.imageSource = result.url
                                    dragPreview.visible = false
                                })
                            })
                        }

                        Rectangle {
                            id: currentOutline
                            anchors.fill: selectedContent
                            anchors.margins: -2
                            radius: 5
                            color: Style.brickImageSelected
                            visible: viewIndex === selectedMasonry.view.currentIndex
                        }

                        Rectangle {
                            id: selectedContent
                            anchors.fill: parent
                            anchors.margins: selectedMasonry.view.spacing / 2
                            radius: 5
                            color: Style.darker
                            border.width: 1
                            border.color: Style.lighter2
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                source: model ? model.imageIdUrl : ""
                                fillMode: Image.PreserveAspectCrop
                                cache: false
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: selectedName.height + 10
                                color: Style.opaqueMasonryViewBackgroundWithOpacity

                                Text {
                                    id: selectedName
                                    anchors.left: parent.left
                                    anchors.right: removeButton.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 4
                                    text: model ? model.text : ""
                                    color: Style.text
                                    elide: Text.ElideMiddle
                                    maximumLineCount: 1
                                }

                                Button {
                                    id: removeButton
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 28
                                    height: 28
                                    icon.source: "qrc:/ZoinGallery/resources/WindowClose.svg"
                                    onClicked: selectedImagesModel.removeFromCollection(viewIndex)
                                    ToolTip.text: "Remove from selected images"
                                    z: 3
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.width: 3
                                border.color: fileListModel.activeSelectionGroupColor
                                radius: 5
                                visible: Boolean(model && model.isSelected)
                                z: 4
                            }
                        }

                        Item {
                            id: dragPreview
                            x: -width - 1000
                            y: -height - 1000
                            width: previewRow.implicitWidth + 12
                            height: 56
                            visible: false

                            Rectangle {
                                anchors.fill: parent
                                radius: 6
                                color: Style.popupBackground
                                border.width: 1
                                border.color: Style.popupBorder
                            }

                            Row {
                                id: previewRow
                                anchors.centerIn: parent
                                spacing: 5

                                Repeater {
                                    model: dragItem.previewItems

                                    Image {
                                        width: 44
                                        height: 44
                                        source: modelData.imageIdUrl
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                }

                                Rectangle {
                                    width: 44
                                    height: 44
                                    radius: 4
                                    visible: dragItem.remainingCount > 0
                                    color: Style.lighter
                                    border.width: 1
                                    border.color: fileListModel.activeSelectionGroupColor

                                    Text {
                                        anchors.centerIn: parent
                                        text: "+" + dragItem.remainingCount
                                        color: Style.text
                                        font.bold: true
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: selectedMouse
                            anchors.fill: parent
                            z: -1
                            hoverEnabled: true
                            drag.target: dragItem

                            onPressed: (mouse) => {
                                dragItem.prepareDrag(
                                            selectedMasonry.singleItemDragRequested(
                                                mouse.modifiers))
                                dragItem.Drag.hotSpot.x = 18
                                dragItem.Drag.hotSpot.y = 18
                                selectedMasonry.handleItemPressed(viewIndex, mouse.modifiers)
                            }

                            onDoubleClicked: {
                                selectedMasonry.setCurrentIndex(viewIndex)
                                panel.imageActivated(viewIndex)
                            }
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: panel.count === 0

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 36
                    height: 36
                    source: "qrc:/ZoinGallery/resources/ImageIcon.svg"
                    opacity: 0.45
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: selectedImagesModel.unavailableCount > 0
                          ? selectedImagesModel.unavailableCount +
                            " selected file" +
                            (selectedImagesModel.unavailableCount === 1 ? " is" : "s are") +
                            " unavailable"
                          : "No images in " + fileListModel.activeSelectionGroupName
                    color: Style.viewerSecondaryText
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: selectedImagesModel.unavailableCount > 0
                    text: "They may have been moved or deleted."
                    color: Style.viewerSecondaryText
                    opacity: 0.75
                    font.pixelSize: 11
                }
            }
        }
    }
}
