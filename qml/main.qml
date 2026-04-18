import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

import ZoinGallery.MainWindow 1.0

import QWindowKit 1.0

MainWindow {
    id: topLevelWindow
    // visible: true
    color: isQWK ? (isQWKLegacy ? Style.windowBackgroundQWKLegacy : "transparent") : Style.windowBackgroundNoQWK
    property bool isQWKLegacy: false
    title: "Zoin Gallery"

    property bool viewerDirty: false
    property bool thumbnailsDirty: false

    onClosing: (closeEvent) => {
        closeEvent.accepted = false
        topLevelWindow.hide()
        viewerController.prepareToClose()
    }

    // CacheViewer {
    // }

    WindowAgent {
        id: windowAgent
    }

    Component.onCompleted: {
        windowAgent.setup(topLevelWindow)

        isQWKLegacy = windowAgent.setWindowAttribute("mica-alt", true) !== true
    }

    Connections {
        target: topLevelWindow

        function onMainWindowResized(isWidthChanged) {
            Qt.callLater(() => {
            console.log("main window resized", topLevelWindow.width, topLevelWindow.height)
            if (root.state === "thumbnails") {
                if (isWidthChanged) {
                    masonryLayout.view.reReadAndDecodeThumbnails()
                }
                viewerDirty = true
            }
            else {
                if (viewerMode.zoomFitView && !viewerMode.sphericViewerMode) {
                    // console.log("onMainWindowResized")
                    viewerMode.imageContainer.zoomToFit(true)
                    fileListModel.cancelAllDecodeViewerRunners()
                    fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.width * dpr, viewerMode.height * dpr)
                    viewerDirty = false
                }
                else {
                    viewerDirty = true
                }
                thumbnailsDirty = true
            }
            })
        }

        function onWindowIsReady() {
            masonryZoomSlider.updateTargetSize()
            viewerController.initialCd()
        }
    }

    Connections {
        target: viewerMode
        function onZoomFitViewChanged() {
            if (viewerMode.zoomFitView && viewerDirty) {
                fileListModel.cancelAllDecodeViewerRunners()
                fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.width * dpr, viewerMode.height * dpr)
                viewerDirty = false
            }
            else {
                fileListModel.cancelAllDecodeViewerRunners()
                fileListModel.requestViewer(masonryLayout.view.currentIndex)
            }
        }
    }

    Connections {
        target: viewerController
        function onCurrentPathChanged() {
            masonryZoomSlider.updateTargetSize()
        }
    }

    Rectangle {
        id: root
        anchors.fill: parent
        color: Style.windowColor

        property bool viewerShowAnimationRunning: viewerMode.animation.running

        function toggleViewer() {
            if (root.state === "thumbnails") {
                if (viewerDirty) {
                    viewerDirty = false
                    console.log("viewer dirty")
                }
                switchToViewer()
            }
            else {
                switchToThumbnails()
                if (thumbnailsDirty) {
                    thumbnailsDirty = false
                    masonryLayout.view.reReadAndDecodeThumbnails()
                }
                fileListModel.cancelAllDecodeViewerRunners()
            }
        }

        function switchToViewer() {
            viewerMode.forceActiveFocus()
            root.state = "viewer"

            if (!viewerMode.animation.running) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                viewerMode.imageContainer.x = mappedGeometry.x
                viewerMode.imageContainer.y = mappedGeometry.y
                viewerMode.imageContainer.width = mappedGeometry.width
                viewerMode.imageContainer.height = mappedGeometry.height
            }

            viewerMode.setImage(masonryLayout.view.currentItem.model.imageIdUrl,
                                masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex), masonryLayout.view.currentIndex, 0)
            let exif = masonryLayout.view.indexExif(masonryLayout.view.currentIndex)
            viewerMode.show(exif["Panorama"])

            viewerMode.animation.x = 0
            viewerMode.animation.y = 0
            viewerMode.animation.width = viewerMode.width
            viewerMode.animation.height = viewerMode.height
            viewerMode.animation.restart()
        }

        function switchToThumbnails() {
            masonryLayout.focusProxy.forceActiveFocus()
            root.state = "thumbnails"

            if (masonryLayout.view.currentItem) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                if (!viewerMode.zoomFitView) {
                    viewerMode.imageContainer.zoomToFit(true) // TODO: Smooth animation
                }

                viewerMode.animation.x = mappedGeometry.x
                viewerMode.animation.y = mappedGeometry.y
                viewerMode.animation.width = mappedGeometry.width
                viewerMode.animation.height = mappedGeometry.height
                viewerMode.animation.restart()
            }

            topLevelWindow.title = "ZoinGallery"
        }

        Item {
            id: titleBar
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            property int viewerHeight: 32
            property int thumbnailsHeight: 48

            height: root.state === "thumbnails" ? thumbnailsHeight : viewerHeight
            Behavior on height {
                NumberAnimation {
                    duration: viewerMode.animationDuration
                    easing.type: viewerMode.easingType
                }
            }

            z: 1
            visible: isQWK

            RowLayout {
                id: titleBarButtonsLayout
                anchors {
                    top: parent.top
                    right: parent.right
                    bottom: parent.bottom
                }
                spacing: 0

                TitleButton {
                    id: minButton

                    Layout.alignment: Qt.AlignTop

                    source: "qrc:/resources/WindowMinimize.svg"
                    onClicked: topLevelWindow.showMinimized()
                    Component.onCompleted: windowAgent.setSystemButton(WindowAgent.Minimize, minButton)
                }

                TitleButton {
                    id: maxButton

                    Layout.alignment: Qt.AlignTop

                    source: topLevelWindow.visibility === Window.Maximized ? "qrc:/resources/WindowRestore.svg" :
                            topLevelWindow.visibility === Window.FullScreen ? "qrc:/resources/WindowFullscreen.svg" :"qrc:/resources/WindowMaximize.svg"
                    onClicked: {
                        if (topLevelWindow.visibility === Window.FullScreen) {
                            topLevelWindow.toggleFullscreen()
                        }
                        else if (topLevelWindow.visibility === Window.Maximized) {
                            topLevelWindow.showNormal()
                        }
                        else {
                            topLevelWindow.showMaximized()
                        }
                    }
                    Component.onCompleted: windowAgent.setSystemButton(WindowAgent.Maximize, maxButton)
                }

                TitleButton {
                    id: closeButton

                    Layout.alignment: Qt.AlignTop

                    source: "qrc:/resources/WindowClose.svg"
                    icon.color: closeButton.hovered ? Style.closeButtonHoveredIcon : Style.text
                    backgroundColor: {
                        if (!closeButton.enabled) {
                            return "gray";
                        }
                        if (closeButton.pressed) {
                            return Style.closeButtonPressed;
                        }
                        if (closeButton.hovered) {
                            return Style.closeButtonHovered;
                        }
                        return "transparent";
                    }
                    onClicked: topLevelWindow.close()

                    Component.onCompleted: windowAgent.setSystemButton(WindowAgent.Close, closeButton)
                }
            }

        }

        Rectangle {
            id: thumbnailsViewBackground
            anchors {
                fill: thumbnailsView
                // margins: 5
                topMargin: titleBar.thumbnailsHeight
                leftMargin: Style.isDarkTheme ? 0 : -1
                rightMargin: Style.isDarkTheme ? 0 : -1
                bottomMargin: Style.isDarkTheme ? 0 : -1
            }
            radius: 7
            border.width: 1
            border.color: Style.masonryViewBackgroundBorder
            color: Style.masonryViewBackground
        }

        Rectangle {
            id: viewerBackground
            anchors.fill: thumbnailsView
            color: thumbnailsViewBackground.color
            opacity: 0
        }


        ColumnLayout {
            id: thumbnailsView
            anchors.fill: parent

            spacing: 0

            Component.onCompleted: {
                windowAgent.setTitleBar(titleBar)

                for (var i = 0; i < titleBarButtonsLayout.children.length; i++) {
                    if (typeof titleBarButtonsLayout.children[i].isPartOfTitleBar === "undefined") {
                        windowAgent.setHitTestVisible(titleBarButtonsLayout.children[i])
                    }
                }

                for (var i = 0; i < toolbarLayout.children.length; i++) {
                    if (typeof toolbarLayout.children[i].isPartOfTitleBar === "undefined") {
                        windowAgent.setHitTestVisible(toolbarLayout.children[i])
                    }
                }
            }

            Item {
                id: toolbar
                Layout.fillWidth: true
                Layout.preferredHeight: titleBar.thumbnailsHeight
                z: 1

                MultiEffect {
                    id: titleBarBlurBehind
                    source: ShaderEffectSource {
                        sourceItem: masonryLayout
                        width: titleBarBlurBehind.width
                        height: titleBarBlurBehind.height
                        sourceRect: Qt.rect(0, -height, width, height)
                    }

                    anchors.fill: parent
                    opacity: 0.3
                    contrast: Style.isDarkTheme ? -0.5 : 0
                    brightness: Style.isDarkTheme ? 0 : 0.3
                    saturation: topLevelWindow.active ? 0 : -1
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
                    anchors.rightMargin: isQWK ? titleBarButtonsLayout.width : 0
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
                        implicitHeight: titleBar.thumbnailsHeight

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
                        visible: isQWK

                        icon.width: 18
                        icon.height: 18

                        colorfulIcon: true
                        icon.source: "qrc:/resources/Logo.svg"
                        onClicked: {
                            // windowAgent.showSystemMenu(mapToGlobal(0, height))
                            // fileListModel.startScanner()
                            settingsDialog.open()
                        }
                        // Component.onCompleted: windowAgent.setSystemButton(WindowAgent.WindowIcon, appIcon)
                    }

                    Text {
                        property bool isPartOfTitleBar: true
                        visible: isQWK

                        Layout.rightMargin: 15

                        text: "ZoinGallery"
                        color: Style.text
                        renderType: Text.NativeRendering

                        opacity: topLevelWindow.active ? 1 : 0.5
                        Behavior on opacity {
                            NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/Back.svg"
                        ToolTip.text: "Go Back\tAlt+←"
                        inactive: !viewerController.canBack

                        implicitWidth: 46
                        centerOffset: 5
                        leftPadding: 18

                        onReleased: {
                            if (inactive) {
                                return
                            }

                            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                            viewerController.back()
                            masonryLayout.view.loadSavedState()
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
                                    viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                                    viewerController.jumpBack(index)
                                    masonryLayout.view.loadSavedState()
                                }
                            }

                            Repeater {
                                model: viewerController.backMenu

                                MenuItem {
                                    text: modelData
                                    onTriggered: {
                                        delayedBackRightClick.index = index
                                        delayedBackRightClick.start()
                                        masonryLayout.focusProxy.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/Forward.svg"
                        ToolTip.text: "Go Forward\tAlt+→"
                        inactive: !viewerController.canForward

                        onReleased: {
                            if (inactive) {
                                return
                            }

                            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                            viewerController.forward()
                            masonryLayout.view.loadSavedState()
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
                                    viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                                    viewerController.jumpForward(index)
                                    masonryLayout.view.loadSavedState()
                                }
                            }

                            Repeater {
                                model: viewerController.forwardMenu

                                MenuItem {
                                    text: modelData
                                    onTriggered: {
                                        delayedForwardRightClick.index = index
                                        delayedForwardRightClick.start()
                                        masonryLayout.focusProxy.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/Up.svg"
                        ToolTip.text: "Go Up\tBackspace"
                        inactive: !viewerController.canUp

                        onReleased: {
                            masonryLayout.disableAnimation = true
                            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                            masonryLayout.setCurrentIndex(viewerController.up())
                            masonryLayout.disableAnimation = false
                            masonryLayout.view.loadSavedState()
                        }
                    }

                    PathControl {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        clip: true

                        onEditModeChanged: {
                            if (!editMode) {
                                masonryLayout.focusProxy.forceActiveFocus()
                            }
                        }
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32

                        text: viewerController.currentPath
                    }

                    Shortcut {
                        sequence: "F12"
                        onActivated: {
                            fileListModel.runningTasksDebug = !fileListModel.runningTasksDebug
                        }
                    }

                    Component {
                        id: runningTasksDebugView
                        Text {
                            id: runningTasks

                            Connections {
                                target: fileListModel
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
                                x: topLevelWindow.x + topLevelWindow.width
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
                        sourceComponent: fileListModel.runningTasksDebug ? runningTasksDebugView : undefined
                    }

                    Slider {
                        id: masonryZoomSlider
                        Layout.preferredWidth: 100
                        implicitHeight: 30 // To work around a warning
                        Layout.preferredHeight: 30
                        Layout.alignment: Qt.AlignVCenter
                        from: 40
                        value: masonryLayout.view.targetHeight
                        to: 500
                        stepSize: 1

                        function updateTargetSize() {
                            if (masonryLayout.view.listView) {
                                // 36 is hardcoded and comes from BrickDelegate's layout folderViewDelegate
                                fileListModel.setFolderViewImageSize(0, (masonryZoomSlider.value - 36) * dpr)
                            }
                            else {
                                // this is taken from commented out actualGridSize item in BrickDelegate

                                let spacing = 2
                                let canvasWidth = (masonryLayout.view.width - 1) - masonryLayout.view.paddingLeft - masonryLayout.view.paddingRight
                                let columns = Math.floor(canvasWidth / masonryLayout.view.targetHeight)
                                let averageCellWidth = canvasWidth / columns
                                // console.log("ZZ COLUMNS", columns, canvasWidth, masonryLayout.view.targetHeight)
                                if (columns >= masonryLayout.view.count) {
                                    // console.log("ZZ TOO FEW" , columns, masonryLayout.view.count)
                                    averageCellWidth = masonryLayout.view.targetHeight
                                }

                                let targetWidth = averageCellWidth - masonryLayout.view.spacing - spacing
                                let targetHeight = averageCellWidth - masonryLayout.view.spacing * 2 - spacing - Math.round(targetWidth/20) - 17

                                let dimensions = targetWidth < 80 ? 1 :
                                                 targetWidth < 150 ? 2 :
                                                 targetWidth < 300 ? 3 : 4

                                targetWidth = (targetWidth - spacing * (dimensions + 1)) / dimensions
                                targetHeight = (targetHeight - spacing * (dimensions + 1)) / dimensions

                                if (targetWidth > 0 && targetHeight > 0) {
                                    // console.log("qml dimens", Math.round(targetWidth * dpr), Math.round(targetHeight * dpr), dimensions)

                                    fileListModel.setFolderViewImageSize(Math.round(targetWidth * dpr), Math.round(targetHeight * dpr))
                                }
                            }
                        }

                        onValueChanged: {
                            masonryLayout.view.targetHeight = masonryZoomSlider.value
                            updateTargetSize()
                        }
                        property int lastValue: value
                        onPressedChanged: {
                            if (pressed) {
                                lastValue = value
                            }
                            else if (lastValue !== value) {
                                masonryLayout.view.reReadAndDecodeThumbnails()
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
                                masonryLayout.view.listView = !masonryLayout.view.listView
                            }
                        }

                        TabButton {
                            implicitWidth: 32
                            implicitHeight: titleBar.thumbnailsHeight

                            icon.source: "qrc:/resources/ListView.svg"
                            icon.width: 16
                            icon.height: 16
                            ToolTip.text: "List View\tF8"

                            checked: masonryLayout.view.listView

                            onReleased: {
                                if (!masonryLayout.view.listView) {
                                    masonryLayout.view.listView = true
                                    masonryZoomSlider.updateTargetSize()
                                    masonryLayout.view.layoutReset()
                                }
                            }
                        }
                        TabButton {
                            implicitWidth: 32
                            implicitHeight: titleBar.thumbnailsHeight

                            icon.source: "qrc:/resources/GridView.svg"
                            icon.width: 16
                            icon.height: 16
                            ToolTip.text: "Grid View\tF8"

                            checked: !masonryLayout.view.listView

                            onReleased: {
                                if (masonryLayout.view.listView) {
                                    masonryLayout.view.listView = false
                                    masonryZoomSlider.updateTargetSize()
                                    masonryLayout.view.layoutReset()
                                }
                            }
                        }
                    }

                    ToolbarButton {
                        Layout.leftMargin: 8
                        Layout.rightMargin: isQWK ? 0 : 7
                        icon.source: "qrc:/resources/RecursiveView.svg"
                        ToolTip.text: "Recursive View\tF10"

                        onReleased: {
                            viewerController.enterRecursiveView()
                        }
                    }

                    Separator {
                        Layout.rightMargin: 7
                        visible: isQWK
                    }
                }

                // Rectangle {
                //     anchors {
                //         left: parent.left
                //         right: parent.right
                //         bottom: parent.bottom
                //         bottomMargin: -4
                //     }
                //     height: 4
                //     gradient: Gradient {
                //         GradientStop { position: 0.0; color: Style.isDarkTheme ? "#fff" : "#000" }
                //         GradientStop { position: 0.5; color: Style.isDarkTheme ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(0, 0, 0, 0.2) }
                //         GradientStop { position: 1.0; color: "transparent" }
                //     }

                //     opacity: masonryLayout.scrolled ? (Style.isDarkTheme ? 0.08 : 0.10) : 0
                //     Behavior on opacity {
                //         NumberAnimation {
                //             duration: 300
                //             easing.type: Easing.InOutQuad
                //         }
                //     }
                // }
            }

            MasonryMode {
                id: masonryLayout
                Layout.fillWidth: true
                Layout.fillHeight: true

                onToggleViewer: root.toggleViewer()
            }
        }

        ViewerMode {
            id: viewerMode
            anchors.fill: parent
        }

        SettingsDialog {
            id: settingsDialog
        }

        state: "thumbnails"

        transitions: [
            Transition {
                from: "thumbnails"
                to: "viewer"
                SequentialAnimation {
                    PropertyAnimation { properties: "opacity"; duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
                    PropertyAction {
                        target: toolbarLayout
                        property: "visible"
                        value: false
                    }
                }
            },
            Transition {
                from: "viewer"
                to: "thumbnails"
                SequentialAnimation {
                    PropertyAction {
                        target: toolbarLayout
                        property: "visible"
                        value: true
                    }
                    PropertyAnimation { properties: "opacity"; duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
                }
            }
        ]


        states: [
            State {
                name: "thumbnails"
            },
            State {
                name: "viewer"
                PropertyChanges {
                    target: thumbnailsView
                    opacity: 0
                }
                PropertyChanges {
                    target: thumbnailsViewBackground
                    opacity: 0
                }
                PropertyChanges {
                    target: viewerBackground
                    opacity: 1
                }
                PropertyChanges {
                    target: titleBar
                    opacity: 0
                }
            }
        ]
    }
}
