pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.PixelGrid 1.0

Window {
    id: window
    objectName: "galleryHdrVideoWindow"
    required property var videoSurface
    required property var hdrOutput
    color: "black"
    visible: false
    readonly property real outputDpr: Screen.devicePixelRatio

    PixelGridScope {
        id: content
        anchors.fill: parent
        function snap(value) { return Math.round(value * window.outputDpr) / window.outputDpr }
        readonly property real aspect: window.videoSurface.displaySize.height > 0
            ? window.videoSurface.displaySize.width / window.videoSurface.displaySize.height : 1

        ViewerResampleEffect {
            objectName: "galleryHdrVideoImage"
            width: content.snap(Math.min(content.width, content.height * content.aspect))
            height: content.snap(Math.min(content.height, content.width / content.aspect))
            x: content.snap((content.width - width) / 2)
            y: content.snap((content.height - height) / 2)
            viewportSize: Qt.size(width * window.outputDpr, height * window.outputDpr)
            videoSource: window.videoSurface.frameSource
            hdrOutput: true
            hardwareSampling: true
        }

        GalleryVideoPlaybackSurface {
            objectName: "galleryHdrPlaybackSurface"
            anchors.fill: parent
            controller: window.videoSurface.controller
            sharedFrameSource: window.videoSurface.frameSource
            managesFrameSource: false
            externalHdrOutput: window.hdrOutput
            linearOutput: true
            iconSources: window.videoSurface.iconSources
            foregroundColor: window.videoSurface.foregroundColor
            mutedColor: window.videoSurface.mutedColor
            videoIdentity: window.videoSurface.videoIdentity
            devicePixelRatio: window.outputDpr
        }

        focus: true
        Keys.onPressed: event => {
            const playback = window.videoSurface.controller
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_F11) window.hdrOutput.close()
            else if (event.key === Qt.Key_Space) playback.playPause()
            else if (event.key === Qt.Key_M) playback.toggleMute()
            else if (event.modifiers & Qt.ControlModifier && event.key === Qt.Key_Left) playback.seekBy(-5000)
            else if (event.modifiers & Qt.ControlModifier && event.key === Qt.Key_Right) playback.seekBy(5000)
            else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_Right || event.key === Qt.Key_Down) window.hdrOutput.navigate(1)
            else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_Left || event.key === Qt.Key_Up || event.key === Qt.Key_Backspace) window.hdrOutput.navigate(-1)
            else return
            event.accepted = true
        }
    }
    onActiveChanged: if (active) content.forceActiveFocus()
}
