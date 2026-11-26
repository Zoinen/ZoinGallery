pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import ZoinGallery.PixelGrid 1.0
import "VideoSubtitle.mjs" as VideoSubtitle

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
    property bool viewerHasFocus: false
    property int controlsAutoHideDelay: 3000
    property bool controlsShown: true
    readonly property bool controlsAnimating: controlsFade.running
    readonly property bool controlsInteracting: seekArea.pressed || volumeArea.pressed
        || playTap.pressed || muteTap.pressed || loopTap.pressed || hdrTap.pressed
        || resumeButton.down || audioButton.down || subtitleButton.down || keyboardButton.down
        || trackMenu.visible
    readonly property bool controlsAutoHideArmed: visible && Window.active
        && pointerActivity.hovered && !controlsHover.hovered && !seekControl.activeFocus && !controlsInteracting
        && !(hdrController && (hdrController.busy || (managesFrameSource && hdrController.active)))
    readonly property var frameSource: sharedFrameSource || playbackFrameSource
    readonly property var hdrController: externalHdrOutput || hdrOutputLoader.item
    readonly property bool hdrButtonVisible: hasDecodedFrame && frameSource.hdr
        && hdrController !== null && hdrController.supported
    readonly property real hdrControlWidth: hdrButtonVisible ? snap(46) : 0
    readonly property var audioTracks: controller && controller.audioTracks ? controller.audioTracks : []
    readonly property var subtitleTracks: controller && controller.subtitleTracks
        ? controller.subtitleTracks : []
    readonly property real audioControlWidth: audioTracks.length > 1 ? audioButton.width + snap(10) : 0
    readonly property real subtitleControlWidth: subtitleTracks.length > 0 ? subtitleButton.width + snap(10) : 0
    readonly property real trackControlWidth: audioControlWidth + subtitleControlWidth
    readonly property real keyboardControlWidth: keyboardButton.width + snap(10)
    readonly property real secondaryControlWidth: trackControlWidth + keyboardControlWidth
    readonly property bool keyboardShortcutsEnabled: viewerHasFocus && visible && controller && !trackMenu.visible
    readonly property bool resumeAvailable: controller && controller.resumePosition > 0
        && controller.resumePosition < controller.duration && controller.state === "ready"
    readonly property bool separateResumeRow: resumeAvailable && width < 300 + resumeButton.width
    readonly property real toolsWidth: width - (resumeAvailable && !separateResumeRow
        ? resumeButton.width + snap(10) : 0)
    readonly property bool stackTrackControls: toolsWidth < 218 + secondaryControlWidth + hdrControlWidth
    readonly property bool separateKeyboardRow: stackTrackControls
        && toolsWidth < secondaryControlWidth + hdrControlWidth + snap(54)
    readonly property real inlineTrackWidth: stackTrackControls ? 0 : secondaryControlWidth
    readonly property real inlineHdrWidth: stackTrackControls ? 0 : hdrControlWidth
    readonly property bool showVolumeSlider: toolsWidth >= 300 + secondaryControlWidth + hdrControlWidth
    readonly property real rightControlsOffset: showVolumeSlider ? 0 : snap(84)
    readonly property real controlsRowHeight: snap(58)
    readonly property real trackRowHeight: stackTrackControls ? snap(44) * (separateKeyboardRow ? 2 : 1) : 0
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
    function toggleArrowKeys() {
        controller.arrowKeysSeek = !controller.arrowKeysSeek
        revealControls()
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
    onControllerChanged: {
        trackMenu.close()
        attachOutputSink()
    }
    onVideoIdentityChanged: {
        trackMenu.close()
        frameRevisionAtIdentityChange = playbackFrameSource.revision
        if (managesFrameSource && hdrController) hdrController.close()
        revealControls()
    }
    onVisibleChanged: {
        if (!visible) trackMenu.close()
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

    Text {
        id: subtitles
        objectName: "galleryVideoSubtitleText"
        readonly property var cue: VideoSubtitle.format(root.controller?.subtitleText ?? "", root.uiColor)
        readonly property real lastY: Math.max(0, root.height - height)
        readonly property real authoredY: cue.alignment >= 7 ? 12 : cue.alignment >= 4
                                          ? (root.height - height) / 2 : controlsBar.y - height - 12
        readonly property int offsetPercent: root.controller?.subtitleOffsetPercent ?? 0
        function shift(direction) {
            if (!root.controller || root.controller.activeSubtitleTrack < 0 || root.height <= 0)
                return
            let from = offsetPercent
            if (text.length) {
                const first = Math.floor(-authoredY * 100 / root.height)
                const last = Math.ceil((lastY - authoredY) * 100 / root.height)
                if ((direction < 0 && from <= first) || (direction > 0 && from >= last))
                    return
                from = Math.max(first, Math.min(last, from))
            }
            root.controller.subtitleOffsetPercent = from + direction
        }
        z: 1
        x: root.snap((root.width - width) / 2)
        y: root.snap(Math.max(0, Math.min(lastY, authoredY + root.height * offsetPercent / 100)))
        width: root.snap(Math.max(0, root.width - 32))
        height: root.snap(implicitHeight)
        text: cue.text
        visible: root.hasDecodedFrame && text !== "" && height <= root.height
        textFormat: Text.StyledText
        horizontalAlignment: [Text.AlignLeft, Text.AlignHCenter, Text.AlignRight][(cue.alignment - 1) % 3]
        wrapMode: Text.Wrap
        maximumLineCount: 4
        elide: Text.ElideRight
        color: root.uiColor("#ffffff")
        style: Text.Outline
        styleColor: root.uiColor("#000000")
        font.pixelSize: root.snap(Math.max(14, Math.min(30, root.width * 0.035)))
        // Use the viewer's usual weight unless the cue specifies its own.
        font.bold: !cue.explicitWeight
        renderType: Text.NativeRendering
        transform: Translate {
            x: subtitles.PixelGridAlignment.offsetX
            y: subtitles.PixelGridAlignment.offsetY
        }
    }

    // Qt maps Ctrl in a portable shortcut to the Command key on macOS.
    Shortcut {
        sequence: "/"
        enabled: root.keyboardShortcutsEnabled
        autoRepeat: false
        onActivated: root.toggleArrowKeys()
    }
    Shortcut {
        sequence: "Left"
        enabled: root.keyboardShortcutsEnabled && keyboardButton.checked
        onActivated: root.controller.seekBy(-5000)
    }
    Shortcut {
        sequence: "Right"
        enabled: root.keyboardShortcutsEnabled && keyboardButton.checked
        onActivated: root.controller.seekBy(5000)
    }
    Shortcut {
        sequence: "Ctrl+Shift+Up"
        enabled: root.viewerHasFocus && root.visible && !trackMenu.visible
        onActivated: subtitles.shift(-1)
    }
    Shortcut {
        sequence: "Ctrl+Shift+Down"
        enabled: root.viewerHasFocus && root.visible && !trackMenu.visible
        onActivated: subtitles.shift(1)
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
        height: root.controlsRowHeight + root.trackRowHeight + (root.separateResumeRow ? root.snap(46) : 0)
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: "#dc000000" }
        }
        readonly property real buttonY: root.trackRowHeight + root.snap(11)

        HoverHandler {
            id: controlsHover
            objectName: "galleryVideoControlsHover"
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: false
        }

        function centeredY(itemHeight) {
            return root.trackRowHeight + root.snap((root.controlsRowHeight - itemHeight) / 2)
        }

        Rectangle {
            id: playButton
            objectName: "galleryVideoPlayButton"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(10), controlsBar.buttonY)
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
            readonly property bool retainsFocusOnShellRefresh: true
            activeFocusOnTab: true
            Keys.onShortcutOverride: event => {
                if (isSeekArrow(event)) event.accepted = true
            }
            function isSeekArrow(event) {
                return !(event.modifiers & ~Qt.KeypadModifier)
                    && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
            }
            Keys.onReleased: event => { if (isSeekArrow(event)) event.accepted = true }
            Keys.onPressed: event => {
                if (!isSeekArrow(event)) return
                root.revealControls()
                root.controller.seekTo(Math.max(0, Math.min(root.controller.duration,
                    root.controller.position + (event.key === Qt.Key_Left ? -5000 : 5000))))
                event.accepted = true
            }
            objectName: "galleryVideoSeekControl"
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(56), controlsBar.centeredY(height))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(Math.max(0,
                root.toolsWidth - 234 - root.inlineHdrWidth - root.inlineTrackWidth + root.rightControlsOffset))
            height: root.controlsRowHeight
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
                width: root.snap(seekControl.activeFocus ? 14 : 10)
                height: width
                radius: width / 2
                color: root.uiColor(seekControl.activeFocus ? "#ffffff" : "#d4e9ff")
            }

            MouseArea {
                id: seekArea
                objectName: "galleryVideoSeekMouseArea"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                onPressed: mouse => {
                    seekControl.forceActiveFocus(Qt.MouseFocusReason)
                    root.seekAt(mouse.x, width)
                }
                onReleased: seekControl.forceActiveFocus(Qt.MouseFocusReason)
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
            x: root.snap(root.stackTrackControls ? 10 : 56)
            y: root.stackTrackControls ? root.snap(keyboardButton.y + (keyboardButton.height - height) / 2)
                                      : root.trackRowHeight + root.controlsRowHeight - height - root.snap(3)
            transform: Translate {
                x: timeText.alignedPosition.x - timeText.x
                y: timeText.alignedPosition.y - timeText.y
            }
            width: root.stackTrackControls
                ? Math.max(0, keyboardButton.x - x - root.snap(6))
                : seekControl.width
            height: root.snap(18)
            readonly property string elapsed: root.formatTime(root.controller ? root.controller.position : 0)
            text: root.stackTrackControls && width < root.snap(100) ? elapsed
                : elapsed + " / " + root.formatTime(root.controller ? root.controller.duration : 0)
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
            x: root.snap(root.toolsWidth - 46)
            y: root.stackTrackControls ? root.snap(7) : controlsBar.buttonY
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
                root.alignedOrigin(controlsBar, root.snap(root.toolsWidth - 46 - root.inlineHdrWidth),
                                   controlsBar.buttonY)
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
                root.alignedOrigin(controlsBar, root.snap(root.toolsWidth - 172 - root.inlineHdrWidth
                                                         + root.rightControlsOffset), controlsBar.buttonY)
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

        ToolbarButton {
            id: keyboardButton
            objectName: "galleryVideoKeyboardButton"
            imageSource: root.iconSources.keyboard || ""
            caption: root.iconSources.keyboard ? "" : "/"
            width: root.snap(36)
            x: root.separateKeyboardRow ? root.snap(root.toolsWidth) - root.keyboardControlWidth
                : subtitleButton.x - root.audioControlWidth - root.keyboardControlWidth
            y: root.separateKeyboardRow ? root.snap(44) + root.snap(7)
                : root.stackTrackControls ? root.snap(7) : controlsBar.buttonY
            enabled: root.controller !== null
            checkable: true
            checked: Boolean(root.controller?.arrowKeysSeek)
            Accessible.name: qsTr("Seek with Left and Right arrows")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Seek with Left and Right arrows (/)")
            onToggled: {
                root.controller.arrowKeysSeek = checked
                root.revealControls()
            }
        }

        ToolbarButton {
            id: audioButton
            objectName: "galleryVideoAudioTrackButton"
            visible: root.audioControlWidth > 0
            caption: root.controller && root.controller.activeAudioTrack >= 0 ? (root.controller.audioLanguage ?? "") : ""
            x: subtitleButton.x - root.audioControlWidth
            imageSource: root.iconSources.audio || ""
            Accessible.name: qsTr("Audio tracks")
            onClicked: trackMenu.showTracks(audioButton, false)
        }

        ToolbarButton {
            id: subtitleButton
            objectName: "galleryVideoSubtitleTrackButton"
            visible: root.subtitleControlWidth > 0
            caption: root.controller && root.controller.activeSubtitleTrack >= 0 ? (root.controller.subtitleLanguage ?? "") : ""
            x: (root.stackTrackControls ? root.snap(root.toolsWidth) - root.hdrControlWidth : muteButton.x)
                - root.subtitleControlWidth
            imageSource: root.iconSources.captions || ""
            Accessible.name: qsTr("Subtitles")
            onClicked: trackMenu.showTracks(subtitleButton, true)
        }

        Item {
            id: volumeControl
            objectName: "galleryVideoVolumeControl"
            visible: root.showVolumeSlider
            readonly property bool muted: root.controller !== null && root.controller.muted
            readonly property point alignedPosition:
                root.alignedOrigin(controlsBar, root.snap(root.toolsWidth - 130 - root.hdrControlWidth),
                                   controlsBar.centeredY(height))
            x: alignedPosition.x
            y: alignedPosition.y
            width: root.snap(78)
            height: root.controlsRowHeight

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

        ToolbarButton {
            id: resumeButton
            objectName: "galleryVideoResumeButton"
            visible: root.resumeAvailable
            caption: qsTr("Resume")
            width: root.snap(36 + resumeMetrics.advanceWidth)
            x: root.snap(root.width - width - 10)
            y: root.separateResumeRow ? controlsBar.height - height - root.snap(10) : controlsBar.buttonY
            imageSource: root.iconSources.play || ""
            Accessible.name: caption
            onClicked: root.controller.resume()
            TextMetrics {
                id: resumeMetrics
                text: resumeButton.caption
                font.bold: true
                font.pixelSize: root.snap(11)
            }
        }
    }

    component ToolbarButton: Button {
        id: button
        required property url imageSource
        required property string caption
        width: root.snap(caption.length ? 54 : 36)
        y: root.stackTrackControls ? root.snap(7) : controlsBar.buttonY
        height: root.snap(36)
        padding: 0
        highlighted: trackMenu.visible && trackMenu.anchorButton === button
            || (button === subtitleButton && root.controller && root.controller.activeSubtitleTrack >= 0)
        background: Rectangle {
            radius: root.snap(5)
            color: root.uiColor(button.down ? "#48515b"
                                : button.checked || button.highlighted ? "#285e87"
                                : button.activeFocus ? (button === keyboardButton ? "#48515b" : "#285e87")
                                : "#30343a")
        }
        contentItem: Item {
            Image {
                id: icon
                objectName: button.objectName + "Icon"
                width: root.snap(18)
                height: width
                x: button.caption.length ? root.snap(6) : root.snap((parent.width - width) / 2)
                y: root.snap((parent.height - height) / 2)
                transform: Translate {
                    x: icon.PixelGridAlignment.offsetX
                    y: icon.PixelGridAlignment.offsetY
                }
                source: button.imageSource
                smooth: false
            }
            Text {
                id: languageLabel
                objectName: button.objectName + (button === resumeButton ? "Label" : "Language")
                visible: button.caption.length > 0
                text: button.caption
                x: button.imageSource.toString() !== "" ? icon.x + icon.width + root.snap(3)
                                                       : root.snap((parent.width - width) / 2)
                y: root.snap((parent.height - height) / 2)
                color: root.uiColor(root.foregroundColor)
                font.pixelSize: root.snap(11)
                font.bold: true
                renderType: Text.NativeRendering
                transform: Translate {
                    x: languageLabel.PixelGridAlignment.offsetX
                    y: languageLabel.PixelGridAlignment.offsetY
                }
            }
        }
    }

    GalleryDropdownPopup {
        id: trackMenu
        objectName: "galleryVideoTrackMenu"
        parent: root
        focus: true
        modal: true
        dim: false
        popupType: Popup.Item
        readonly property var themeWindow: root.Window.window
        menuBackground: root.uiColor(themeWindow?.controlBg ?? "#222c38")
        menuBorder: root.uiColor(themeWindow?.controlBorder ?? "#3a495b")
        menuHover: root.uiColor(themeWindow?.controlHoverBg ?? "#2a3745")
        property Item anchorButton: null
        property bool choosingSubtitles: false
        readonly property var choices: choosingSubtitles
            ? [qsTr("Off")].concat(Array.from(root.subtitleTracks)) : root.audioTracks
        readonly property int selectedIndex: !root.controller ? -1
            : choosingSubtitles
              ? (root.controller.activeSubtitleTrack ?? -1) + 1
              : (root.controller.activeAudioTrack ?? -1)
        readonly property real rowHeight: root.snap(31)
        padding: root.snap(6)
        width: root.snap(Math.max(0, Math.min(280, root.width - 16)))
        height: Math.max(0, Math.min(choices.length * rowHeight + topPadding + bottomPadding,
                                   controlsBar.y - root.snap(16)))
        x: anchorButton ? root.snap(Math.max(8, Math.min(root.width - width - 8,
                                                        anchorButton.x + anchorButton.width - width))) : 0
        y: root.snap(controlsBar.y - height - 8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        enter: Transition {}
        exit: Transition {}
        onOpened: {
            trackList.currentIndex = Math.max(0, selectedIndex)
            trackScroll.reveal(trackRows.itemAt(trackList.currentIndex))
            trackList.forceActiveFocus()
        }
        onChoicesChanged: {
            if (visible && choices.length <= 1)
                close()
        }
        function showTracks(button, subtitles) {
            anchorButton = button
            choosingSubtitles = subtitles
            root.revealControls()
            open()
        }
        function selectRow(row) {
            if (!root.controller || row < 0 || row >= choices.length)
                return
            if (choosingSubtitles)
                root.controller.selectSubtitleTrack(row - 1)
            else
                root.controller.selectAudioTrack(row)
            close()
        }
        contentItem: GalleryDropdownScrollView {
            id: trackScroll
            menuObjectName: trackMenu.objectName
            contentHeight: trackList.implicitHeight
            Column {
                id: trackList
                width: trackScroll.rowWidth
                property int currentIndex: -1
                function move(delta) {
                    currentIndex = Math.max(0, Math.min(trackRows.count - 1, currentIndex + delta))
                    trackScroll.reveal(trackRows.itemAt(currentIndex))
                }
                Keys.onUpPressed: move(-1)
                Keys.onDownPressed: move(1)
                Keys.onReturnPressed: trackMenu.selectRow(currentIndex)
                Keys.onEnterPressed: trackMenu.selectRow(currentIndex)
                Keys.onSpacePressed: trackMenu.selectRow(currentIndex)
                Repeater {
                    id: trackRows
                    model: trackMenu.choices
                    delegate: ItemDelegate {
                        id: trackRow
                        required property int index
                        required property string modelData
                        objectName: "galleryVideoTrackOption" + index
                        width: trackScroll.rowWidth
                        height: trackMenu.rowHeight
                        leftPadding: root.snap(30)
                        rightPadding: root.snap(8)
                        verticalPadding: 0
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: true
                        highlighted: index === trackList.currentIndex && trackList.activeFocus
                        onHoveredChanged: if (hovered) trackList.currentIndex = index
                        Accessible.role: Accessible.RadioButton
                        Accessible.checkable: true
                        Accessible.checked: index === trackMenu.selectedIndex
                        text: modelData
                        onClicked: trackMenu.selectRow(index)
                        background: Rectangle {
                            radius: root.snap(3)
                            color: trackRow.down || trackRow.hovered || trackRow.highlighted
                                                ? trackMenu.menuHover : "transparent"
                        }
                        Text {
                            id: checkmark
                            objectName: "galleryVideoTrackCheck" + trackRow.index
                            visible: trackRow.index === trackMenu.selectedIndex
                            x: root.snap(8)
                            y: root.snap((trackRow.height - height) / 2)
                            text: "✓"
                            color: root.uiColor(root.foregroundColor)
                            font.pixelSize: root.snap(13)
                            renderType: Text.NativeRendering
                            Accessible.ignored: true
                            transform: Translate {
                                x: checkmark.PixelGridAlignment.offsetX
                                y: checkmark.PixelGridAlignment.offsetY
                            }
                        }
                        contentItem: Text {
                            id: trackLabel
                            objectName: "galleryVideoTrackOptionText" + trackRow.index
                            text: trackRow.text
                            color: root.uiColor(root.foregroundColor)
                            font.pixelSize: root.snap(13)
                            font.bold: trackRow.index === trackMenu.selectedIndex
                            renderType: Text.NativeRendering
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            Accessible.ignored: true
                            transform: Translate {
                                x: trackLabel.PixelGridAlignment.offsetX
                                y: trackLabel.PixelGridAlignment.offsetY
                            }
                        }
                    }
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
