import QtQuick

import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: viewerMode

    visible: false

    property int animationDuration: 150
    property int easingType: Easing.OutSine

    property real sphericViewerOpacity: 1
    property bool sphericViewerMode: false
    onSphericViewerModeChanged: {
        if (sphericViewerMode) {
            flickableArea.image.mipmap = false

            fileListModel.cancelAllDecodeViewerRunners()
            fileListModel.requestViewer(masonryLayout.view.currentIndex)
            sphericViewerLoader.sourceComponent = sphericViewerComponent
        }
        else {
            flickableArea.image.mipmap = true
            sphericViewerLoader.sourceComponent = undefined
        }
    }

    property bool panelsVisible: false
    property alias zoomFitView: flickableArea.zoomFitView

    property alias animation: viewerAnimation
    property alias image: flickableArea.image
    property alias imageContainer: flickableArea

    // ZZZ: Viewer size request takes current image's full size for all future images!!!

    function setImage(imageId, originalSize) {
        image.source = imageId
        flickableArea.originalSize = Qt.size(originalSize.width / dpr, originalSize.height / dpr)
    }

    function show(sphericViewer) {
        sphericViewerMode = sphericViewer === "True"
        onCurrentIndexChanged()
        visible = true
        flickableArea.zoomToFit(true)
    }

    function onCurrentIndexChanged() {
        let exif = masonryLayout.view.indexExif(masonryLayout.view.currentIndex)
        sphericViewerMode = exif["Panorama"] === "True"

        if (zoomFitView && !sphericViewerMode) {
            // console.log("onCIC FIT", viewerMode.width * dpr, viewerMode.height * dpr)
            fileListModel.requestViewer(masonryLayout.view.currentIndex, viewerMode.width * dpr, viewerMode.height * dpr)
        }
        else {
            // console.log("onCIC ORIG", flickableArea.originalSize.width * dpr, flickableArea.originalSize.height * dpr)
            fileListModel.requestViewer(masonryLayout.view.currentIndex)
            flickableArea.forceShowScrollBars = true
            flickableArea.forceShowScrollBars = false
        }

        if (masonryLayout.view.currentItem) {
            topLevelWindow.title = masonryLayout.view.currentItem.text + " [" +
                    (masonryLayout.view.currentImageIndex + 1) + "/" + masonryLayout.view.imageCount + "] - ZoinGallery"
        }
    }

    property bool leftPressed: false
    property bool rightPressed: false
    property bool upPressed: false
    property bool downPressed: false
    property bool zoomInPressed: false
    property bool zoomOutPressed: false
    property bool controlPressed: false

    Keys.onPressed:
        (event) => {
            let nextIndex = -1
            let currentIndex = masonryLayout.view.currentIndex
            if (!zoomFitView && (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up ||
                                 event.key === Qt.Key_Down) ||
                                 event.key === Qt.Key_Plus || event.key === Qt.Key_Minus || event.key === Qt.Key_Equal ||
                                 event.key === Qt.Key_Control) {
                if (event.isAutoRepeat) {
                    return
                }
                if (event.key === Qt.Key_Left) {
                    leftPressed = true
                }
                else if (event.key === Qt.Key_Right) {
                    rightPressed = true
                }
                else if (event.key === Qt.Key_Up) {
                    upPressed = true
                }
                else if (event.key === Qt.Key_Down) {
                    downPressed = true
                }
                else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                    zoomInPressed = true
                }
                else if (event.key === Qt.Key_Minus) {
                    zoomOutPressed = true
                }
                else if (event.key === Qt.Key_Control) {
                    controlPressed = true
                }

                let speed = controlPressed ? 0.06 : 1
                flickableArea.startZoomScrollingAnimation(leftPressed ? speed : rightPressed ? -speed : 0,
                                                          upPressed ? speed : downPressed ? -speed : 0,
                                                          zoomInPressed ? speed : zoomOutPressed ? -speed : 0)
            }
            else if ((event.key === Qt.Key_Left || event.key === Qt.Key_PageUp || event.key === Qt.Key_Backspace ||
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
            else if (event.key === Qt.Key_Asterisk || event.key === Qt.Key_9) {
                flickableArea.zoomTo100()
            }
            else if (event.key === Qt.Key_0 && (event.modifiers & Qt.ControlModifier)) {
                flickableArea.zoomToFit()
            }
            else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Slash || event.key === Qt.Key_0) {
                flickableArea.toggleZoomToFit()
            }
            else if (event.key === Qt.Key_S || event.key === Qt.Key_P) {
                sphericViewerMode = !sphericViewerMode
            }

            if (nextIndex !== -1 && nextIndex !== currentIndex) {
                onCurrentIndexChanged()
            }
    }

    Keys.onReleased:
        (event) => {

            if (!event.isAutoRepeat) {
                if (event.key === Qt.Key_Left) {
                    leftPressed = false
                }
                else if (event.key === Qt.Key_Right) {
                    rightPressed = false
                }
                else if (event.key === Qt.Key_Up) {
                    upPressed = false
                }
                else if (event.key === Qt.Key_Down) {
                    downPressed = false
                }
                else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                    zoomInPressed = false
                }
                else if (event.key === Qt.Key_Minus) {
                    zoomOutPressed = false
                }
                else if (event.key === Qt.Key_Control) {
                    controlPressed = false
                }
            }

            if (!zoomFitView && (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up ||
                                 event.key === Qt.Key_Down ||
                                 event.key === Qt.Key_Plus || event.key === Qt.Key_Minus || event.key === Qt.Key_Equal ||
                                 event.key === Qt.Key_Control)) {
                if (event.isAutoRepeat) {
                    return
                }

                let speed = controlPressed ? 0.06 : 1
                flickableArea.startZoomScrollingAnimation(leftPressed ? speed : rightPressed ? -speed : 0,
                                                      upPressed ? speed : downPressed ? -speed : 0,
                                                      zoomInPressed ? speed : zoomOutPressed ? -speed : 0)
                if (event.key === Qt.Key_Control) {
                    if (!leftPressed && !rightPressed && !upPressed && !downPressed && !zoomInPressed && !zoomOutPressed) {
                        flickableArea.onControlReleased()
                    }
                }
            }
        }

    Connections {
        target: masonryLayout.view
        function onCurrentIndexChanged() {
            if (root.state === "viewer") {
                let imageId = masonryLayout.view.indexImage(masonryLayout.view.currentIndex)
                // console.log("ZZ INDEX CHANGE 2", masonryLayout.view.currentIndex, imageId)
                if (imageId) {
                    setImage(imageId, masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex))
                    if (zoomFitView) {
                        flickableArea.zoomToFit(true)
                        // console.log("ZZ FIT ON CHANGE")
                    }
                    else {
                        flickableArea.fitViewerImageInViewportBounds()
                        // console.log("ZZ ELSE")
                    }
                }
            }
        }
    }

    Connections {
        target: fileListModel
        function onViewerImageIdChanged(newImageId) {
            viewerMode.setImage(newImageId, masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex))
        }
    }

    FlickableZoomable {
        id: flickableArea

        // visible: !sphericViewerMode
        width: parent.width
        height: parent.height
        infoVisible: !viewerAnimation.running
        animationDuration: viewerMode.animationDuration

        onClicked: panelsVisible = !panelsVisible

        Rectangle {
            id: delegateOutline
            anchors {
                fill: image
                margins: -2 //selectionExtendsForImage
            }
            color: Style.brickImageSelected
            radius: 4
            z: -1
        }

        Item {
            id: imageInfoPanel
            anchors {
                left: image.left
                right: image.right
                bottom: image.bottom
            }
            height: imageText.height + 10
            z: 1
            clip: true

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: -radius
                radius: 4
                color: Style.brickInfoPanelSelected
            }


            Text {
                id: imageText
                anchors{
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    margins: 5
                }

                text: masonryLayout.view.indexText(masonryLayout.view.currentIndex)
                textFormat: masonryLayout.quickSearchMode ? Text.RichText : Text.PlainText

                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                color: Style.text
                maximumLineCount: 4
                wrapMode: Text.Wrap
            }
        }
    }

    Loader {
        id: sphericViewerLoader
        anchors.fill: flickableArea
    }

    Component {
        id: sphericViewerComponent

        SphericViewer {
            originalSize: flickableArea.originalSize
            source: flickableArea.image
            opacity: sphericViewerOpacity
        }
    }


    MouseArea {
        id: viewerMouse
        anchors.fill: parent
        enabled: root.state === "viewer" // && zoomFitView

        acceptedButtons: Qt.LeftButton | Qt.MiddleButton

        onPressed:
            (mouse) => {
                if (mouse.button === Qt.MiddleButton) {
                    topLevelWindow.toggleFullscreen()
                }
                else if (mouse.button === Qt.LeftButton) {
                    mouse.accepted = false
                }
            }

        onWheel:
            (wheel) => {
                if (!(wheel.modifiers === Qt.ControlModifier || (wheel.buttons & Qt.LeftButton))) {
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
                else {
                    wheel.accepted = false
                }
            }
    }

    /*NumberAnimation {
        id: viewerAnimation

        property real x
        property real y
        property real width
        property real height

        duration: 0

        onFinished: {
            if (root.state === "thumbnails") {
                viewerMode.visible = false
            }
        }

    }*/

    ParallelAnimation {
        id: viewerAnimation

        property alias x: viewerMaximizeAnimationX.to
        property alias y: viewerMaximizeAnimationY.to
        property alias width: viewerMaximizeAnimationWidth.to
        property alias height: viewerMaximizeAnimationHeight.to

        NumberAnimation {
            id: viewerMaximizeAnimationX
            target: flickableArea
            property: "x"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationY
            target: flickableArea
            property: "y"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationWidth
            target: flickableArea
            property: "width"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationHeight
            target: flickableArea
            property: "height"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            target: delegateOutline
            property: "opacity"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
            to: root.state === "viewer" ? 0 : 1
        }

        NumberAnimation {
            target: imageInfoPanel
            property: "opacity"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
            to: root.state === "viewer" ? 0 : 1
        }

        NumberAnimation {
            target: viewerMode
            property: "sphericViewerOpacity"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
            to: root.state === "viewer" ? 1 : 0
        }

        onFinished: {
            if (root.state === "thumbnails") {
                viewerMode.visible = false
            }
            else {
                // image.x = Qt.binding(function() {return zoomFitView ? 0 : zoomCenterOffsetX})
                // image.y = Qt.binding(function() {return zoomFitView ? 0 : zoomCenterOffsetY})
                flickableArea.width = Qt.binding(() => {return viewerMode.width})
                flickableArea.height = Qt.binding(() => {return viewerMode.height})
            }
        }
    }

    // Flickable {
    //     anchors.fill: parent
    //     contentWidth: image.width
    //     contentHeight: image.height

    //     ScrollBar.horizontal: ScrollBar {}
    //     ScrollBar.vertical: ScrollBar {}

    //     Image {
    //         source: image.source
    //         width: image.width
    //         height: image.height
    //     }
    // }

    MouseArea {
        id: rightPanel
        anchors {
            top: parent.top
            topMargin: isQWK ? titleBar.height : 0
            right: parent.right
            bottom: parent.bottom
        }
        width: 120

        property bool viewerOverlapsFilmstrip: x < flickableArea.image.x + flickableArea.image.width
        onViewerOverlapsFilmstripChanged: {
            titleBar.backgroundVisible = viewerOverlapsFilmstrip
        }

        opacity: root.state === "viewer" && (panelsVisible || rightPanel.containsMouse)
        visible: opacity !== 0
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
                    id: image

                    width: parent.width
                    height: parent.height
                    source: "image://thumbnails/" + imageIdRole

                    fillMode: Image.PreserveAspectFit
                    cache: false
                    // Async adds black blinking for folder views
                    //asynchronous: true
                    mipmap: true
                    visible: false
                }

                ShaderEffect {
                    id: imageShader
                    property real aspect: image.sourceSize.width / image.sourceSize.height
                    property bool useHeight: (image.sourceSize.height * image.width / image.height) <= image.sourceSize.width

                    anchors.centerIn: parent
                    width: useHeight ? image.width : (image.height * aspect)
                    height: useHeight ? (image.width / aspect) : image.height

                    property var source: image
                    property var viewportSize: Qt.size(width * dpr, height * dpr)
                    property real sharpenAmount: 2
                    property bool showCheckerboard: masonryLayout.view.showTransparentGrid
                    property int checkerboardSize: 4 * dpr
                    property real borderRadius: 4.1 * dpr

                    fragmentShader: "qrc:/resources/shader.frag.qsb"
                    visible: image.source != ""
                }

                /*Image {
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
                }*/

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
                        masonryLayout.setCurrentIndex(imageModel.mapToSourceRow(index))
                        onCurrentIndexChanged()
                    }
                }
            }
        }

        Slider {
            id: currentImageSlider
            x: parent.width
            y: 0
            width: parent.height
            height: 16
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0

            handleVisible: false
            visualHeight: 10

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
        visible: opacity !== 0
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
            color: width < flickableArea.image.x || height < flickableArea.image.y ? Style.viewerPanel : Style.opaqueMasonryViewBackgroundWithOpacity
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
