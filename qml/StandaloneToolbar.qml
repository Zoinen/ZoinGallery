import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import Qt.labs.platform as Platform

import ZoinGallery 1.0
import ZoinGallery.Native 1.0

pragma ComponentBehavior: Bound

Item {
    id: toolbar

    required property Item shell
    required property QtObject hostWindow
    required property Item titleBarItem
    required property Item systemButtonsLayout
    required property QtObject windowAgentObject
    required property Item galleryLayout
    required property QtObject navigationController
    required property QtObject catalogModel
    required property QtObject viewModel
    required property Window settingsWindow
    required property Window historyWindow
    required property bool quickWindowKitEnabled
    property real devicePixelRatio: 1.0

    readonly property alias controlLayout: toolbarLayout
    readonly property alias zoomSlider: masonryZoomSlider

    Layout.fillWidth: true
    Layout.preferredHeight: toolbar.titleBarItem.thumbnailsHeight
    z: 1

    MultiEffect {
        id: titleBarBlurBehind
        source: ShaderEffectSource {
            sourceItem: toolbar.galleryLayout
            width: titleBarBlurBehind.width
            height: titleBarBlurBehind.height
            sourceRect: Qt.rect(0, -height, width, height)
        }

        anchors.fill: parent
        opacity: 0.3
        contrast: Style.isDarkTheme ? -0.5 : 0
        brightness: Style.isDarkTheme ? 0 : 0.3
        saturation: toolbar.hostWindow.active ? 0 : -1
        Behavior on saturation {
            NumberAnimation {
                duration: 300
                easing.type: Easing.InOutQuad
            }
        }

        colorization: Style.isDarkTheme ? 0.4 : 0
        colorizationColor: Style.windowBackgroundNoQWK
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: 64
        blur: 1.0
    }

    RowLayout {
        id: toolbarLayout
        anchors.fill: parent
        anchors.leftMargin: toolbar.hostWindow.macTitleBarLeftPadding + toolbar.hostWindow.thumbnailsTitleBarSidePadding
        anchors.rightMargin: (toolbar.quickWindowKitEnabled && !toolbar.hostWindow.useMacNativeTitleBar ? toolbar.systemButtonsLayout.width : 0) + toolbar.hostWindow.thumbnailsTitleBarSidePadding
        spacing: 0
        clip: true

        component Separator : Rectangle {
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            implicitWidth: 1
            implicitHeight: 32
            color: Style.lighter2
        }

        component ToolbarButton : Button {
            id: toolbarButton
            Layout.alignment: Qt.AlignVCenter

            implicitWidth: 36
            implicitHeight: toolbar.titleBarItem.thumbnailsHeight

            signal rightReleased

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: toolbarButton.rightReleased()
            }
        }

        Button {
            id: appIcon
            implicitWidth: 46
            implicitHeight: parent.height
            visible: toolbar.quickWindowKitEnabled && !toolbar.hostWindow.useMacNativeTitleBar

            icon.width: 18
            icon.height: 18

            colorfulIcon: true
            icon.source: "qrc:/ZoinGallery/resources/Logo.svg"
            onClicked: {
                // toolbar.windowAgentObject.showSystemMenu(mapToGlobal(0, height))
                // toolbar.catalogModel.startScanner()
                toolbar.settingsWindow.open()
            }
            // Component.onCompleted: toolbar.windowAgentObject.setSystemButton(WindowAgent.WindowIcon, appIcon)
        }

        Text {
            property bool isPartOfTitleBar: true
            visible: toolbar.quickWindowKitEnabled

            Layout.rightMargin: 15

            text: "ZoinGallery"
            color: Style.text

            opacity: toolbar.hostWindow.active ? 1 : 0.5
            Behavior on opacity {
                NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
            }
        }

        ToolbarButton {
            icon.source: "qrc:/ZoinGallery/resources/Back.svg"
            ToolTip.text: "Go Back\tAlt+←"
            inactive: !toolbar.navigationController.canBack

            implicitWidth: 46
            centerOffset: 5
            leftPadding: 18

            onReleased: {
                if (inactive) {
                    return
                }

                toolbar.navigationController.saveCurrentState(toolbar.galleryLayout.view.contentY, toolbar.galleryLayout.view.currentIndex)
                toolbar.navigationController.back()
                toolbar.galleryLayout.loadSavedState()
            }

            onRightReleased: {
                if (inactive) {
                    return
                }

                backMenu.popup()
            }

            Menu {
                id: backMenu

                Timer {
                    id: delayedBackRightClick
                    property int index: -1
                    interval: 0
                    onTriggered: {
                        toolbar.navigationController.saveCurrentState(toolbar.galleryLayout.view.contentY, toolbar.galleryLayout.view.currentIndex)
                        toolbar.navigationController.jumpBack(index)
                        toolbar.galleryLayout.loadSavedState()
                    }
                }

                Repeater {
                    model: toolbar.navigationController.backMenu

                    MenuItem {
                        text: modelData
                        onTriggered: {
                            delayedBackRightClick.index = index
                            delayedBackRightClick.start()
                            toolbar.galleryLayout.focusProxy.forceActiveFocus()
                        }
                    }
                }
            }
        }

        ToolbarButton {
            icon.source: "qrc:/ZoinGallery/resources/Forward.svg"
            ToolTip.text: "Go Forward\tAlt+→"
            inactive: !toolbar.navigationController.canForward

            onReleased: {
                if (inactive) {
                    return
                }

                toolbar.navigationController.saveCurrentState(toolbar.galleryLayout.view.contentY, toolbar.galleryLayout.view.currentIndex)
                toolbar.navigationController.forward()
                toolbar.galleryLayout.loadSavedState()
            }

            onRightReleased: {
                if (inactive) {
                    return
                }

                forwardMenu.popup()
            }

            Menu {
                id: forwardMenu

                Timer {
                    id: delayedForwardRightClick
                    property int index: -1
                    interval: 0
                    onTriggered: {
                        toolbar.navigationController.saveCurrentState(toolbar.galleryLayout.view.contentY, toolbar.galleryLayout.view.currentIndex)
                        toolbar.navigationController.jumpForward(index)
                        toolbar.galleryLayout.loadSavedState()
                    }
                }

                Repeater {
                    model: toolbar.navigationController.forwardMenu

                    MenuItem {
                        text: modelData
                        onTriggered: {
                            delayedForwardRightClick.index = index
                            delayedForwardRightClick.start()
                            toolbar.galleryLayout.focusProxy.forceActiveFocus()
                        }
                    }
                }
            }
        }

        ToolbarButton {
            icon.source: "qrc:/ZoinGallery/resources/Up.svg"
            ToolTip.text: "Go Up\tBackspace"
            inactive: !toolbar.navigationController.canUp

            onReleased: {
                toolbar.galleryLayout.disableAnimation = true
                toolbar.navigationController.saveCurrentState(toolbar.galleryLayout.view.contentY, toolbar.galleryLayout.view.currentIndex)
                toolbar.galleryLayout.setCurrentIndex(toolbar.navigationController.up())
                toolbar.galleryLayout.disableAnimation = false
                toolbar.galleryLayout.loadSavedState()
            }
        }

        PathControl {
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            clip: true

            onEditModeChanged: {
                if (!editMode) {
                    toolbar.galleryLayout.focusProxy.forceActiveFocus()
                }
            }
            Layout.fillWidth: true
            Layout.preferredHeight: 32

            text: toolbar.navigationController.currentPath
        }

        ToolbarButton {
            id: createButton
            implicitWidth: 44
            icon.source: "qrc:/ZoinGallery/resources/FolderIcon.svg"
            ToolTip.text: toolbar.catalogModel.fileDragActive
                          ? "Drop here to create a folder for these items"
                          : "Create…"

            onReleased: createMenu.popup()

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                z: 10
                radius: 6
                color: Style.persistentSelectionBorder
                opacity: createDropArea.containsDrag
                         ? 0.3
                         : (toolbar.catalogModel.fileDragActive ? 0.12 : 0)
                border.width: createDropArea.containsDrag ? 2 : 1
                border.color: Style.persistentSelectionBorder
                visible: opacity > 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 140
                        easing.type: Easing.OutQuad
                    }
                }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 7
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 7
                z: 11
                text: "+"
                color: Style.text
                font.bold: true
                font.pixelSize: 13
            }

            DropArea {
                id: createDropArea
                anchors.fill: parent
                z: 20

                function proposedFileAction(drag) {
                    if (drag.proposedAction === Qt.MoveAction &&
                            (drag.supportedActions & Qt.MoveAction)) {
                        return Qt.MoveAction
                    }
                    if (drag.supportedActions & Qt.CopyAction) {
                        return Qt.CopyAction
                    }
                    if (drag.supportedActions & Qt.MoveAction) {
                        return Qt.MoveAction
                    }
                    return Qt.IgnoreAction
                }

                onEntered: (drag) => {
                    const action = proposedFileAction(drag)
                    if (!drag.hasUrls || action === Qt.IgnoreAction) {
                        drag.accepted = false
                    }
                    else {
                        drag.accept(action)
                    }
                }
                onPositionChanged: (drag) => {
                    const action = proposedFileAction(drag)
                    if (drag.hasUrls && action !== Qt.IgnoreAction) {
                        drag.accept(action)
                    }
                    else {
                        drag.accepted = false
                    }
                }
                onDropped: (drop) => {
                    const action = proposedFileAction(drop)
                    if (!drop.hasUrls || action === Qt.IgnoreAction) {
                        drop.accepted = false
                        return
                    }
                    const urls = drop.urls
                    // The actual move is deferred until the name dialog is
                    // confirmed. Report Copy (or Ignore) now so the drag
                    // source cannot delete the originals prematurely.
                    if (drop.supportedActions & Qt.CopyAction) {
                        drop.accept(Qt.CopyAction)
                    }
                    else {
                        drop.accepted = false
                    }
                    toolbar.shell.beginFolderCreation(urls, action)
                }
            }

            Menu {
                id: createMenu

                MenuItem {
                    text: "Folder…"
                    onTriggered: toolbar.shell.beginFolderCreation([], Qt.CopyAction)
                }

                MenuSeparator {}

                MenuItem {
                    text: "Selection group"
                    enabled: toolbar.catalogModel.canAddSelectionGroup
                    onTriggered: {
                        toolbar.catalogModel.addSelectionGroup()
                        toolbar.shell.selectedImagesPanelOpen = true
                    }
                }
            }
        }

        Shortcut {
            sequence: "F12"
            onActivated: {
                toolbar.catalogModel.runningTasksDebug = !toolbar.catalogModel.runningTasksDebug
            }
        }

        Component {
            id: runningTasksDebugView
            Text {
                id: runningTasks

                Connections {
                    target: toolbar.catalogModel
                    function onRunningTasksChanged(tasks, tasksInfo) {
                        runningTasks.text = tasks
                        infoWindow.title = tasks
                        infoList.model = tasksInfo
                    }
                }
                text: "0/0"
                color: Style.text
                Layout.preferredWidth: 45
                Layout.rightMargin: 5
                horizontalAlignment: Text.AlignRight

                Window {
                    id: infoWindow
                    x: toolbar.hostWindow.x + toolbar.hostWindow.width
                    y: 20
                    width: 700
                    height: 1000
                    visible: true
                    color: Style.windowBackgroundNoQWK

                    ListView {
                        id: infoList
                        anchors {
                            fill: parent
                            margins: 10
                        }
                        delegate: Text {
                            height: 12
                            color: modelData.endsWith(" E") ? "#80ff80" : Style.text
                            text: modelData
                        }
                    }
                }
            }
        }

        Loader {
            sourceComponent: toolbar.catalogModel.runningTasksDebug ? runningTasksDebugView : undefined
        }

        Slider {
            id: masonryZoomSlider
            Layout.preferredWidth: 100
            implicitHeight: 30 // To work around a warning
            Layout.preferredHeight: 30
            Layout.alignment: Qt.AlignVCenter
            from: toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Columns
                  || toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Details
                  ? 22
                  : toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Grid
                    ? 96
                    : toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Icons
                      ? 72 : 30
            value: toolbar.galleryLayout.view.density
            to: toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Columns
                || toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Details
                ? 72
                : toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Grid
                  ? 320
                  : toolbar.galleryLayout.view.presentationMode === GalleryViewportItem.Icons
                    ? 256 : 500
            stepSize: 1

            function updateTargetSize() {
                if (toolbar.galleryLayout.view.presentationMode
                        !== GalleryViewportItem.Masonry)
                    return
                if (toolbar.galleryLayout.listView) {
                    // 36 is hardcoded and comes from BrickDelegate's layout folderViewDelegate
                    toolbar.catalogModel.setFolderViewImageSize(0, (masonryZoomSlider.value - 36) * toolbar.devicePixelRatio)
                }
                else {
                    // this is taken from commented out actualGridSize item in BrickDelegate

                    let spacing = 2
                    let canvasWidth = (toolbar.galleryLayout.view.width - 1) - toolbar.galleryLayout.view.paddingLeft - toolbar.galleryLayout.view.paddingRight
                    let columns = Math.floor(canvasWidth / toolbar.galleryLayout.view.targetHeight)
                    let averageCellWidth = canvasWidth / columns
                    // console.log("ZZ COLUMNS", columns, canvasWidth, toolbar.galleryLayout.view.targetHeight)
                    if (columns >= toolbar.galleryLayout.view.count) {
                        // console.log("ZZ TOO FEW" , columns, toolbar.galleryLayout.view.count)
                        averageCellWidth = toolbar.galleryLayout.view.targetHeight
                    }

                    let targetWidth = averageCellWidth - toolbar.galleryLayout.view.spacing - spacing
                    let targetHeight = averageCellWidth - toolbar.galleryLayout.view.spacing * 2 - spacing - Math.round(targetWidth/20) - 17

                    let dimensions = targetWidth < 80 ? 1 :
                                     targetWidth < 150 ? 2 :
                                     targetWidth < 300 ? 3 : 4

                    targetWidth = (targetWidth - spacing * (dimensions + 1)) / dimensions
                    targetHeight = (targetHeight - spacing * (dimensions + 1)) / dimensions

                    if (targetWidth > 0 && targetHeight > 0) {
                        // console.log("qml dimens", Math.round(targetWidth * toolbar.devicePixelRatio), Math.round(targetHeight * toolbar.devicePixelRatio), dimensions)

                        toolbar.catalogModel.setFolderViewImageSize(Math.round(targetWidth * toolbar.devicePixelRatio), Math.round(targetHeight * toolbar.devicePixelRatio))
                    }
                }
            }

            onValueChanged: {
                toolbar.galleryLayout.view.density = masonryZoomSlider.value
                updateTargetSize()
            }
            property int lastValue: value
            onPressedChanged: {
                if (pressed) {
                    lastValue = value
                }
                else if (lastValue !== value) {
                    toolbar.galleryLayout.view.reReadAndDecodeThumbnails()
                }
            }
        }

        Separator {
        }

        ToolbarButton {
            id: collectionLayoutButton
            icon.source: "qrc:/ZoinGallery/resources/GridView.svg"
            icon.width: 16
            icon.height: 16
            ToolTip.text: "Collection layout"

            function choose(mode, columns) {
                toolbar.galleryLayout.choosePresentation(mode, columns)
            }

            onReleased: collectionLayoutMenu.open()

            Platform.Menu {
                id: collectionLayoutMenu

                Platform.MenuItemGroup {
                    id: collectionLayoutMenuGroup
                    exclusive: true
                }

                Platform.MenuItem {
                    text: "Masonry"
                    checkable: true
                    group: collectionLayoutMenuGroup
                    checked: toolbar.galleryLayout.view.presentationMode
                             === GalleryViewportItem.Masonry
                    onTriggered: collectionLayoutButton.choose(
                                     GalleryViewportItem.Masonry, 2)
                }
                Platform.MenuItem {
                    text: "Columns (2)"
                    checkable: true
                    group: collectionLayoutMenuGroup
                    checked: toolbar.galleryLayout.view.presentationMode
                             === GalleryViewportItem.Columns
                             && toolbar.galleryLayout.view.columnCount === 2
                    onTriggered: collectionLayoutButton.choose(
                                     GalleryViewportItem.Columns, 2)
                }
                Platform.MenuItem {
                    text: "Columns (3)"
                    checkable: true
                    group: collectionLayoutMenuGroup
                    checked: toolbar.galleryLayout.view.presentationMode
                             === GalleryViewportItem.Columns
                             && toolbar.galleryLayout.view.columnCount === 3
                    onTriggered: collectionLayoutButton.choose(
                                     GalleryViewportItem.Columns, 3)
                }
                Platform.MenuItem {
                    text: "Details"
                    checkable: true
                    group: collectionLayoutMenuGroup
                    checked: toolbar.galleryLayout.view.presentationMode
                             === GalleryViewportItem.Details
                    onTriggered: collectionLayoutButton.choose(
                                     GalleryViewportItem.Details, 1)
                }
                Platform.MenuItem {
                    text: "Uniform grid"
                    checkable: true
                    group: collectionLayoutMenuGroup
                    checked: toolbar.galleryLayout.view.presentationMode
                             === GalleryViewportItem.Grid
                    onTriggered: collectionLayoutButton.choose(
                                     GalleryViewportItem.Grid, 1)
                }
                Platform.MenuItem {
                    text: "Large icons"
                    checkable: true
                    group: collectionLayoutMenuGroup
                    checked: toolbar.galleryLayout.view.presentationMode
                             === GalleryViewportItem.Icons
                    onTriggered: collectionLayoutButton.choose(
                                     GalleryViewportItem.Icons, 1)
                }
            }
        }

        Separator {
        }

        TabBar {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter

            Shortcut {
                sequence: "F8"
                onActivated: {
                    toolbar.galleryLayout.listView = !toolbar.galleryLayout.listView
                }
            }

            TabButton {
                implicitWidth: 32
                implicitHeight: toolbar.titleBarItem.thumbnailsHeight

                icon.source: "qrc:/ZoinGallery/resources/ListView.svg"
                icon.width: 16
                icon.height: 16
                ToolTip.text: "Folder preview list\tF8"

                checked: toolbar.galleryLayout.listView

                onReleased: {
                    if (!toolbar.galleryLayout.listView) {
                        toolbar.galleryLayout.listView = true
                        masonryZoomSlider.updateTargetSize()
                        toolbar.galleryLayout.view.layoutReset()
                    }
                }
            }
            TabButton {
                implicitWidth: 32
                implicitHeight: toolbar.titleBarItem.thumbnailsHeight

                icon.source: "qrc:/ZoinGallery/resources/GridView.svg"
                icon.width: 16
                icon.height: 16
                ToolTip.text: "Folder preview grid\tF8"

                checked: !toolbar.galleryLayout.listView

                onReleased: {
                    if (toolbar.galleryLayout.listView) {
                        toolbar.galleryLayout.listView = false
                        masonryZoomSlider.updateTargetSize()
                        toolbar.galleryLayout.view.layoutReset()
                    }
                }
            }
        }

        ToolbarButton {
            id: sortButton
            Layout.leftMargin: 8
            icon.source: "qrc:/ZoinGallery/resources/Sort.svg"
            ToolTip.text: "Sort: " + toolbar.viewModel.sortModeLabel +
                          (toolbar.viewModel.selectedOnly ? "\nShowing selected only" : "")

            onReleased: {
                sortMenu.open()
            }

            Platform.Menu {
                id: sortMenu

                Platform.MenuItemGroup {
                    id: sortModeMenuGroup
                    exclusive: true
                }

                Platform.MenuItem {
                    text: "Show selected items only"
                    checkable: true
                    checked: toolbar.viewModel.selectedOnly
                    shortcut: "Ctrl+\\"
                    onTriggered: toolbar.viewModel.selectedOnly = !toolbar.viewModel.selectedOnly
                }

                Platform.MenuSeparator {}

                Platform.MenuItem {
                    text: "Name A-Z"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 0
                    onTriggered: toolbar.viewModel.sortMode = 0
                }

                Platform.MenuItem {
                    text: "Name Z-A"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 1
                    onTriggered: toolbar.viewModel.sortMode = 1
                }

                Platform.MenuItem {
                    text: "Modified oldest first"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 2
                    onTriggered: toolbar.viewModel.sortMode = 2
                }

                Platform.MenuItem {
                    text: "Modified newest first"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 3
                    onTriggered: toolbar.viewModel.sortMode = 3
                }

                Platform.MenuItem {
                    text: "Extension A-Z"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 4
                    onTriggered: toolbar.viewModel.sortMode = 4
                }

                Platform.MenuItem {
                    text: "Extension Z-A"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 5
                    onTriggered: toolbar.viewModel.sortMode = 5
                }

                Platform.MenuItem {
                    text: "Size smallest first"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 6
                    onTriggered: toolbar.viewModel.sortMode = 6
                }

                Platform.MenuItem {
                    text: "Size largest first"
                    group: sortModeMenuGroup
                    checkable: true
                    checked: toolbar.viewModel.sortMode === 7
                    onTriggered: toolbar.viewModel.sortMode = 7
                }
            }
        }

        ToolbarButton {
            Layout.leftMargin: 8
            icon.source: "qrc:/ZoinGallery/resources/RecursiveView.svg"
            ToolTip.text: "Recursive View\tF10"

            onReleased: {
                toolbar.navigationController.enterRecursiveView()
            }
        }

        ToolbarButton {
            icon.source: "qrc:/ZoinGallery/resources/SelectionCheck.svg"
            ToolTip.text: (toolbar.shell.selectedImagesPanelOpen ? "Hide" : "Show") +
                          " selected images panel (" +
                          toolbar.catalogModel.totalSelectedCount + ")"
            checked: toolbar.shell.selectedImagesPanelOpen

            onReleased: toolbar.shell.selectedImagesPanelOpen = !toolbar.shell.selectedImagesPanelOpen
        }

        ToolbarButton {
            icon.source: "qrc:/ZoinGallery/resources/SelectionHistory.svg"
            ToolTip.text: "Selection history\tCtrl+Shift+H"
            Layout.rightMargin: toolbar.quickWindowKitEnabled ? 0 : 7

            onReleased: {
                toolbar.historyWindow.visible = !toolbar.historyWindow.visible
                if (toolbar.historyWindow.visible) {
                    toolbar.historyWindow.refresh(true)
                }
            }
        }

        Separator {
            Layout.rightMargin: 7
            visible: toolbar.quickWindowKitEnabled && !toolbar.hostWindow.useMacNativeTitleBar
        }
    }

}
