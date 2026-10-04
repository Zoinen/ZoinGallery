pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root
    objectName: "galleryVideoPlaybackSurface"
    property var controller: null
    property var iconSources: ({})
    property var outputSinkController: null
    property url posterSource: ""
    property string videoIdentity: ""
    property bool previewVisible: false
    property var frameRevisionAtIdentityChange: 0
    property real devicePixelRatio: 1.0
    property color foregroundColor: "#f3f4f6"
    property color mutedColor: "#c7c9cc"

    // A frame retained by the sink belongs to this source only if it arrived
    // after the selected video identity changed. This also prevents a late
    // render from the previous source from taking ownership of the viewport.
    readonly property bool hasDecodedFrame:
        playbackFrameSource.hasFrame
        && playbackFrameSource.revision > frameRevisionAtIdentityChange
    readonly property bool displayReady:
        hasDecodedFrame
        || (posterSource.toString() !== ""
            && posterImage.status === Image.Ready)
        || (controller && (controller.state === "failed"
                           || controller.state === "unavailable"))
    readonly property bool playbackFailed:
        controller && (controller.state === "failed"
                       || controller.state === "unavailable")
    readonly property var presentedFrameSource:
        hasDecodedFrame ? playbackFrameSource : null
    readonly property var posterImageSource: posterImage
    readonly property size displaySize: hasDecodedFrame
        ? playbackFrameSource.frameSize
        : (posterImage.status === Image.Ready
           ? posterImage.sourceSize : Qt.size(0, 0))

    function snap(value) {
        const dpr = Math.max(0.5, devicePixelRatio)
        return Math.round(value * dpr) / dpr
    }
    function alignedOrigin(parentItem, x, y) {
        const dpr = Math.max(0.5, devicePixelRatio)
        const scenePoint = parentItem.mapToItem(null, x, y)
        const snappedPoint = Qt.point(Math.round(scenePoint.x * dpr) / dpr,
                                      Math.round(scenePoint.y * dpr) / dpr)
        return parentItem.mapFromItem(null, snappedPoint)
    }
    function formatTime(value) {
        const seconds = Math.floor(Math.max(0, value) / 1000)
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor((seconds % 3600) / 60)
        const remainder = seconds % 60
        const two = number => String(number).padStart(2, "0")
        return hours > 0 ? hours + ":" + two(minutes) + ":" + two(remainder)
                          : two(minutes) + ":" + two(remainder)
    }
    function seekAt(mouseX, width) {
        if (!controller || controller.duration <= 0 || width <= 0)
            return
        controller.seekTo(Math.round(Math.max(0, Math.min(1, mouseX / width))
                                     * controller.duration))
    }
    function setVolumeAt(mouseX, width) {
        if (!controller || width <= 0)
            return
        const next = Math.max(0, Math.min(1, mouseX / width))
        controller.adjustVolume(next - controller.volume)
    }
    function attachOutputSink() {
        if (outputSinkController && outputSinkController !== controller
                && typeof outputSinkController.setOutputSink === "function")
            outputSinkController.setOutputSink(null)
        outputSinkController = null
        if (controller && typeof controller.setOutputSink === "function")
        {
            controller.setOutputSink(playbackFrameSource.sink)
            outputSinkController = controller
        }
    }
    onControllerChanged: attachOutputSink()
    onVideoIdentityChanged:
        frameRevisionAtIdentityChange = playbackFrameSource.revision
    Component.onCompleted: {
        frameRevisionAtIdentityChange = playbackFrameSource.revision
        attachOutputSink()
    }
    Component.onDestruction: {
        if (outputSinkController
                && typeof outputSinkController.setOutputSink === "function")
            outputSinkController.setOutputSink(null)
    }

    ViewerVideoFrameSource {
        id: playbackFrameSource
        objectName: "galleryVideoFrameSource"
    }

    Image {
        id: posterImage
        objectName: "galleryVideoPosterImage"
        anchors.fill: parent
        source: root.posterSource
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
        // This is a texture source for the shared ViewerImageLayer shader.
        // It never draws into the overlay itself.
        visible: false
    }

    Rectangle {
        id: controlsBar
        objectName: "galleryVideoControlsBar"
        z: 2
        readonly property point alignedPosition:
            root.alignedOrigin(root, 0, root.height - height)
        x: alignedPosition.x
        y: alignedPosition.y
        width: root.snap(root.width)
        height: root.snap(58)
        color: "#dd101114"

        Rectangle {
            id: playButton
            objectName: "galleryVideoPlayButton"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(10), root.snap(11))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(36)
            height: root.snap(36)
            radius: root.snap(5)
            color: playTap.pressed ? "#48515b" : "#30343a"
            Accessible.role: Accessible.Button
            Accessible.name: root.controller && root.controller.playing
                             ? qsTr("Pause") : qsTr("Play")

            TapHandler {
                id: playTap
                objectName: "galleryVideoPlayTap"
                onTapped: root.controller.playPause()
            }

            Image {
                id: playButtonIcon
                objectName: "galleryVideoPlayButtonIcon"
                readonly property point alignedPosition:
                    root.alignedOrigin(playButton,
                        (playButton.width - width) / 2,
                        (playButton.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(18)
                height: root.snap(18)
                source: root.iconSources
                        ? (root.controller && root.controller.playing
                           ? root.iconSources.pause : root.iconSources.play)
                        : ""
                fillMode: Image.PreserveAspectFit
                smooth: false
                mipmap: false
            }
        }

        Item {
            id: seekControl
            objectName: "galleryVideoSeekControl"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(56), root.snap(2))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(Math.max(48, root.width - 192))
            height: root.snap(27)
            enabled: root.controller && root.controller.duration > 0

            Rectangle {
                id: seekTrack
                objectName: "galleryVideoSeekTrack"
                readonly property point alignedPosition:
                    root.alignedOrigin(seekControl, 0, (seekControl.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: seekControl.width
                height: root.snap(4)
                radius: root.snap(2)
                color: "#777b80"

                Rectangle {
                    id: seekProgress
                    objectName: "galleryVideoSeekProgress"
                    width: root.snap(seekTrack.width
                        * (root.controller && root.controller.duration > 0
                           ? root.controller.position / root.controller.duration : 0))
                    height: parent.height
                    radius: parent.radius
                    color: "#75b9ff"
                }
            }

            Rectangle {
                id: seekHandle
                objectName: "galleryVideoSeekHandle"
                readonly property point alignedPosition:
                    root.alignedOrigin(seekControl,
                        seekTrack.width * (root.controller && root.controller.duration > 0
                            ? root.controller.position / root.controller.duration : 0)
                            - width / 2,
                        (seekControl.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(10)
                height: root.snap(10)
                radius: width / 2
                color: seekArea.pressed ? "#ffffff" : "#d4e9ff"
            }

            MouseArea {
                id: seekArea
                objectName: "galleryVideoSeekMouseArea"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                onPressed: mouse => root.seekAt(mouse.x, width)
                onPositionChanged: mouse => {
                    if (pressed)
                        root.seekAt(mouse.x, width)
                }
            }
        }

        Text {
            id: timeText
            objectName: "galleryVideoTimeText"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, x, y)
            x: root.snap(56)
            y: root.snap(32)
            transform: Translate {
                x: timeText.alignedPosition.x - timeText.x
                y: timeText.alignedPosition.y - timeText.y
            }
            width: root.snap(Math.max(48, root.width - 192))
            height: root.snap(18)
            text: root.formatTime(root.controller ? root.controller.position : 0)
                  + " / "
                  + root.formatTime(root.controller ? root.controller.duration : 0)
            color: root.mutedColor
            font.pixelSize: root.snap(11)
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Rectangle {
            id: muteButton
            objectName: "galleryVideoMuteButton"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(root.width - 130), root.snap(11))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(36)
            height: root.snap(36)
            radius: root.snap(5)
            color: muteTap.pressed ? "#48515b" : "#30343a"
            Accessible.role: Accessible.Button
            Accessible.name: root.controller && root.controller.muted
                             ? qsTr("Unmute") : qsTr("Mute")

            TapHandler {
                id: muteTap
                objectName: "galleryVideoMuteTap"
                onTapped: root.controller.toggleMute()
            }

            Image {
                id: muteButtonIcon
                objectName: "galleryVideoMuteButtonIcon"
                readonly property point alignedPosition:
                    root.alignedOrigin(muteButton,
                        (muteButton.width - width) / 2,
                        (muteButton.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(18)
                height: root.snap(18)
                source: root.iconSources
                        ? (root.controller && root.controller.muted
                           ? root.iconSources.muted : root.iconSources.sound)
                        : ""
                fillMode: Image.PreserveAspectFit
                smooth: false
                mipmap: false
            }
        }

        Item {
            id: volumeControl
            objectName: "galleryVideoVolumeControl"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(root.width - 88), root.snap(7))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(78)
            height: root.snap(30)

            Rectangle {
                id: volumeTrack
                objectName: "galleryVideoVolumeTrack"
                readonly property point alignedPosition:
                    root.alignedOrigin(volumeControl, 0,
                        (volumeControl.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: volumeControl.width
                height: root.snap(4)
                radius: root.snap(2)
                color: "#777b80"

                Rectangle {
                    objectName: "galleryVideoVolumeProgress"
                    width: root.snap(parent.width
                        * (root.controller ? root.controller.volume : 1))
                    height: parent.height
                    radius: parent.radius
                    color: "#75b9ff"
                }
            }

            Rectangle {
                id: volumeHandle
                objectName: "galleryVideoVolumeHandle"
                readonly property point alignedPosition:
                    root.alignedOrigin(volumeControl,
                        volumeTrack.width * (root.controller ? root.controller.volume : 1)
                            - width / 2,
                        (volumeControl.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(10)
                height: root.snap(10)
                radius: width / 2
                color: volumeArea.pressed ? "#ffffff" : "#d4e9ff"
            }

            MouseArea {
                id: volumeArea
                objectName: "galleryVideoVolumeMouseArea"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                onPressed: mouse => root.setVolumeAt(mouse.x, width)
                onPositionChanged: mouse => {
                    if (pressed)
                        root.setVolumeAt(mouse.x, width)
                }
            }
        }
    }

    Text {
        id: statusText
        objectName: "galleryVideoStatusText"
        readonly property point alignedPosition:
            root.alignedOrigin(root,
                (root.width - width) / 2,
                (root.height - controlsBar.height - height) / 2)
        x: alignedPosition.x
        y: alignedPosition.y
        width: root.snap(Math.max(0, root.width - 32))
        height: root.snap(30)
        visible: root.controller
                 && ((root.controller.state === "loading"
                      && !root.previewVisible)
                     || root.controller.state === "failed"
                     || root.controller.state === "unavailable")
        text: root.controller && root.controller.state === "failed"
              ? root.controller.error
              : root.controller && root.controller.state === "unavailable"
                ? qsTr("Video playback is unavailable")
                : qsTr("Loading video…")
        color: root.foregroundColor
        font.pixelSize: root.snap(14)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
}
