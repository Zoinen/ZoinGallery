pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

Item {
    id: root

    required property Item viewer

    function alignedCenter(item) {
        // Read the complete ancestor chain so host movement and resize both
        // invalidate the scene-space correction, including fractional DPR.
        let dependency = 0
        for (let ancestor = root; ancestor; ancestor = ancestor.parent)
            dependency += ancestor.x + ancestor.y + ancestor.width + ancestor.height
        const dpr = Math.max(0.5, root.viewer.devicePixelRatio)
        const point = root.mapToItem(null, (root.width - item.width) / 2 + dependency * 0,
                                     (root.height - item.height) / 2)
        return root.mapFromItem(null, Math.round(point.x * dpr) / dpr,
                                Math.round(point.y * dpr) / dpr)
    }

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
                 && !(root.viewer.currentViewerRequestState === "failed"
                      && root.viewer.currentSourceValue.toString() === "")
        clip: true
        anchors.fill: parent
        opacity: root.viewer.transitionHasGeometry
                 ? 1 : root.viewer.transitionProgress

        Item {
            anchors.fill: parent
            opacity: root.viewer.viewerNavigationCurrentOpacity
            transform: Translate {
                id: navigationTranslation
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
                pixelAlignmentRevision: navigationTranslation.x
                externalTransformMoving:
                    root.viewer.viewerNavigationActive
                    || root.viewer.viewerNavigationAnimationRunning
                    || root.viewer.viewerNavigationCommitAfterAnimation
                sphericTextureMipmapsEnabled: root.viewer.sphericViewerMode
                topInset: 0
                checkerboardEnabled: true
                scrollBarTheme: root.viewer.theme
                pinchZoomEnabled: !root.viewer.sphericViewerMode
                hideVerticalScrollBar:
                    root.viewer.viewerNavigationActive
                    || root.viewer.viewerNavigationAnimationRunning
                    || root.viewer.viewerNavigationCommitAfterAnimation
                    || Math.abs(root.viewer.viewerNavigationOffsetX) > 0.1

                onZoomScaleChanged: root.viewer.scheduleDecodeRequest()
                onCloseRequested: root.viewer.handleViewportDoubleClick()
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

                        onCloseRequested: root.viewer.handleViewportDoubleClick()
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
                    readonly property point alignedOrigin:
                        viewportItem.image.alignedPosition(parent,
                            (parent.width - width) / 2,
                            (parent.height - height) / 2,
                            width, height, rotation)
                    x: alignedOrigin.x
                    y: alignedOrigin.y
                    width: viewportItem.image.snapExtent(
                           root.viewer.viewerNavigationTargetHasSize
                           ? root.viewer.viewerNavigationTargetDisplayOriginalSize.width
                             * root.viewer.viewerNavigationTargetScale
                           : parent.width)
                    height: viewportItem.image.snapExtent(
                            root.viewer.viewerNavigationTargetHasSize
                            ? root.viewer.viewerNavigationTargetDisplayOriginalSize.height
                              * root.viewer.viewerNavigationTargetScale
                            : parent.height)
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
                        mipmap: false
                    }

                    ViewerResample {
                        objectName: "galleryViewerNavigationNeighborShader"
                        anchors.fill: parent

                        imageSource: neighborImage
                        viewportSize: Qt.size(
                            width * root.viewer.devicePixelRatio,
                            height * root.viewer.devicePixelRatio)
                        showCheckerboard:
                            viewportItem.checkerboardEnabled
                            && neighborImage.status === Image.Ready
                        checkerboardSize:
                            4 * root.viewer.devicePixelRatio
                        borderRadius: 0
                    }
                }
            }
        }
    }

    BusyIndicator {
        objectName: "galleryViewerBusyIndicator"
        anchors.centerIn: parent
        running: !root.viewer.customContent
                 && root.viewer.visible
                 && root.viewer.viewerContentVisible
                 && root.viewer.transitionProgress > 0.5
                 && root.viewer.session
                 && root.viewer.presentedIndex >= 0
                 && root.viewer.currentViewerRequestState === "pending"
                 && root.viewer.currentSourceValue.toString() === ""
        visible: running
    }

    Label {
        id: loadFailure
        objectName: "galleryViewerLoadFailure"
        readonly property point alignedPosition: root.alignedCenter(loadFailure)
        x: alignedPosition.x
        y: alignedPosition.y
        visible: !root.viewer.customContent && root.viewer.session
                 && (root.viewer.presentedIndex < 0
                     || (root.viewer.currentViewerRequestState === "failed"
                         && root.viewer.currentSourceValue.toString() === ""))
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
