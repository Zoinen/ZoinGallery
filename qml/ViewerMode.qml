import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: viewerMode

    visible: false

    property int animationDuration: 150
    property int easingType: Easing.OutSine

    property alias animation: viewerAnimation
    property alias image: viewerImage
    property alias imageContainer: imageContainerItem

    function onCurrentIndexChanged() {
        fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.imageContainer.width * dpr, viewerMode.imageContainer.height * dpr)
        if (masonryLayout.view.currentItem) {
            topLevelWindow.title = masonryLayout.view.currentItem.text + " [" +
                    (masonryLayout.view.currentImageIndex + 1) + "/" + masonryLayout.view.imageCount + "] - ZoinGallery"
        }
    }

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
                     event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier) ||
                     event.key === Qt.Key_PageUp && (event.modifiers & Qt.ControlModifier)) {
                root.toggleViewer()
            }

            if (nextIndex !== -1 && nextIndex !== currentIndex) {
                onCurrentIndexChanged()
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

    Item {
        id: imageContainerItem

        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            right: sliderContainer.left
        }

        Item {
            clip: true
            x: Math.floor(viewerImage.x + viewerImage.width / 2 - width / 2) + (viewerAnimation.running ? 0.5 : 0)
            y: Math.floor(viewerImage.y + viewerImage.height / 2 - height / 2) + (viewerAnimation.running ? 0.5 : 0)

            width: Math.floor(viewerImage.useHeight ? viewerImage.width : (viewerImage.height * viewerImage.aspect)) - (viewerAnimation.running ? 2.5 : 0)
            height: Math.floor(viewerImage.useHeight ? (viewerImage.width / viewerImage.aspect) : viewerImage.height) - (viewerAnimation.running ? 2.5 : 0)
            visible: masonryLayout.view.showTransparentGrid

            Image {
                x: viewerAnimation.running ? -0.5 : 0
                y: viewerAnimation.running ? -0.5 : 0
                width: parent.width + (viewerAnimation.running ? 0.5 : 0)
                height: parent.height + (viewerAnimation.running ? 0.5 : 0)
                source: "image://resources/transparent_grid|" + dpr
                sourceSize.width: 16 * dpr
                sourceSize.height: 16 * dpr
                fillMode: Image.Tile
                verticalAlignment: Image.AlignTop
                horizontalAlignment: Image.AlignLeft
            }
        }

        Image {
            id: viewerImage
            fillMode: Image.PreserveAspectFit
            cache: false

            property int actualWidth: Math.round(viewerImage.width * dpr)
            property int actualHeight: Math.round(viewerImage.height * dpr)
            property real aspect: viewerImage.sourceSize.width / viewerImage.sourceSize.height
            property bool useHeight: (viewerImage.sourceSize.height * actualWidth / actualHeight) <= viewerImage.sourceSize.width

            // Dirty hack to workaround blurry output. Remove someday
            property bool needScaling: {
                return useHeight ? (Math.abs(viewerImage.sourceSize.width - actualWidth) > 1) :
                                   (Math.abs(viewerImage.sourceSize.height - actualHeight) > 1)
            }
            smooth: needScaling
        }
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
                    onCurrentIndexChanged()
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
                viewerImage.width = Qt.binding(function() {return imageContainer.width})
                viewerImage.height = Qt.binding(function() {return imageContainer.height})
            }
        }
    }

    Item {
        id: sliderContainer
        anchors {
            top: parent.top
            right: parent.right
            bottom: parent.bottom
        }
        width: 16

        opacity: root.state === "viewer"
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
        }

        Slider {
            id: currentImageSlider
            x: 16
            y: 0
            width: parent.height
            height: 16
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0

            rectangular: true

            from: 0
            to: masonryLayout.view.imageCount - 1
            value: masonryLayout.view.currentImageIndex
            stepSize: 1
            snapMode: Slider.SnapAlways

            rotation: 90
            transformOrigin: Item.TopLeft

            onValueChanged: {
                if (pressed) {
                    masonryLayout.view.currentImageIndex = Math.round(value)
                    masonryLayout.setCurrentIndex(masonryLayout.view.currentIndex)
                    onCurrentIndexChanged()

                }
            }

            Connections {
                target: masonryLayout.view
                function onCurrentImageIndexChanged() {
                    currentImageSlider.value = masonryLayout.view.currentImageIndex
                }
            }
        }
    }
}
