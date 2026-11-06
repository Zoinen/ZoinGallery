pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.PixelGrid 1.0

PixelGridScope {
    id: root
    objectName: "galleryVideoPlaybackSurface"
    property var controller: null
    property var iconSources: ({})
    property var outputSinkController: null
    property url posterSource: ""
    property string videoIdentity: ""
    property var frameRevisionAtIdentityChange: 0
    property real devicePixelRatio: 1.0
    property real pixelAlignmentRevision: 0
    property color foregroundColor: "#f3f4f6"
    property color mutedColor: "#c7c9cc"
    property var sharedFrameSource: null
    property bool managesFrameSource: true
    property var externalHdrOutput: null
    property bool linearOutput: false
    property int controlsAutoHideDelay: 3000
    property bool controlsShown: true
    readonly property bool controlsAnimating: controlsFade.running
    readonly property bool controlsInteracting: seekArea.pressed || volumeArea.pressed
        || playTap.pressed || muteTap.pressed || loopTap.pressed || hdrTap.pressed
    readonly property bool controlsAutoHideArmed: visible && Window.active
        && pointerActivity.hovered && !controlsHover.hovered && !controlsInteracting
        && !(hdrController && (hdrController.busy || (managesFrameSource && hdrController.active)))
    readonly property var frameSource: sharedFrameSource || playbackFrameSource
    readonly property var hdrController: externalHdrOutput || hdrOutputLoader.item
    readonly property bool hdrButtonVisible: hasDecodedFrame && frameSource.hdr
        && hdrController !== null && hdrController.supported
    readonly property real hdrControlWidth: hdrButtonVisible ? snap(46) : 0
    signal navigationRequested(int direction)
    function uiColor(value) {
        return linearOutput && hdrController ? hdrController.linearColor(value) : value
    }
    function revealControls() {
        controlsShown = true
        if (controlsAutoHideArmed)
            controlsIdleTimer.restart()
        else
            controlsIdleTimer.stop()
    }
    onControlsAutoHideArmedChanged: revealControls()
    onControlsAutoHideDelayChanged: revealControls()

    // A frame retained by the sink belongs to this source only if it arrived
    // after the selected video identity changed. This also prevents a late
    // render from the previous source from taking ownership of the viewport.
    readonly property bool hasDecodedFrame:
        frameSource.hasFrame
        && (!managesFrameSource || frameSource.revision > frameRevisionAtIdentityChange)
    readonly property bool displayReady:
        hasDecodedFrame
        || (posterSource.toString() !== ""
            && posterImage.status === Image.Ready)
        || (controller && (controller.state === "failed"
                           || controller.state === "unavailable"))
    readonly property bool playbackFailed:
        controller && (controller.state === "failed"
                       || controller.state === "unavailable")
    readonly property bool playbackPending:
        controller !== null && (controller.state === "loading"
                || (controller.playing && !hasDecodedFrame))
    readonly property var presentedFrameSource:
        hasDecodedFrame && !(managesFrameSource && hdrController && hdrController.active)
            ? frameSource : null
    readonly property var posterImageSource: posterImage
    readonly property size displaySize: hasDecodedFrame
        ? frameSource.frameSize
        : (posterImage.status === Image.Ready
           ? posterImage.sourceSize : Qt.size(0, 0))

    function snap(value) {
        const dpr = Math.max(0.5, devicePixelRatio)
        return Math.round(value * dpr) / dpr
    }
    function alignedOrigin(parentItem, x, y) {
        return Qt.point(snap(x), snap(y))
    }
    onPixelAlignmentRevisionChanged: invalidate()
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
        if (!managesFrameSource) return
        if (outputSinkController !== controller) {
            setFrameTarget(outputSinkController, null)
            outputSinkController = controller
        }
        setFrameTarget(controller, playbackFrameSource)
    }
    function setFrameTarget(target, frameSource) {
        if (!target)
            return
        if (typeof target.setFrameSource === "function")
            target.setFrameSource(frameSource)
        else if (typeof target.setOutputSink === "function")
            target.setOutputSink(frameSource ? frameSource.sink : null)
    }
    onControllerChanged: attachOutputSink()
    onVideoIdentityChanged: {
        frameRevisionAtIdentityChange = playbackFrameSource.revision
        if (managesFrameSource && hdrController) hdrController.close()
        revealControls()
    }
    onVisibleChanged: {
        if (!visible && managesFrameSource && hdrController) hdrController.close()
        revealControls()
    }
    Component.onCompleted: {
        frameRevisionAtIdentityChange = playbackFrameSource.revision
        attachOutputSink()
        revealControls()
    }
    Component.onDestruction: {
        if (managesFrameSource) setFrameTarget(outputSinkController, null)
    }

    Loader {
        id: hdrOutputLoader
        active: root.managesFrameSource && root.hasDecodedFrame && root.frameSource.hdr
        sourceComponent: Component {
            HdrVideoOutput {
                surface: root
                onNavigationRequested: direction => root.navigationRequested(direction)
            }
        }
    }

    ViewerVideoFrameSource {
        id: playbackFrameSource
        objectName: "galleryVideoFrameSource"
    }

    HoverHandler {
        id: pointerActivity
        objectName: "galleryVideoPointerActivity"
        property point lastPosition: Qt.point(-1, -1)
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        blocking: false
        onPointChanged: {
            // Visibility changes can resend hover points without mouse motion.
            const position = point.scenePosition
            if (hovered && (position.x !== lastPosition.x || position.y !== lastPosition.y))
                root.revealControls()
            lastPosition = position
        }
    }

    Timer {
        id: controlsIdleTimer
        objectName: "galleryVideoControlsIdleTimer"
        interval: Math.max(1, root.controlsAutoHideDelay)
        onTriggered: if (root.controlsAutoHideArmed) root.controlsShown = false
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
        opacity: root.controlsShown ? 1 : 0
        visible: root.controlsShown || opacity > 0
        enabled: root.controlsShown
        Behavior on opacity {
            OpacityAnimator {
                id: controlsFade
                objectName: "galleryVideoControlsFade"
                duration: 180
                easing.type: Easing.OutSine
            }
        }
        readonly property point alignedPosition:
            root.alignedOrigin(root, 0, root.height - height)
        x: alignedPosition.x
        y: alignedPosition.y
        width: root.snap(root.width)
        height: root.snap(58)
        color: root.uiColor("#dd101114")

        HoverHandler {
            id: controlsHover
            objectName: "galleryVideoControlsHover"
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: false
        }

        function centeredY(itemHeight) {
            return root.snap((height - itemHeight) / 2)
        }

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
            color: root.uiColor(playTap.pressed ? "#48515b" : "#30343a")
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
                root.alignedOrigin(controlsBar, root.snap(56), controlsBar.centeredY(height))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(Math.max(48, root.width - 234 - root.hdrControlWidth))
            height: controlsBar.height
            enabled: root.controller && root.controller.duration > 0

            Rectangle {
                id: seekTrack
                objectName: "galleryVideoSeekTrack"
                readonly property point alignedPosition:
                    root.alignedOrigin(seekControl, 0, controlsBar.centeredY(height) - seekControl.y)
                x: alignedPosition.x
                y: alignedPosition.y
                width: seekControl.width
                height: root.snap(4)
                radius: root.snap(2)
                color: root.uiColor("#777b80")

                Rectangle {
                    id: seekProgress
                    objectName: "galleryVideoSeekProgress"
                    width: root.snap(seekTrack.width
                        * (root.controller && root.controller.duration > 0
                           ? root.controller.position / root.controller.duration : 0))
                    height: parent.height
                    radius: parent.radius
                    color: root.uiColor("#75b9ff")
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
                        controlsBar.centeredY(height) - seekControl.y)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(10)
                height: root.snap(10)
                radius: width / 2
                color: root.uiColor(seekArea.pressed ? "#ffffff" : "#d4e9ff")
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
            y: controlsBar.height - height - root.snap(3)
            transform: Translate {
                x: timeText.alignedPosition.x - timeText.x
                y: timeText.alignedPosition.y - timeText.y
            }
            width: root.snap(Math.max(48, root.width - 234 - root.hdrControlWidth))
            height: root.snap(18)
            text: root.formatTime(root.controller ? root.controller.position : 0)
                  + " / "
                  + root.formatTime(root.controller ? root.controller.duration : 0)
            color: root.uiColor(root.mutedColor)
            font.pixelSize: root.snap(11)
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Rectangle {
            id: hdrButton
            objectName: "galleryVideoHdrButton"
            visible: root.hdrButtonVisible
            x: root.snap(root.width - 46)
            y: root.snap(11)
            width: root.snap(36)
            height: root.snap(36)
            radius: root.snap(5)
            color: root.uiColor(hdrTap.pressed ? "#48515b"
                : root.hdrController && root.hdrController.active ? "#285e87" : "#30343a")
            Accessible.role: Accessible.CheckBox
            Accessible.name: root.hdrController && root.hdrController.active
                ? qsTr("Disable HDR") : qsTr("Enable HDR in fullscreen on %1")
                    .arg(root.hdrController ? root.hdrController.displayName : "")
            Accessible.checked: root.hdrController ? root.hdrController.active : false
            TapHandler {
                id: hdrTap
                enabled: root.hdrController !== null && !root.hdrController.busy
                onTapped: root.hdrController.toggle()
            }
            Text {
                objectName: "galleryVideoHdrButtonText"
                x: root.snap((hdrButton.width - width) / 2)
                y: root.snap((hdrButton.height - height) / 2)
                width: root.snap(32)
                height: root.snap(18)
                text: "HDR"
                color: root.uiColor(root.foregroundColor)
                font.pixelSize: root.snap(11)
                font.bold: true
                renderType: Text.NativeRendering
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            id: loopButton
            objectName: "galleryVideoLoopButton"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(root.width - 46 - root.hdrControlWidth), root.snap(11))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(36)
            height: root.snap(36)
            radius: root.snap(5)
            color: root.uiColor(loopTap.pressed ? "#48515b"
                   : root.controller && root.controller.looping ? "#285e87" : "#30343a")
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Loop video")
            Accessible.checked: root.controller ? root.controller.looping : false

            TapHandler {
                id: loopTap
                objectName: "galleryVideoLoopTap"
                onTapped: root.controller.toggleLoop()
            }

            Image {
                objectName: "galleryVideoLoopButtonIcon"
                readonly property point alignedPosition:
                    root.alignedOrigin(loopButton,
                        (loopButton.width - width) / 2,
                        (loopButton.height - height) / 2)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(18)
                height: root.snap(18)
                source: root.iconSources && root.iconSources.loop ? root.iconSources.loop : ""
                fillMode: Image.PreserveAspectFit
                smooth: false
                mipmap: false
            }
        }

        Rectangle {
            id: muteButton
            objectName: "galleryVideoMuteButton"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(root.width - 172 - root.hdrControlWidth), root.snap(11))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(36)
            height: root.snap(36)
            radius: root.snap(5)
            color: root.uiColor(muteTap.pressed ? "#48515b" : "#30343a")
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
            readonly property bool muted: root.controller !== null && root.controller.muted
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(root.width - 130 - root.hdrControlWidth),
                                   controlsBar.centeredY(height))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(78)
            height: controlsBar.height

            Rectangle {
                id: volumeTrack
                objectName: "galleryVideoVolumeTrack"
                readonly property point alignedPosition:
                    root.alignedOrigin(volumeControl, 0,
                        controlsBar.centeredY(height) - volumeControl.y)
                x: alignedPosition.x
                y: alignedPosition.y
                width: volumeControl.width
                height: root.snap(4)
                radius: root.snap(2)
                color: root.uiColor("#777b80")

                Rectangle {
                    objectName: "galleryVideoVolumeProgress"
                    width: root.snap(parent.width
                        * (root.controller ? root.controller.volume : 1))
                    height: parent.height
                    radius: parent.radius
                    color: root.uiColor(volumeControl.muted ? "#a1a5aa" : "#75b9ff")
                }
            }

            Rectangle {
                id: volumeHandle
                objectName: "galleryVideoVolumeHandle"
                readonly property point alignedPosition:
                    root.alignedOrigin(volumeControl,
                        volumeTrack.width * (root.controller ? root.controller.volume : 1)
                            - width / 2,
                        controlsBar.centeredY(height) - volumeControl.y)
                x: alignedPosition.x
                y: alignedPosition.y
                width: root.snap(10)
                height: root.snap(10)
                radius: width / 2
                color: root.uiColor(volumeControl.muted
                       ? (volumeArea.pressed ? "#d5d7da" : "#b7bbc0")
                       : (volumeArea.pressed ? "#ffffff" : "#d4e9ff"))
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
        visible: root.playbackFailed || (root.hdrController && (root.hdrController.busy || root.hdrController.error !== ""))
        text: root.hdrController && root.hdrController.error !== "" ? root.hdrController.error
              : root.hdrController && root.hdrController.busy ? qsTr("Switching HDR display mode…")
              : root.controller && root.controller.state === "failed"
              ? root.controller.error
              : qsTr("Video playback is unavailable")
        color: root.uiColor(root.foregroundColor)
        font.pixelSize: root.snap(14)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
}
