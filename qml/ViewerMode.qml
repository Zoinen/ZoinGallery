import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: viewerMode

    property int animationDuration: 150
    property int easingType: Easing.OutSine

    property alias animation: viewerAnimation
    property alias image: viewerImage

    Keys.onPressed:
        (event) => {
            let nextIndex = -1
            let currentIndex = masonryLayout.view.currentIndex
            if ((event.key === Qt.Key_Left || event.key === Qt.Key_PageUp || event.key === Qt.Key_Backspace ||
                 event.key === Qt.Key_Up) && !(event.modifiers & Qt.AltModifier)) {
                nextIndex = masonryLayout.moveInImageList(false, false)
            }
            else if ((event.key === Qt.Key_Right || event.key === Qt.Key_PageDown || event.key === Qt.Key_Space ||
                      event.key === Qt.Key_Down) && !(event.modifiers & Qt.AltModifier)) {
                nextIndex = masonryLayout.moveInImageList(true, false)
            }
            else if (event.key === Qt.Key_Home) {
                nextIndex = masonryLayout.moveInImageList(false, true)
            }
            else if (event.key === Qt.Key_End) {
                nextIndex = masonryLayout.moveInImageList(true, true)
            }
            else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F || event.key === Qt.Key_Clear /*Num_5*/ ||
                     (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier)) {
                topLevelWindow.toggleFullscreen()
            }
            else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Escape ||
                     event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier)) {
                root.toggleViewer()
            }

            if (nextIndex !== -1 && nextIndex !== currentIndex) {
                fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.width * dpr, viewerMode.height * dpr)
                if (masonryLayout.view.currentItem) {
                    topLevelWindow.title = masonryLayout.view.currentItem.text + " - ZoinGallery"
                }
            }
    }

    Connections {
        target: masonryLayout.view
        function onCurrentIndexChanged() {
            if (root.state === "viewer") {
                let imageId = masonryLayout.view.indexImage(masonryLayout.view.currentIndex)
                if (imageId) {
                    viewerImage.source = imageId
                }
            }
        }
    }

    Connections {
        target: fileListModel
        function onViewerImageIdChanged(newImageId) {
            viewerImage.source = newImageId
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: root.state === "viewer"
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
        }
    }

    Image {
        id: viewerImage
        fillMode: Image.PreserveAspectFit
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.state === "viewer"

        onDoubleClicked:
            (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    root.toggleViewer()
                }
            }
        onPressed:
            (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    root.toggleViewer()
                }
                else if (mouse.button === Qt.MiddleButton) {
                    topLevelWindow.toggleFullscreen()
                }
            }

        onWheel:
            (wheel) => {
                let nextIndex = -1
                let currentIndex = masonryLayout.view.currentIndex
                if (wheel.angleDelta.y < 0) {
                    nextIndex = masonryLayout.moveInImageList(true, false)
                }
                else {
                    nextIndex = masonryLayout.moveInImageList(false, false)
                }

                if (nextIndex !== currentIndex) {
                    fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.width * dpr, viewerMode.height * dpr)
                    if (masonryLayout.view.currentItem) {
                        topLevelWindow.title = masonryLayout.view.currentItem.text + " - ZoinGallery"
                    }
                }
            }

        acceptedButtons: Qt.AllButtons
    }

    ParallelAnimation {
        id: viewerAnimation

        property alias x: viewerMaximizeAnimationX.to
        property alias y: viewerMaximizeAnimationY.to
        property alias width: viewerMaximizeAnimationWidth.to
        property alias height: viewerMaximizeAnimationHeight.to

        NumberAnimation {
            id: viewerMaximizeAnimationX
            target: viewerImage
            property: "x"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationY
            target: viewerImage
            property: "y"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationWidth
            target: viewerImage
            property: "width"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationHeight
            target: viewerImage
            property: "height"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        onFinished: {
            if (root.state === "thumbnails") {
                viewerMode.visible = false
            }
            else {
                viewerImage.width = Qt.binding(function() {return viewerMode.width})
                viewerImage.height = Qt.binding(function() {return viewerMode.height})
            }
        }
    }
}
