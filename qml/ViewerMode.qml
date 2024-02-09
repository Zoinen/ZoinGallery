import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: viewerMode

    visible: false

    property int animationDuration: 150
    property int easingType: Easing.OutSine

    property bool panelsVisible: true

    property bool canHaveTransparency
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

    // Rectangle {
    //     anchors.fill: parent
    //     color: Style.viewerBackground
    //     opacity: root.state === "viewer"
    //     Behavior on opacity {
    //         NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
    //     }
    // }

    Item {
        id: imageContainerItem

        anchors.fill: parent

        Item {
            id: viewerBoundaries
            clip: true
            x: Math.floor(viewerImage.x + viewerImage.width / 2 - width / 2) + (viewerAnimation.running ? 0.5 : 0)
            y: Math.floor(viewerImage.y + viewerImage.height / 2 - height / 2) + (viewerAnimation.running ? 0.5 : 0)

            width: Math.floor(viewerImage.useHeight ? viewerImage.width : (viewerImage.height * viewerImage.aspect)) - (viewerAnimation.running ? 2.5 : 0)
            height: Math.floor(viewerImage.useHeight ? (viewerImage.width / viewerImage.aspect) : viewerImage.height) - (viewerAnimation.running ? 2.5 : 0)
            visible: masonryLayout.view.showTransparentGrid && canHaveTransparency

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
        id: viewerMouse
        anchors.fill: parent
        anchors.topMargin: isQWK ? titleBar.height : 0
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
                else if (mouse.button === Qt.LeftButton) {
                    panelsVisible = !panelsVisible
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

    MouseArea {
        id: rightPanel
        anchors {
            top: parent.top
            topMargin: isQWK ? titleBar.height : 0
            right: parent.right
            bottom: parent.bottom
        }
        width: 120

        property bool viewerOverlapsFilmstrip: x < viewerBoundaries.x + viewerBoundaries.width
        onViewerOverlapsFilmstripChanged: {
            titleBar.backgroundVisible = viewerOverlapsFilmstrip
        }

        opacity: root.state === "viewer" && (panelsVisible || rightPanel.containsMouse)
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
        }

        hoverEnabled: true

        Rectangle {
            anchors.fill: parent
            color: rightPanel.viewerOverlapsFilmstrip ? Style.opaqueMasonryViewBackgroundWithOpacity : Style.viewerPanel
        }

        ListView {
            id: filmstrip

            anchors {
                top: parent.top
                right: parent.right
                rightMargin: 25
                bottom: parent.bottom
            }
            width: 86
            spacing: 13
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            interactive: false

            model: imageModel

            Connections {
                target: masonryLayout.view

                function onCurrentIndexChanged() {
                    filmstrip.positionViewAtIndex(imageModel.mapFromSourceRow(masonryLayout.view.currentIndex), ListView.Center)
                }
            }


            delegate: Item {
                width: 86
                height: 57

                property bool isCurrent: imageModel.mapFromSourceRow(masonryLayout.view.currentIndex) === index

                Image {
                    id: thumbnailImage
                    source: "image://thumbnails/" + imageIdRole
                    sourceSize.width: parent.width
                    sourceSize.height: parent.height
                    fillMode: Image.PreserveAspectFit
                    width: parent.width
                    height: parent.height
                }

                RoundCorners {
                    anchors.fill: parent
                    backgroundColor: Style.opaqueMasonryViewBackground
                }

                Rectangle {
                    anchors.fill: parent
                    visible: isCurrent || thumbnailMouse.containsMouse

                    color: "transparent"
                    border.width: 2
                    border.color: thumbnailMouse.pressed ? Style.brickImagePressed : (isCurrent ? Style.brickImageSelected : Style.brickImageHovered)
                    radius: 4
                }

                MouseArea {
                    id: thumbnailMouse
                    anchors.fill: parent

                    hoverEnabled: true

                    onClicked: {
                        masonryLayout.view.currentIndex = index
                        masonryLayout.setCurrentIndex(imageModel.mapToSourceRow(masonryLayout.view.currentIndex))
                        onCurrentIndexChanged()
                    }
                }
            }
        }

        Slider {
            id: currentImageSlider
            x: parent.width - 6
            y: 0
            width: parent.height
            height: 10
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0

            handleVisible: false

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

    MouseArea {
        id: leftPanel
        anchors {
            top: parent.top
            left: parent.left
        }
        width: 180
        height: exifLayout.height + 10

        opacity: root.state === "viewer" && (panelsVisible || leftPanel.containsMouse)
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
        }

        hoverEnabled: true

        Rectangle {
            anchors {
                fill: parent
                leftMargin: -radius
                topMargin: -radius
            }
            color: width < viewerBoundaries.x || height < viewerBoundaries.y ? Style.viewerPanel : Style.opaqueMasonryViewBackgroundWithOpacity
            radius: 4
        }

        ColumnLayout {
            id: exifLayout
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                leftMargin: 12
                rightMargin: 6
            }
            spacing: 0

            Repeater {
                model: masonryLayout.view.currentImageExif
                delegate: RowLayout {
                    property bool isTitle: modelData.title !== undefined

                    Layout.topMargin: !index ? 10 : isTitle ? 15 : 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: exifText.height + 2

                    spacing: 7

                    IconLabel {
                        visible: modelData.icon !== undefined
                        icon.source: modelData.icon !== undefined ? modelData.icon : ""
                        icon.width: 15
                        icon.height: 15
                        icon.color: Style.text
                    }

                    Text {
                        id: exifText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: modelData.url !== undefined ? modelData.text.replace(" ", "\n") : modelData.text
                        color: isTitle || !index ? Style.text : Style.textGray
                        font.pixelSize: !index ? 12 : 16
                        font.underline: modelData.url !== undefined

                        MouseArea {
                            id: exifMouse
                            anchors.fill: parent

                            hoverEnabled: true
                            cursorShape: modelData.url !== undefined ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onClicked: {
                                if (modelData.url !== undefined) {
                                    Qt.openUrlExternally(modelData.url)
                                }
                            }
                        }

                        ToolTip {
                            id: tooltip

                            visible: exifMouse.containsMouse && exifText.implicitWidth > exifText.width
                            text: modelData.text
                        }
                    }
                }
            }
        }
    }
}
