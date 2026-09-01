pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

Item {
    id: root

    required property Item viewer

    readonly property alias viewport: viewportItem
    readonly property alias transitionFrame: frame
    readonly property alias navigationNeighborImage: neighborImage
    readonly property alias sphericLoader: sphereLoader
    readonly property alias backgroundItem: background
    readonly property alias wheelArea: wheelInput

    Rectangle {
        id: background
        objectName: "galleryViewerBackground"
        anchors.fill: parent
        color: root.viewer.backgroundColor
        opacity: root.viewer.surfaceProgress
        visible: !root.viewer.customContent && opacity > 0
    }

    Item {
        id: frame
        objectName: "galleryViewerTransitionFrame"
        visible: !root.viewer.customContent
                 && root.viewer.viewerContentVisible
        clip: true
        anchors.fill: parent
        opacity: root.viewer.transitionHasGeometry
                 ? 1 : root.viewer.transitionProgress

        Item {
            anchors.fill: parent
            opacity: root.viewer.viewerNavigationCurrentOpacity
            transform: Translate {
                x: root.viewer.viewerNavigationCurrentOffsetX
            }

            FlickableZoomable {
                id: viewportItem
                objectName: "galleryViewerViewport"
                x: root.viewer.pinchCloseActive ? 0
                   : root.viewer.transitionHasGeometry
                     ? root.viewer.lerp(
                           root.viewer.transitionSourceGeometry.x, 0,
                           root.viewer.transitionProgress) : 0
                y: root.viewer.pinchCloseActive ? 0
                   : root.viewer.transitionHasGeometry
                     ? root.viewer.lerp(
                           root.viewer.transitionSourceGeometry.y, 0,
                           root.viewer.transitionProgress) : 0
                width: root.viewer.pinchCloseActive ? root.viewer.width
                       : root.viewer.transitionHasGeometry
                         ? root.viewer.lerp(
                               root.viewer.transitionSourceGeometry.width,
                               root.viewer.width,
                               root.viewer.transitionProgress)
                         : root.viewer.width
                height: root.viewer.pinchCloseActive ? root.viewer.height
                        : root.viewer.transitionHasGeometry
                          ? root.viewer.lerp(
                                root.viewer.transitionSourceGeometry.height,
                                root.viewer.height,
                                root.viewer.transitionProgress)
                          : root.viewer.height
                active: !root.viewer.customContent
                        && !root.viewer.completingClose
                animationDuration: root.viewer.animationDuration
                devicePixelRatio: root.viewer.devicePixelRatio
                topInset: 0
                checkerboardEnabled: true
                scrollBarTheme: root.viewer.theme
                pinchZoomEnabled: !root.viewer.sphericViewerMode
                hideVerticalScrollBar:
                    root.viewer.viewerNavigationActive
                    || root.viewer.viewerNavigationAnimationRunning

                onZoomScaleChanged: root.viewer.scheduleDecodeRequest()
                onCloseRequested: root.viewer.requestClose()
                onMiddleClickRequested:
                    root.viewer.fullscreenToggleRequested()
                onPinchZoomOutToThumbnailsProgressed:
                    progress => root.viewer.updatePinchClose(progress)
                onPinchZoomOutToThumbnailsFinished:
                    commit => root.viewer.finishPinchClose(commit)
            }

            Loader {
                id: sphereLoader
                objectName: "gallerySphericViewerLoader"
                anchors.fill: viewportItem
                active: root.viewer.sphericViewerMode
                opacity: root.viewer.transitionHasGeometry
                         ? root.viewer.transitionProgress : 1

                sourceComponent: Component {
                    SphericViewer {
                        objectName: "gallerySphericViewer"
                        source: viewportItem.textureSource
                        originalSize: viewportItem.originalSize
                        easingType: Easing.OutSine

                        onCloseRequested: root.viewer.requestClose()
                        onSphereScrollingMouseCursorRequested:
                            (set, idle, rotation) =>
                                root.viewer.sphereScrollingMouseCursorRequested(
                                    set, idle, rotation)
                    }
                }
            }
        }

        Item {
            anchors.fill: parent
            z: root.viewer.viewerNavigationDirection < 0 ? 1 : -1
            opacity: root.viewer.viewerNavigationTargetOpacity
            visible: opacity > 0
                     && root.viewer.viewerNavigationActive
                     && root.viewer.viewerNavigationTargetIndex !== -1
                     && root.viewer.viewerNavigationTargetSource.toString()
                        !== ""

            Item {
                x: root.viewer.viewerNavigationTargetImageX
                y: root.viewer.viewerNavigationTargetImageY
                width: root.viewer.viewerNavigationTargetDisplayWidth
                height: root.viewer.viewerNavigationTargetDisplayHeight

                Item {
                    x: (parent.width - width) / 2
                    y: (parent.height - height) / 2
                    width: root.viewer.viewerNavigationTargetHasSize
                           ? root.viewer.viewerNavigationTargetDisplayOriginalSize.width
                             * root.viewer.viewerNavigationTargetScale
                           : parent.width
                    height: root.viewer.viewerNavigationTargetHasSize
                            ? root.viewer.viewerNavigationTargetDisplayOriginalSize.height
                              * root.viewer.viewerNavigationTargetScale
                            : parent.height
                    rotation: viewportItem.rotationMode * 90

                    Image {
                        id: neighborImage
                        objectName: "galleryViewerNavigationNeighborImage"
                        anchors.fill: parent
                        source: root.viewer.viewerNavigationTargetSource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        visible: false
                        mipmap:
                            root.viewer.viewerNavigationTargetSourceLevel === 2
                    }

                    ShaderEffect {
                        objectName: "galleryViewerNavigationNeighborShader"
                        anchors.fill: parent

                        property var source: neighborImage
                        property size viewportSize: Qt.size(
                            width * root.viewer.devicePixelRatio,
                            height * root.viewer.devicePixelRatio)
                        property real sharpenAmount:
                            root.viewer.viewerNavigationTargetScale < 1
                            ? 1.5 : 0
                        property bool showCheckerboard:
                            viewportItem.checkerboardEnabled
                            && neighborImage.status === Image.Ready
                        property int checkerboardSize:
                            4 * root.viewer.devicePixelRatio
                        property int borderRadius: 0

                        fragmentShader:
                            "qrc:/ZoinGallery/resources/shader.frag.qsb"
                    }
                }
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: !root.viewer.customContent
                 && root.viewer.currentSourceValue.toString() === ""
        visible: running && root.viewer.transitionProgress > 0.5
    }

    Label {
        anchors.centerIn: parent
        visible: !root.viewer.customContent && root.viewer.session
                 && root.viewer.presentedIndex < 0
        text: qsTr("Unable to load image")
        color: root.viewer.foregroundColor
    }

    ViewerWheelArea {
        id: wheelInput
        anchors.fill: parent
        enabled: !root.viewer.customContent
                 && !root.viewer.completingClose
        z: 3

        onWheelReceived:
            (pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase,
             modifiers, buttons, hasPixelDelta, inverted, source, deviceType,
             nativeMomentum, nativePhase, nativeMomentumPhase) => {
                root.viewer.handleViewerWheel(
                            pixelDeltaX, pixelDeltaY,
                            angleDeltaX, angleDeltaY, phase, modifiers,
                            buttons, hasPixelDelta, inverted, source,
                            deviceType, nativeMomentum, nativePhase,
                            nativeMomentumPhase)
            }
        onWheelForwarded: root.viewer.finishViewerNavigation()
        onZoomWheelReceived: (angleDeltaY, modifiers, buttons) => {
            if (root.viewer.sphericViewerMode && sphereLoader.item) {
                sphereLoader.item.handleZoomWheel(
                            angleDeltaY, modifiers, buttons)
            } else {
                viewportItem.handleZoomWheel(
                            angleDeltaY, modifiers, buttons)
            }
        }
    }
}
