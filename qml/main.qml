import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls

import ZoinGallery.MainWindow 1.0

import QWindowKit 1.0

MainWindow {
    id: topLevelWindow
    visible: true
    color: isQWK ? "transparent" : Style.windowBackgroundNoQWK
    title: "Zoin Gallery"

    property bool viewerDirty: false
    property bool thumbnailsDirty: false

    onClosing: (closeEvent) => {
        closeEvent.accepted = false
        topLevelWindow.hide()
        viewerController.prepareToClose()
    }

    WindowAgent {
        id: windowAgent
    }

    Component.onCompleted: {
        windowAgent.setup(topLevelWindow)

        windowAgent.setWindowAttribute("mica", true)
    }

    Connections {
        target: topLevelWindow

        function onMainWindowResized() {
            console.log("ZZ MAIN RESIZED")
            if (root.state === "thumbnails") {
                masonryLayout.view.reReadAndDecodeThumbnails()
                viewerDirty = true
            }
            else {
                console.log("onMainWindowResized", viewerMode.width * dpr, viewerMode.height * dpr)
                fileListModel.invalidateViewerImages()
                fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.width * dpr, viewerMode.height * dpr)
                thumbnailsDirty = true
            }
        }
    }

    Rectangle {
        id: root
        anchors.fill: parent
        color: Style.windowColor

        function toggleViewer() {
            if (root.state === "thumbnails") {
                if (viewerDirty) {
                    viewerDirty = false
                    console.log("viewer dirty")
                    fileListModel.invalidateViewerImages()
                }
                switchToViewer()
            }
            else {
                switchToThumbnails()
                if (thumbnailsDirty) {
                    thumbnailsDirty = false
                    masonryLayout.view.reReadAndDecodeThumbnails()
                }
            }
        }

        function switchToViewer() {
            viewerMode.forceActiveFocus()
            root.state = "viewer"

            if (!viewerMode.animation.running) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                viewerMode.image.x = mappedGeometry.x
                viewerMode.image.y = mappedGeometry.y
                viewerMode.image.width = mappedGeometry.width
                viewerMode.image.height = mappedGeometry.height
            }

            viewerMode.image.source = masonryLayout.view.currentItem.imageId
            viewerMode.canHaveTransparency = masonryLayout.view.currentItem.canHaveTransparency
            viewerMode.onCurrentIndexChanged()

            viewerMode.visible = true

            viewerMode.animation.x = 0
            viewerMode.animation.y = 0
            viewerMode.animation.width = viewerMode.imageContainer.width
            viewerMode.animation.height = viewerMode.imageContainer.height
            viewerMode.animation.restart()
        }

        function switchToThumbnails() {
            masonryLayout.focusProxy.forceActiveFocus()
            root.state = "thumbnails"

            if (masonryLayout.view.currentItem) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

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
            height: 46
            z: 1
            visible: isQWK

            property bool backgroundVisible: false

            Rectangle {
                anchors {
                    fill: titleBarButtonsLayout
                    topMargin: -radius
                    rightMargin: -radius
                }
                color: Style.opaqueMasonryViewBackgroundWithOpacity
                visible: titleBar.backgroundVisible && viewerMode.panelsVisible
                radius: 4
            }

            RowLayout {
                id: titleBarButtonsLayout
                anchors {
                    top: parent.top
                    right: parent.right
                    bottom: parent.bottom
                }
                spacing: 0

                component TitleButton : Button {
                    id: titleButton

                    implicitHeight: 32
                    implicitWidth: 46

                    leftPadding: 0
                    topPadding: 0
                    rightPadding: 0
                    bottomPadding: 0
                    leftInset: 0
                    topInset: 0
                    rightInset: 0
                    bottomInset: 0

                    property alias source: titleButton.icon.source

                    icon.width: 10
                    icon.height: 10
                    icon.color: Style.text

                    // property alias source: image.source
                    /*contentItem: Item {
                    Image {
                        id: image
                        anchors.centerIn: parent
                        mipmap: true
                        width: 10
                        height: 10
                    }
                }*/
                    background: Rectangle {
                        color: {
                            if (!titleButton.enabled) {
                                return "gray";
                            }
                            if (titleButton.pressed) {
                                return Style.darker;
                            }
                            if (titleButton.hovered) {
                                return Style.lighter;
                            }
                            return "transparent";
                        }
                    }
                }

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

                    source: topLevelWindow.visibility === Window.Maximized ? "qrc:/resources/WindowRestore.svg" : "qrc:/resources/WindowMaximize.svg"
                    onClicked: {
                        if (topLevelWindow.visibility === Window.Maximized) {
                            topLevelWindow.showNormal()
                        } else {
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
                    background: Rectangle {
                        color: {
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
                    }
                    onClicked: topLevelWindow.close()

                    Component.onCompleted: windowAgent.setSystemButton(WindowAgent.Close, closeButton)
                }
            }

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
                Layout.fillWidth: true
                Layout.preferredHeight: titleBar.height

                RowLayout {
                    id: toolbarLayout
                    anchors.fill: parent
                    anchors.rightMargin: isQWK ? titleBarButtonsLayout.width : 0
                    spacing: 0

                    component Separator : Rectangle {
                        Layout.leftMargin: 5
                        Layout.rightMargin: 5
                        implicitWidth: 1
                        implicitHeight: 46 - 15
                        color: Style.lighter2
                    }

                    component ToolbarButton : Button {
                        id: toolbarButton
                        Layout.alignment: Qt.AlignVCenter

                        implicitWidth: 36
                        implicitHeight: 46

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
                        onClicked: windowAgent.showSystemMenu(mapToGlobal(0, height))
                        Component.onCompleted: windowAgent.setSystemButton(WindowAgent.WindowIcon, appIcon)
                    }

                    Text {
                        property bool isPartOfTitleBar: true
                        visible: isQWK

                        Layout.rightMargin: 15

                        text: "ZoinGallery"
                        color: Style.text
                        opacity: topLevelWindow.active ? 1 : 0.5
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
                        Layout.leftMargin: 15
                        Layout.rightMargin: 15

                        onEditModeChanged: {
                            if (!editMode) {
                                masonryLayout.focusProxy.forceActiveFocus()
                            }
                        }
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32

                        text: viewerController.currentPath
                    }


                    Slider {
                        id: masonryZoomSlider
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 30
                        Layout.alignment: Qt.AlignVCenter
                        from: 40
                        value: masonryLayout.view.targetHeight
                        to: 500
                        stepSize: 1

                        onValueChanged: masonryLayout.view.targetHeight = masonryZoomSlider.value
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
                        Layout.leftMargin: 13
                        Layout.rightMargin: 15
                    }

                    TabBar {
                        spacing: 0
                        Layout.alignment: Qt.AlignVCenter
                        Layout.rightMargin: 15

                        Shortcut {
                            sequence: "F8"
                            onActivated: {
                                masonryLayout.view.listView = !masonryLayout.view.listView
                            }
                        }

                        TabButton {
                            implicitWidth: 36
                            implicitHeight: 46

                            icon.source: "qrc:/resources/ListView.svg"
                            icon.width: 16
                            icon.height: 16

                            checked: masonryLayout.view.listView

                            onReleased: {
                                if (!masonryLayout.view.listView) {
                                    masonryLayout.view.listView = true
                                    masonryLayout.view.layoutReset()
                                }
                            }
                        }
                        TabButton {
                            implicitWidth: 36
                            implicitHeight: 46

                            icon.source: "qrc:/resources/GridView.svg"
                            icon.width: 16
                            icon.height: 16

                            checked: !masonryLayout.view.listView

                            onReleased: {
                                if (masonryLayout.view.listView) {
                                    masonryLayout.view.listView = false
                                    masonryLayout.view.layoutReset()
                                }
                            }
                        }
                    }
                }
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
            }
        ]
    }
}
