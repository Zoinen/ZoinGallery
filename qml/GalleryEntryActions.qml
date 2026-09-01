pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Item {
    id: actions
    objectName: "galleryEntryActions-" + entry.viewIndex

    required property var entry
    readonly property var panelRoot: entry.panelRoot
    readonly property var controller: panelRoot.controller
    property bool dragPrepared: false

    function proposedDropAction(drag) {
        if (drag.proposedAction === Qt.MoveAction
                && (drag.supportedActions & Qt.MoveAction))
            return Qt.MoveAction
        if (drag.proposedAction === Qt.CopyAction
                && (drag.supportedActions & Qt.CopyAction))
            return Qt.CopyAction
        if (drag.supportedActions & Qt.CopyAction)
            return Qt.CopyAction
        if (drag.supportedActions & Qt.MoveAction)
            return Qt.MoveAction
        return Qt.IgnoreAction
    }

    function prepareDrag(mouse) {
        if (mouse.button !== Qt.LeftButton || !controller.dragEnabled)
            return
        dragPrepared = controller.prepareDrag(
                    entry.viewIndex,
                    panelRoot.singleItemDragRequested(mouse.modifiers), 5)
        if (!dragPrepared)
            return
        dragSource.Drag.hotSpot.x = 18
        dragSource.Drag.hotSpot.y = 18
        dragPreview.visible = true
        dragPreview.grabToImage(result => {
            dragSource.Drag.imageSource = result.url
            dragPreview.visible = false
        })
    }

    Item {
        id: dragSource
        width: parent.width
        height: parent.height

        Drag.active: interaction.drag.active && actions.dragPrepared
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.mimeData: ({ "text/uri-list": actions.controller.dragUrls })
        Drag.onDragStarted:
            actions.controller.configureNativeDragCursors(dragSource)
        Drag.onDragFinished: dropAction => {
            actions.controller.finishExternalDrag(dropAction)
            actions.dragPrepared = false
            dragSource.x = 0
            dragSource.y = 0
            dragSource.Drag.imageSource = ""
        }
    }

    Item {
        id: dragPreview
        x: -width - 1000
        y: -height - 1000
        width: previewRow.implicitWidth + 12
        height: 58
        visible: false

        Rectangle {
            anchors.fill: parent
            radius: 6
            color: actions.panelRoot.overlayBackgroundColor
            border.width: 1
            border.color: actions.panelRoot.separatorColor
        }

        Row {
            id: previewRow
            anchors.centerIn: parent
            spacing: 6

            Repeater {
                model: actions.controller.dragPreviewModel

                Rectangle {
                    required property url imageSource
                    required property url iconSource
                    width: 46
                    height: 46
                    radius: 4
                    color: actions.panelRoot.itemBackgroundColor
                    border.width: 1
                    border.color: actions.panelRoot.separatorColor
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 3
                        source: parent.imageSource.toString() !== ""
                                ? parent.imageSource : parent.iconSource
                        sourceSize: Qt.size(width, height)
                        fillMode: parent.imageSource.toString() !== ""
                                  ? Image.PreserveAspectCrop
                                  : Image.PreserveAspectFit
                    }
                }
            }

            Rectangle {
                width: 46
                height: 46
                radius: 4
                color: actions.panelRoot.itemHoverColor
                border.width: 1
                border.color: actions.panelRoot.selectionColor
                visible: actions.controller.dragPreviewRemainingCount > 0

                Label {
                    anchors.centerIn: parent
                    text: "+" + actions.controller.dragPreviewRemainingCount
                    color: actions.panelRoot.foregroundColor
                    font.bold: true
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        z: 8
        radius: 6
        color: actions.panelRoot.selectionColor
        opacity: directoryDrop.containsDrag ? 0.22 : 0
        border.width: directoryDrop.containsDrag ? 2 : 0
        border.color: actions.panelRoot.selectionColor
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
        }
    }

    DropArea {
        id: directoryDrop
        anchors.fill: parent
        z: 9
        enabled: actions.controller.directoryDropEnabled
                 && actions.entry.isFolder

        onEntered: drag => {
            const action = actions.proposedDropAction(drag)
            if (drag.hasUrls && action !== Qt.IgnoreAction)
                drag.accept(action)
            else
                drag.accepted = false
        }
        onPositionChanged: drag => {
            const action = actions.proposedDropAction(drag)
            if (drag.hasUrls && action !== Qt.IgnoreAction)
                drag.accept(action)
            else
                drag.accepted = false
        }
        onDropped: drop => {
            const action = actions.proposedDropAction(drop)
            if (!drop.hasUrls || action === Qt.IgnoreAction) {
                drop.accepted = false
                return
            }
            const accepted = actions.controller.dropUrlsIntoDirectory(
                        drop.urls, actions.entry.viewIndex, action)
            if (accepted === Qt.IgnoreAction)
                drop.accepted = false
            else
                drop.accept(accepted)
        }
    }

    MouseArea {
        id: interaction
        anchors.fill: parent
        z: 10
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        drag.target: actions.controller.dragEnabled ? dragSource : undefined

        onPressed: mouse => {
            actions.panelRoot.handlePointerPress(
                        actions.entry.viewIndex, mouse.button,
                        mouse.modifiers)
            actions.prepareDrag(mouse)
            mouse.accepted = true
        }
        onPositionChanged: mouse => {
            if (drag.active
                    || !(mouse.buttons & (Qt.LeftButton | Qt.RightButton)))
                return
            const point = mapToItem(actions.panelRoot, mouse.x, mouse.y)
            actions.panelRoot.handlePointerDrag(point.x, point.y)
        }
        onReleased: actions.panelRoot.endPointerDrag()
        onCanceled: actions.panelRoot.endPointerDrag()
        onDoubleClicked: mouse => {
            if ((mouse.button & Qt.LeftButton) !== 0)
                actions.panelRoot.selectIndex(actions.entry.viewIndex, true)
            else if ((mouse.button & Qt.RightButton) !== 0)
                actions.panelRoot.invertPanelSelection()
            mouse.accepted = true
        }
    }

    Rectangle {
        objectName: "galleryRemoveEntry-" + actions.entry.viewIndex
        width: 24
        height: 24
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 7
        radius: 6
        z: 12
        visible: actions.controller.canRemoveEntries
        color: removePointer.containsMouse
               ? actions.panelRoot.itemHoverColor
               : actions.panelRoot.overlayBackgroundColor
        border.width: 1
        border.color: actions.panelRoot.separatorColor

        IconImage {
            anchors.centerIn: parent
            width: 14
            height: 14
            source: "qrc:/ZoinGallery/resources/WindowClose.svg"
            color: actions.panelRoot.foregroundColor
        }

        MouseArea {
            id: removePointer
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                actions.controller.removeEntry(actions.entry.viewIndex)
                actions.panelRoot.forceActiveFocus()
            }
        }

        ToolTip.visible: removePointer.containsMouse
        ToolTip.text: qsTr("Remove from selected images")
    }
}
