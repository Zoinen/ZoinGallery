import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls

Window {
    id: topLevelWindow
    visible: true
    color: "#333333"
    title: "Zoin Gallery"

    property int lastVisibility: Window.Windowed

    function toggleFullscreen() {
        if (topLevelWindow.visibility === Window.FullScreen) {
            if (topLevelWindow.lastVisibility !== undefined) {
                topLevelWindow.visibility = lastVisibility
            }
            else {
                topLevelWindow.visibility = Window.Maximized
            }
        }
        else {
            topLevelWindow.lastVisibility = topLevelWindow.visibility
            topLevelWindow.visibility = Window.FullScreen
        }
    }


    property real dpr: topLevelWindow.screen.devicePixelRatio

    onClosing: (closeEvent) => {
                   viewerController.prepareToClose()
               }

    Component.onCompleted: {
        viewerController.mainWindow = topLevelWindow
    }

    property bool viewerDirty: false
    property bool thumbnailsDirty: false

    Connections {
        target: viewerController

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

    Item {
        id: root
        anchors.fill: parent

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

        ColumnLayout {
            id: thumbnailsView
            anchors.fill: parent

            spacing: 0

            RowLayout {
                id: toolbarLayout
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                spacing: 5

                component Separator : Rectangle {
                    implicitWidth: 1
                    implicitHeight: 46 - 10
                    color: "#474747"
                }

                Button {
                    Layout.leftMargin: 5
                    Layout.alignment: Qt.AlignVCenter

                    implicitWidth: 46
                    implicitHeight: 46

                    icon.source: "qrc:/resources/Up.svg"

                    onReleased: {
                        masonryLayout.disableAnimation = true
                        masonryLayout.setCurrentIndex(viewerController.up())
                        masonryLayout.disableAnimation = false
                    }
                }

                Separator {}

                PathControl {
                    onEditModeChanged: {
                        if (!editMode) {
                            masonryLayout.focusProxy.forceActiveFocus()
                        }
                    }
                    Layout.rightMargin: 10
                    Layout.fillWidth: true
                    Layout.preferredHeight: 46

                    text: viewerController.currentPath
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 50
                    horizontalAlignment: Text.AlignRight

                    text: Math.round(masonryZoomSlider.value)
                    color: Style.text
                }

                Slider {
                    id: masonryZoomSlider
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 30
                    Layout.rightMargin: 10
                    Layout.alignment: Qt.AlignVCenter
                    from: 30
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

                Separator {}

                TabBar {
                    spacing: 0
                    Layout.alignment: Qt.AlignVCenter

                    TabButton {
                        implicitWidth: 46
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
                        implicitWidth: 46
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

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#474747"
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

        states: [
            State {
                name: "thumbnails"
            },
            State {
                name: "viewer"
            }
        ]
    }

    Item {
        transitions: [
            Transition {
                to: "inactive"
                ColorAnimation {
                    duration: 150
                    easing.type: Easing.InOutQuad
                }
            }
        ]

        states: [
            State {
                name: "inactive"
                when: !topLevelWindow.active
                PropertyChanges {
                    target: topLevelWindow
                    color: "#202020"
                }
            }
        ]
    }
}
