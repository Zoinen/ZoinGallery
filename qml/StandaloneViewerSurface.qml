import QtQuick

import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects
import ZoinGallery 1.0
import ZoinGallery.Native 1.0

pragma ComponentBehavior: Bound

Item {
    id: surface

    required property Item viewer
    required property Item shell
    required property QtObject hostWindow
    required property Item titleBarItem
    required property Item topChrome
    required property Item rightChrome
    property real devicePixelRatio: 1.0

    property alias zoomFitView: flickableArea.zoomFitView
    property alias animation: viewerAnimation
    readonly property alias viewport: flickableArea
    readonly property alias viewportContainer: flickableAreaContainer
    readonly property alias outline: delegateOutline
    readonly property alias imageInfo: imageInfoPanel
    readonly property alias sphereLoader: sphericViewerLoader
    readonly property alias sphereComponent: sphericViewerComponent
    readonly property alias neighborImage: viewerNavigationNeighborImage
    readonly property alias navigationFinishTimer: viewerNavigationFinishTimer
    readonly property alias wheelPanFinishTimer: viewerWheelPanFinishTimer
    readonly property alias gestureEndTimer: viewerNavigationGestureEndTimer
    readonly property alias residualQuietTimer: viewerNavigationResidualQuietTimer
    readonly property alias navigationOffsetAnimation: viewerNavigationOffsetAnimation
    readonly property alias wheelArea: viewerWheelArea
    readonly property alias pointerLayer: viewerMouse

    GalleryThemePalette {
        id: viewerScrollTheme
        scrollBarHandle: Style.scrollBarHandle
        scrollBarHandleBackgroundHovered:
            Style.scrollBarHandleBackgroundHovered
        scrollBarHandleHovered: Style.scrollBarHandleHovered
        scrollBarHandlePressed: Style.scrollBarHandlePressed
        scrollBarTrackHovered: Style.lighter
    }

    Rectangle {
        anchors.fill: parent
        color: surface.viewer.currentItemSelected
               ? surface.viewer.currentItemSelectionColor
               : surface.viewer.viewerNavigationTargetSelectionColor
        opacity: 0.16 * surface.viewer.viewerBackgroundOpacity * surface.viewer.selectionHighlightNavigationOpacity
        visible: opacity > 0
        z: -2

        Behavior on opacity {
            enabled: !surface.viewer.selectionHighlightAnimationSuppressed &&
                     !surface.viewer.viewerNavigationActive &&
                     surface.shell.state === "viewer" &&
                     surface.viewer.viewerBackgroundOpacity >= 0.999
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutSine
            }
        }
    }

    Item {
        id: flickableAreaContainer
        anchors.fill: parent

        FlickableZoomable {
            id: flickableArea

            viewerModel: surface.viewer.decodeModel
            sourceMasonry: surface.viewer.sourceMasonry
            active: surface.shell.state === "viewer"
            devicePixelRatio: surface.devicePixelRatio
            topInset: surface.titleBarItem.viewerHeight
            checkerboardEnabled: Boolean(surface.viewer.sourceMasonry && surface.viewer.sourceMasonry.view
                                         && surface.viewer.sourceMasonry.view.showTransparentGrid)
            scrollBarTheme: viewerScrollTheme
            onCloseRequested: surface.shell.toggleViewer()
            onMiddleClickRequested: surface.hostWindow.toggleFullscreen()
            // visible: !surface.viewer.sphericViewerMode
            width: parent.width
            height: parent.height
            animationDuration: surface.viewer.animationDuration
            scrollBarsRightMargin: surface.viewer.panelsVisible ? surface.rightChrome.width : 0
            hideVerticalScrollBar: surface.viewer.viewerNavigationActive || viewerNavigationOffsetAnimation.running ||
                    surface.viewer.viewerNavigationCommitAfterAnimation || Math.abs(surface.viewer.viewerNavigationOffsetX) > 0.1
            pinchZoomEnabled: !surface.viewer.sphericViewerMode
            opacity: surface.viewer.viewerNavigationCurrentOpacity
            transform: Translate { x: surface.viewer.viewerNavigationCurrentOffsetX }
            onPinchZoomOutToThumbnailsProgressed: (progress) =>
                    surface.viewer.pinchZoomOutToThumbnailsProgressed(progress)
            onPinchZoomOutToThumbnailsFinished: (commit) =>
                    surface.viewer.pinchZoomOutToThumbnailsFinished(commit)

            Rectangle {
                id: delegateOutline
                anchors {
                    fill: flickableArea.image
                    margins: -2 //selectionExtendsForImage
                }
                color: Style.brickImageSelected
                radius: 4
                z: -1
            }

            Item {
                id: imageInfoPanel
                anchors {
                    left: flickableArea.image.left
                    right: flickableArea.image.right
                    bottom: flickableArea.image.bottom
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

                    text: surface.viewer.sourceMasonry.view.indexText(surface.viewer.sourceMasonry.view.currentIndex)
                    textFormat: surface.viewer.sourceMasonry.quickSearchMode ? Text.RichText : Text.PlainText

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: Style.viewerMainText
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }
            }

            component RectangleShadow : Rectangle {
                property var baseItem

                x: baseItem.x
                y: baseItem.y
                width: baseItem.width
                height: baseItem.height
                z: -1

                color: Style.viewerPanelBackground
                opacity: baseItem.opacity
            }
        }

        RectangleShadow {
            baseItem: leftPanel
            bottomRightRadius: 8
            topRightRadius: 8 * (1 - surface.topChrome.backgroundOpacity)
        }

        RectangleShadow {
            baseItem: surface.rightChrome
            topLeftRadius: 8 * (1 - surface.topChrome.backgroundOpacity)
            bottomLeftRadius: surface.rightChrome.listContentsFitScreen ? 8 : 0
        }

        RectangleShadow {
            baseItem: surface.topChrome
            opacity: surface.topChrome.backgroundOpacity
        }
    }

    Loader {
        id: sphericViewerLoader
        x: flickableArea.x
        y: flickableArea.y
        width: flickableArea.width
        height: flickableArea.height
        opacity: surface.viewer.viewerNavigationCurrentOpacity
        transform: Translate { x: surface.viewer.viewerNavigationCurrentOffsetX }
    }

    Component {
        id: sphericViewerComponent

        SphericViewer {
            originalSize: flickableArea.originalSize
            source: flickableArea.textureSource
            opacity: surface.viewer.sphericViewerOpacity
            easingType: surface.viewer.easingType
            onCloseRequested: surface.shell.toggleViewer()
            onSphereScrollingMouseCursorRequested:
                (set, idle, rotation) =>
                    surface.hostWindow.setSphereScrollingMouseCursor(
                        set, idle, rotation)
        }
    }

    Item {
        id: viewerNavigationNeighbor
        x: 0
        y: 0
        width: surface.viewer.width
        height: surface.viewer.height
        z: surface.viewer.viewerNavigationDirection < 0 ? 1 : -1
        opacity: surface.viewer.viewerNavigationTargetOpacity
        visible: opacity > 0 &&
                 surface.viewer.viewerNavigationActive &&
                 surface.viewer.viewerNavigationTargetIndex !== -1 && surface.viewer.viewerNavigationTargetSource !== ""

        Item {
            id: viewerNavigationNeighborEffectiveBounds
            x: surface.viewer.viewerNavigationTargetImageX
            y: surface.viewer.viewerNavigationTargetImageY
            width: surface.viewer.viewerNavigationTargetDisplayWidth
            height: surface.viewer.viewerNavigationTargetDisplayHeight

            // Match FlickableZoomable's effective-bounds/unrotated-content
            // structure. The shader samples the raw texture, so rotation must
            // happen around a source-aspect item rather than by stretching the
            // texture into the already-swapped effective bounds.
            Item {
                id: viewerNavigationNeighborUnrotatedContent
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                width: surface.viewer.viewerNavigationTargetHasSize
                       ? surface.viewer.viewerNavigationTargetDisplayOriginalSize.width
                         * viewerNavigationTargetScale : parent.width
                height: surface.viewer.viewerNavigationTargetHasSize
                        ? surface.viewer.viewerNavigationTargetDisplayOriginalSize.height
                          * viewerNavigationTargetScale : parent.height
                rotation: flickableArea.rotationMode * 90

                Image {
                    id: viewerNavigationNeighborImage
                    objectName: "standaloneViewerNavigationNeighborImage"
                    anchors.fill: parent
                    source: surface.viewer.viewerNavigationTargetSource
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    visible: false
                    // Level 1 already covers the requested physical Fit size.
                    // Native level 2 can still need minification.
                    mipmap: surface.viewer.viewerNavigationTargetSourceLevel === 2
                }

                ShaderEffect {
                    objectName: "standaloneViewerNavigationNeighborShader"
                    anchors.fill: parent

                    property var source: viewerNavigationNeighborImage
                    property var viewportSize: Qt.size(width * surface.devicePixelRatio,
                                                       height * surface.devicePixelRatio)
                    property real sharpenAmount:
                        surface.viewer.viewerNavigationTargetScale < 1 ? 1.5 : 0
                    property bool showCheckerboard:
                        flickableArea.checkerboardEnabled
                        && viewerNavigationNeighborImage.status === Image.Ready
                    property int checkerboardSize: 4 * surface.devicePixelRatio
                    property int borderRadius: 0

                    fragmentShader: "qrc:/ZoinGallery/resources/shader.frag.qsb"
                }
            }
        }
    }

    Timer {
        id: viewerNavigationFinishTimer
        interval: 140
        onTriggered: surface.viewer.finishViewerNavigation()
    }

    Timer {
        id: viewerWheelPanFinishTimer
        interval: 70
        onTriggered: flickableArea.finishWheelPan()
    }

    Timer {
        id: viewerNavigationGestureEndTimer
        interval: 350
        onTriggered: surface.viewer.endViewerNavigationGesture()
    }

    Timer {
        id: viewerNavigationResidualQuietTimer
        interval: 180
        onTriggered: surface.viewer.clearViewerNavigationResidualSuppression("quiet")
    }

    NumberAnimation {
        id: viewerNavigationOffsetAnimation
        target: surface.viewer
        property: "surface.viewer.viewerNavigationOffsetX"
        easing.type: surface.viewer.easingType

        onFinished: {
            if (surface.viewer.viewerNavigationCommitAfterAnimation) {
                surface.viewer.commitViewerNavigation()
            }
            else {
                surface.viewer.resetViewerNavigation()
                flickableArea.settlePan()
            }
        }
    }

    ViewerWheelArea {
        id: viewerWheelArea
        anchors.fill: parent
        enabled: surface.shell.state === "viewer"
        z: 2

        onWheelReceived:
            (pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers, buttons,
             hasPixelDelta, inverted, source, deviceType, nativeMomentum, nativePhase, nativeMomentumPhase) => {
                surface.viewer.handleViewerWheel(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers,
                                  buttons, hasPixelDelta, inverted, source, deviceType,
                                  nativeMomentum, nativePhase, nativeMomentumPhase)
            }

        onWheelForwarded: {
            surface.viewer.logViewerGesture("wheel forwarded to zoom/drag handler")
            surface.viewer.finishViewerNavigation()
        }

        onZoomWheelReceived:
            (angleDeltaY, modifiers, buttons) => {
                if (surface.viewer.sphericViewerMode && sphericViewerLoader.item) {
                    sphericViewerLoader.item.handleZoomWheel(
                                angleDeltaY, modifiers, buttons)
                }
                else {
                    flickableArea.handleZoomWheel(
                                angleDeltaY, modifiers, buttons)
                }
            }
    }

    MouseArea {
        id: viewerMouse
        anchors.fill: parent
        enabled: surface.shell.state === "viewer" // && surface.viewer.zoomFitView

        acceptedButtons: Qt.LeftButton

        onPressed:
            (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    mouse.accepted = false
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
            if (surface.shell.state === "thumbnails") {
                surface.viewer.visible = false
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
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationY
            target: flickableArea
            property: "y"
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationWidth
            target: flickableArea
            property: "width"
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationHeight
            target: flickableArea
            property: "height"
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
        }

        NumberAnimation {
            target: delegateOutline
            property: "opacity"
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
            to: surface.shell.state === "viewer" ? 0 : 1
        }

        NumberAnimation {
            target: imageInfoPanel
            property: "opacity"
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
            to: surface.shell.state === "viewer" ? 0 : 1
        }

        NumberAnimation {
            target: surface.viewer
            property: "surface.viewer.sphericViewerOpacity"
            duration: surface.viewer.animationDuration
            easing.type: surface.viewer.easingType
            to: surface.shell.state === "viewer" ? 1 : 0
        }

        onFinished: {
            if (surface.shell.state === "thumbnails") {
                surface.viewer.visible = false
            }
            else {
                // image.x = Qt.binding(function() {return surface.viewer.zoomFitView ? 0 : zoomCenterOffsetX})
                // image.y = Qt.binding(function() {return surface.viewer.zoomFitView ? 0 : zoomCenterOffsetY})
                flickableArea.width = Qt.binding(() => {return surface.viewer.width})
                flickableArea.height = Qt.binding(() => {return surface.viewer.height})
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

}
