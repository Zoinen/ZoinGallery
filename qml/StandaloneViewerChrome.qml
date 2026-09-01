import QtQuick

import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects
import ZoinGallery 1.0
import ZoinGallery.Native 1.0

pragma ComponentBehavior: Bound

Item {
    id: chrome

    required property Item viewer
    required property Item shell
    required property QtObject hostWindow
    required property Item titleBarItem
    required property Item imageViewport
    required property Item viewportContainer
    required property Loader sphereLoader
    required property Item minimizeButton
    required property Item maximizeButton
    required property Item closeButton
    required property bool quickWindowKitEnabled
    property real devicePixelRatio: 1.0

    readonly property alias topChrome: topPanel
    readonly property alias rightChrome: rightPanel
    readonly property alias leftChrome: leftPanel

    component BlurBackground : MultiEffect {
        id: blurItem
        source: ShaderEffectSource {
            sourceItem: chrome.viewer.sphericViewerMode ? chrome.sphereLoader.item : chrome.viewportContainer
            width: blurItem.width
            height: blurItem.height
            sourceRect: Qt.rect(blurItem.parent.x, blurItem.parent.y, blurItem.parent.width, blurItem.parent.height)
        }

        anchors.fill: parent
        // opacity: 0.5
        contrast: Style.isDarkTheme ? -0.5 : -0.7
        brightness: Style.isDarkTheme ? 0 : 0.35
        // saturation: -0.5

        colorization: 0.6
        colorizationColor: Style.viewerPanelBackground
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: 64
        blur: 0.3

        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }

    component CanvasText : Item {
        id: canvasTextControl
        implicitWidth: canvasText.width
        implicitHeight: canvasText.height

        property string text
        property alias font: canvasDummyText.font
        property alias texture: canvasText
        property bool elide: false

        Text {
            id: canvasDummyText
            text: canvasTextControl.text
            font.pixelSize: 14
            visible: false
            width: canvasTextControl.elide ? parent.width : implicitWidth

            onTextChanged: canvasText.requestPaint()
        }

        Canvas {
            id: canvasText
            width: canvasDummyText.width
            height: canvasDummyText.height
            onWidthChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();

                var text = canvasDummyText.text;
                var fontSize = canvasDummyText.font.pixelSize;
                var fontFamily = canvasDummyText.font.family;
                var fillColor = Style.viewerMainText;

                ctx.font = fontSize + "px \"" + fontFamily + "\"";
                ctx.textBaseline = "top";
                ctx.lineJoin = 'miter';
                ctx.miterLimit = 2;

                ctx.fillStyle = fillColor;

                if (canvasTextControl.elide) {
                    var ellipsis = "...";
                    var ellipsisWidth = ctx.measureText(ellipsis).width;
                    var textWidth = ctx.measureText(text).width;

                    // Check if text fits
                    if (textWidth > width) {
                        // Split text into start and end parts
                        var startText = text;
                        var endText = text;

                        // Remove characters from the middle until text fits with ellipsis
                        while (ctx.measureText(startText + ellipsis + endText).width > width && startText.length > 0 && endText.length > 0) {
                            startText = startText.slice(0, -1);  // Shorten the start
                            endText = endText.slice(1);          // Shorten the end
                        }

                        // Combine start, ellipsis, and end parts
                        text = startText + ellipsis + endText;
                    }
                }
                ctx.fillText(text, 0, 0);
            }
        }
    }

    component OutlineAndShadowEffect : ShaderEffect {
        property var source
        property color outlineColor: Style.viewerMainTextOutline
        property real outlineWidth: 0.5
        property real outlineOpacity: 0.6
        property size textureSize: Qt.size(width, height)

        property real blurRadius: 2.0
        property color blurColor: Style.viewerMainTextOutline
        property real blurOpacity: 0.7

        fragmentShader: "qrc:/ZoinGallery/resources/outline.frag.qsb"
    }


    Item {
        id: topPanel

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: chrome.titleBarItem.viewerHeight

        opacity: chrome.viewer.viewerChromeOpacity
        visible: opacity !== 0
        Behavior on opacity {
            NumberAnimation { duration: chrome.viewer.animationDuration; easing.type: chrome.viewer.easingType }
        }
        property bool hovered: false
        property alias backgroundOpacity: topBarBackground.opacity

        property string fileName: {
            let rotationStr = ""
            if (chrome.imageViewport.rotationMode === 1) rotationStr = " [90°]"
            else if (chrome.imageViewport.rotationMode === 2) rotationStr = " [180°]"
            else if (chrome.imageViewport.rotationMode === 3) rotationStr = " [270°]"
            return chrome.viewer.sourceMasonry.view.indexText(chrome.viewer.sourceMasonry.view.currentIndex) + rotationStr
        }

        component TitleProxyButton : TitleButton {
            property var proxyControl
            property bool backgroundVisible: true
            property bool contentVisible: true

            source: contentVisible || proxyControl.hovered ? proxyControl.source : ""
            icon.color: proxyControl.icon.color === Style.text ? Style.viewerMainText : proxyControl.icon.color
            backgroundColor: backgroundVisible ? proxyControl.backgroundColor : "transparent"
            hoveredOverride: proxyControl.hovered
            pressedOverride: proxyControl.pressed
        }


        Rectangle {
            id: topBarRect
            width: topPanel.width
            height: topPanel.height

            layer.enabled: true
            visible: false
        }

        BlurBackground {
            id: topBarBackground
            opacity: topPanel.hovered ? chrome.viewer.viewerChromeOpacity : 0
            visible: opacity !== 0
            Behavior on opacity {
                NumberAnimation { duration: chrome.viewer.animationDuration; easing.type: chrome.viewer.easingType }
            }
            maskSource: topBarRect
        }

        Timer {
            repeat: true
            running: chrome.shell.state === "viewer"
            interval: 50
            onTriggered: {
                let pos = chrome.hostWindow.mousePos()
                pos = chrome.titleBarItem.mapFromGlobal(pos.x, pos.y)
                let containsPos = pos.x >= chrome.titleBarItem.x && pos.y >= chrome.titleBarItem.y && pos.x <= chrome.titleBarItem.x + chrome.titleBarItem.width && pos.y <= chrome.titleBarItem.y + chrome.titleBarItem.height
                topPanel.hovered = chrome.hostWindow.isPressedOnTitleBar() && containsPos
            }
        }

        Item {
            id: topPanelRowContainer
            anchors.fill: parent

            RowLayout {
                id: topPanelRow
                anchors {
                    left: parent.left
                    leftMargin: chrome.hostWindow.macTitleBarLeftPadding
                    top: parent.top
                    right: parent.right
                    rightMargin: chrome.hostWindow.useMacNativeTitleBar ? 0 : titleBarButtonsRow.width
                    bottom: parent.bottom
                }
                spacing: 0
                clip: true

                CanvasText {
                    id: canv1
                    text: topPanel.fileName
                    elide: true
                    Layout.leftMargin: 12
                    Layout.fillWidth: true
                    Layout.bottomMargin: 1
                }

                IconLabel {
                    Layout.leftMargin: 13
                    icon.source: "qrc:/ZoinGallery/resources/Sphere.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: Style.viewerMainText

                    visible: chrome.viewer.sphericViewerMode
                }

                Text {
                    id: text2
                    Layout.leftMargin: chrome.viewer.sphericViewerMode ? 5 : 13
                    Layout.rightMargin: 5
                    verticalAlignment: Text.AlignVCenter
                    Layout.bottomMargin: 1
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth

                    text: (chrome.viewer.sphericViewerMode ? (chrome.sphereLoader.item ? (Math.round(chrome.sphereLoader.item.fovVisual) + "°") : "") : ((chrome.viewer.zoomFitView ? "* " : "") + (Math.round(chrome.imageViewport.zoomScale * 100) + "%"))) +
                          " " + chrome.viewer.selectionModel.selectedCount
                    font.pixelSize: 14
                    color: Style.viewerMainText
                }

                Item {
                    id: previousLockIndicator

                    Layout.leftMargin: 8
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: chrome.titleBarItem.viewerHeight

                    visible: chrome.viewer.previousImageLocked && chrome.viewer.previousImageIndex !== -1

                    IconLabel {
                        anchors.centerIn: parent

                        icon.source: "qrc:/ZoinGallery/resources/TildeLock.svg"
                        icon.width: 16
                        icon.height: 16
                        icon.color: Style.viewerMainText
                    }

                    MouseArea {
                        id: previousLockMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    ToolTip {
                        visible: previousLockMouse.containsMouse
                        delay: 500
                        timeout: 5000

                        contentItem: ColumnLayout {
                            spacing: 8

                            Image {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 96

                                source: chrome.viewer.lockedPreviousImageIdUrl
                                sourceSize.width: 96
                                sourceSize.height: 96
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                            }

                            Text {
                                Layout.maximumWidth: 260

                                text: chrome.viewer.lockedPreviousPath
                                wrapMode: Text.WrapAnywhere
                                font.pixelSize: 12
                                color: Style.text
                            }
                        }

                        background: Rectangle {
                            color: Style.tooltipBackground
                            border.color: Style.tooltipBorder
                            radius: 5
                        }
                    }

                    Component.onCompleted: {
                        windowAgent.setHitTestVisible(previousLockIndicator)
                    }
                }

                Button {
                    id: settingsButton

                    icon.width: 10
                    icon.height: 10

                    implicitWidth: 36
                    implicitHeight: chrome.titleBarItem.viewerHeight

                    icon.source: "qrc:/ZoinGallery/resources/Settings.svg"
                    onClicked: chrome.viewer.panelsVisible = !chrome.viewer.panelsVisible
                    Component.onCompleted: {
                        windowAgent.setHitTestVisible(settingsButton)
                    }
                }

                component Separator : Rectangle {
                    Layout.leftMargin: 14
                    Layout.rightMargin: 14
                    implicitWidth: 1
                    implicitHeight: 20
                    color: "#505050"
                }

                Separator {
                    visible: !chrome.hostWindow.useMacNativeTitleBar
                }
            }

            Row {
                id: titleBarButtonsRow
                anchors {
                    right: parent.right
                    top: parent.top
                }
                visible: !chrome.hostWindow.useMacNativeTitleBar

                TitleProxyButton {
                    proxyControl: chrome.minimizeButton
                    backgroundVisible: false
                }

                TitleProxyButton {
                    proxyControl: chrome.maximizeButton
                    backgroundVisible: false
                }

                TitleProxyButton {
                    proxyControl: chrome.closeButton
                    backgroundVisible: false
                }
            }
        }

        OutlineAndShadowEffect {
            width: topPanelRowContainer.width
            height: topPanelRowContainer.height
            source: ShaderEffectSource {
                sourceItem: topPanelRowContainer
                hideSource: true
            }

            outlineOpacity: 0.6 * (1 - topBarBackground.opacity)
            blurOpacity: 0.7 * (1 - topBarBackground.opacity)
        }

        Row {
            id: titleBarButtonsBackground
            anchors {
                top: parent.top
                right: parent.right
            }
            spacing: 0
            visible: !chrome.hostWindow.useMacNativeTitleBar

            TitleProxyButton {
                proxyControl: chrome.minimizeButton
                contentVisible: false
            }

            TitleProxyButton {
                proxyControl: chrome.maximizeButton
                contentVisible: false
            }

            TitleProxyButton {
                proxyControl: chrome.closeButton
                contentVisible: false
            }
        }
    }


    MouseArea {
        id: rightPanel
        anchors {
            top: parent.top
            topMargin: chrome.quickWindowKitEnabled ? chrome.titleBarItem.viewerHeight : 0
            right: parent.right
        }
        width: 120
        property int fullContentHeight: filmstrip.count * (57 + filmstrip.spacing) + filmstrip.spacing
        height: Math.min(fullContentHeight, parent.height - y)

        property bool viewerOverlapsFilmstrip: x < chrome.imageViewport.image.x + chrome.imageViewport.image.width

        property bool listContentsFitScreen: rightPanel.fullContentHeight < rightPanel.parent.height - rightPanel.y

        opacity: (chrome.viewer.panelsVisible || rightPanel.containsMouse) ? chrome.viewer.viewerChromeOpacity : 0
        visible: opacity !== 0
        Behavior on opacity {
            NumberAnimation { duration: chrome.viewer.animationDuration; easing.type: chrome.viewer.easingType }
        }

        hoverEnabled: true

        Rectangle {
            id: bgRect
            width: rightPanel.width
            height: rightPanel.height

            layer.enabled: true
            visible: false

            topLeftRadius: 8 * (1 - topPanel.backgroundOpacity)
            bottomLeftRadius: rightPanel.listContentsFitScreen ? 8 : 0
        }

        BlurBackground {
            maskSource: bgRect
            maskEnabled: true
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
            topMargin: spacing
            bottomMargin: spacing
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            interactive: false

            model: chrome.viewer.filmstripModel

            Connections {
                target: chrome.viewer.sourceMasonry.view

                function onCurrentIndexChanged() {
                    filmstrip.positionViewAtIndex(chrome.viewer.filmstripModel.mapFromSourceRow(chrome.viewer.sourceMasonry.view.currentIndex), ListView.Center)
                }
            }


            delegate: Item {
                id: filmstripDelegate
                required property int index
                required property string imageIdUrlRole
                required property bool selectedRole
                required property color selectionGroupColorRole

                width: 86
                height: 57

                property bool isCurrent: chrome.viewer.filmstripModel.mapFromSourceRow(chrome.viewer.sourceMasonry.view.currentIndex) === filmstripDelegate.index

                Image {
                    id: filmstripImage

                    width: parent.width
                    height: parent.height
                    source: filmstripDelegate.imageIdUrlRole

                    fillMode: Image.PreserveAspectFit
                    cache: false
                    // Async adds black blinking for folder views
                    // asynchronous: true
                    mipmap: true
                    visible: false
                }

                ShaderEffect {
                    id: imageShader
                    property real aspect: filmstripImage.sourceSize.width / filmstripImage.sourceSize.height
                    property bool useHeight: (filmstripImage.sourceSize.height * filmstripImage.width / filmstripImage.height) <= filmstripImage.sourceSize.width

                    anchors.centerIn: parent
                    width: useHeight ? filmstripImage.width : (filmstripImage.height * aspect)
                    height: useHeight ? (filmstripImage.width / aspect) : filmstripImage.height

                    property var source: filmstripImage
                    property var viewportSize: Qt.size(width * chrome.devicePixelRatio, height * chrome.devicePixelRatio)
                    property real sharpenAmount: 2
                    property bool showCheckerboard: chrome.viewer.sourceMasonry.view.showTransparentGrid
                    property int checkerboardSize: 4 * chrome.devicePixelRatio
                    property real borderRadius: 4.1 * chrome.devicePixelRatio

                    fragmentShader: "qrc:/ZoinGallery/resources/shader.frag.qsb"
                    visible: filmstripImage.source != ""
                }

                /*Image {
                    id: thumbnailImage
                    source: parent.imageIdUrlRole
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
                    anchors.centerIn: parent
                    width: imageShader.width + 6
                    height: imageShader.height + 6
                    visible: isCurrent || thumbnailMouse.containsMouse

                    color: "transparent"
                    border.width: 2
                    border.color: thumbnailMouse.pressed ? Style.brickImagePressed : (isCurrent ? Style.brickImageSelected : Style.brickImageHovered)
                    radius: 6
                    z: 2
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: imageShader.width
                    height: imageShader.height
                    visible: filmstripDelegate.selectedRole

                    color: "transparent"
                    border.width: 3
                    border.color: filmstripDelegate.selectionGroupColorRole
                    radius: 4
                    z: 3
                }

                MouseArea {
                    id: thumbnailMouse
                    anchors.fill: parent

                    hoverEnabled: true

                    onClicked: {
                        chrome.viewer.sourceMasonry.setCurrentIndex(chrome.viewer.filmstripModel.mapToSourceRow(filmstripDelegate.index))
                    }
                }
            }
        }

        Slider {
            id: currentImageSlider
            x: parent.width
            y: 3
            width: parent.height - 6
            height: 16
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0

            handleVisible: false
            visualHeight: 10

            from: 0
            to: chrome.viewer.sourceMasonry.view.imageCount - 1
            value: chrome.viewer.sourceMasonry.view.currentImageIndex
            stepSize: 1
            snapMode: Slider.SnapAlways

            rotation: 90
            transformOrigin: Item.TopLeft

            onValueChanged: {
                if (pressed) {
                    chrome.viewer.sourceMasonry.view.currentImageIndex = Math.round(value)
                    chrome.viewer.sourceMasonry.setCurrentIndex(chrome.viewer.sourceMasonry.view.currentIndex)
                }
            }

            Connections {
                target: chrome.viewer.sourceMasonry.view
                function onCurrentImageIndexChanged() {
                    currentImageSlider.value = chrome.viewer.sourceMasonry.view.currentImageIndex
                }
            }
        }
    }

    MouseArea {
        id: leftPanel
        anchors {
            top: parent.top
            topMargin: chrome.titleBarItem.viewerHeight
            left: parent.left
        }
        width: 180
        height: exifLayout.height > 0 ? (exifLayout.height + 10) : 0

        opacity: (chrome.viewer.panelsVisible || leftPanel.containsMouse) ? chrome.viewer.viewerChromeOpacity : 0
        visible: opacity !== 0
        Behavior on opacity {
            NumberAnimation { duration: chrome.viewer.animationDuration; easing.type: chrome.viewer.easingType }
        }

        hoverEnabled: true

        Rectangle {
            id: leftPanelBg

            width: leftPanel.width
            height: leftPanel.height

            layer.enabled: true
            visible: false

            bottomRightRadius: 8
            topRightRadius: 8 * (1 - topPanel.backgroundOpacity)
        }

        BlurBackground {
            maskSource: leftPanelBg
            maskEnabled: true
        }
        /*Rectangle {
            anchors {
                fill: parent
            }
            color: "transparent" // width < chrome.imageViewport.image.x || height < chrome.imageViewport.image.y ? Style.viewerPanel : Style.opaqueMasonryViewBackgroundWithOpacity
            bottomRightRadius: 7
        }*/

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
                model: chrome.viewer.sourceMasonry.view.currentImageExif
                delegate: RowLayout {
                    id: exifRow
                    required property int index
                    required property var modelData
                    property bool isTitle: exifRow.modelData.title !== undefined

                    Layout.topMargin: !exifRow.index ? 10 : exifRow.isTitle ? 15 : 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: exifText.height + 2

                    spacing: 7

                    IconLabel {
                        visible: exifRow.modelData.icon !== undefined
                        icon.source: exifRow.modelData.icon !== undefined ? exifRow.modelData.icon : ""
                        icon.width: 15
                        icon.height: 15
                        icon.color: Style.viewerSecondaryText
                    }

                    Text {
                        id: exifText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: exifRow.modelData.url !== undefined ? exifRow.modelData.text.replace(" ", "\n") : exifRow.modelData.text
                        wrapMode: exifRow.modelData.multiline !== undefined ? Text.Wrap : Text.NoWrap
                        color: !isTitle ? Style.viewerMainText : Style.viewerSecondaryText
                        font.pixelSize: 16
                        font.underline: exifRow.modelData.url !== undefined

                        MouseArea {
                            id: exifMouse
                            anchors.fill: parent

                            hoverEnabled: true
                            cursorShape: exifRow.modelData.url !== undefined ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onClicked: {
                                if (exifRow.modelData.url !== undefined) {
                                    Qt.openUrlExternally(exifRow.modelData.url)
                                }
                            }
                        }

                        ToolTip {
                            id: tooltip

                            visible: exifMouse.containsMouse && exifText.implicitWidth > exifText.width
                            text: exifRow.modelData.text
                        }
                    }
                }
            }
        }
    }
}
