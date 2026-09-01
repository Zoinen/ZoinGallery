pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: motion

    required property GalleryPanel panel

    property alias scrollAnimation: panelScrollObject
    property alias cursorAnimation: cursorAnimationObject
    property alias cursorXAnimation: cursorChromeXAnimation
    property alias cursorYAnimation: cursorChromeYAnimation
    property alias cursorWidthAnimation: cursorChromeWidthAnimation
    property alias cursorHeightAnimation: cursorChromeHeightAnimation
    property alias cursorFinalizeTimer: cursorFinalizeObject
    property alias cursorRetargetTimer: cursorRetargetObject
    property alias pathPlacementTimer: pathPlacementObject
    property alias viewportTimer: viewportTimerObject
    property alias cursorAfterScrollTimer: cursorAfterScrollObject
    property alias thumbnailResizeTimer: thumbnailResizeObject
    property alias densityTimer: densityTimerObject

    property Timer pathViewportPlacementTimer: Timer {
        id: pathPlacementObject
        objectName: "galleryPathViewportPlacementTimer"
        interval: 0
        repeat: false
        onTriggered: {
            motion.panel.traceBenchmarkStage("placement.timer.triggered", {})
            motion.panel.placeViewportForPathChange()
        }
    }

    property Timer viewportUpdateTimer: Timer {
        id: viewportTimerObject
        interval: 0
        repeat: false
        onTriggered: {
            const panel = motion.panel
            const ensureCursor = panel.viewportUpdateEnsuresCursor
            const suppressAnimation = panel.viewportUpdateSuppressAnimation
            panel.viewportUpdateEnsuresCursor = false
            panel.viewportUpdateSuppressAnimation = false
            if (ensureCursor)
                panel.ensureCurrentVisible(!suppressAnimation)
            else
                panel.restoreScrollOrEnsureCursor()
        }
    }

    property Timer cursorCommitTimer: Timer {
        id: cursorCommitObject
        interval: 5000
        repeat: false
        onTriggered: {
            if (motion.panel.keyboardShiftSelectionActive
                    || motion.panel.keyboardToggleSelectionActive)
                return
            motion.panel.commitPendingCursor()
        }
    }

    property Timer cursorCommitAfterScrollTimer: Timer {
        id: cursorAfterScrollObject
        interval: 0
        repeat: false
        onTriggered: {
            const panel = motion.panel
            if (panel.cursorCommitAfterScroll
                    && !panel.navigationKeyHeld
                    && !motion.scrollAnimation.running
                    && !motion.cursorAnimation.running)
                panel.commitPendingCursor()
        }
    }

    property Timer thumbnailResizeDecodeTimer: Timer {
        id: thumbnailResizeObject
        interval: 120
        repeat: false
        onTriggered: {
            const panel = motion.panel
            if (panel.controllerReady && panel.galleryLayout.width > 0
                    && panel.galleryLayout.count > 0)
                panel.galleryLayout.reReadAndDecodeThumbnails()
        }
    }

    property Timer densityCommitTimer: Timer {
        id: densityTimerObject
        interval: 180
        repeat: false
        onTriggered: motion.panel.noteDensityChanged(true)
    }

    property NumberAnimation panelScrollAnimation: NumberAnimation {
        id: panelScrollObject
        objectName: "galleryPanelScrollAnimation"
        target: motion.panel.galleryLayout
        property: "contentY"
        duration: 150
        easing.type: Easing.OutSine
        onRunningChanged: {
            const panel = motion.panel
            if (running)
                motion.cursorFinalizeTimer.stop()
            else {
                panel.masonryPageScrollActive = false
                if (panel.cursorChromeTransitionActive)
                    motion.cursorFinalizeTimer.restart()
            }
            if (!running && panel.cursorCommitAfterScroll)
                motion.cursorAfterScrollTimer.restart()
            if (!running && !panel.suppressScrollAnimationPersistence
                    && panel.controllerReady) {
                panel.controller.panelScrollOffset =
                        panel.galleryLayout.contentY
                panel.controller.panelViewportCursorEntryId =
                        panel.controller.cursorEntryId
                if (panel.viewportUpdatePendingAfterScroll) {
                    const ensureCursor = panel.viewportUpdateEnsuresCursor
                    panel.viewportUpdatePendingAfterScroll = false
                    panel.viewportUpdateEnsuresCursor = false
                    panel.scheduleViewportUpdate(ensureCursor)
                }
            }
            if (!running)
                panel.updateVisualCursorForViewport()
        }
    }

    property ParallelAnimation cursorChromeGeometryAnimation:
            ParallelAnimation {
        id: cursorAnimationObject
        objectName: "galleryCursorChromeGeometryAnimation"
        onRunningChanged: {
            const panel = motion.panel
            if (running) {
                motion.cursorFinalizeTimer.stop()
            } else {
                if (panel.cursorChromeTransitionActive)
                    motion.cursorFinalizeTimer.restart()
                if (panel.cursorCommitAfterScroll)
                    motion.cursorAfterScrollTimer.restart()
            }
        }
        NumberAnimation {
            id: cursorChromeXAnimation
            target: motion.panel
            property: "cursorChromeX"
            duration: 150
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            id: cursorChromeYAnimation
            target: motion.panel
            property: "cursorChromeY"
            duration: 150
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            id: cursorChromeWidthAnimation
            target: motion.panel
            property: "cursorChromeWidth"
            duration: 150
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            id: cursorChromeHeightAnimation
            target: motion.panel
            property: "cursorChromeHeight"
            duration: 150
            easing.type: Easing.OutSine
        }
    }

    property Timer cursorChromeFinalizeTimer: Timer {
        id: cursorFinalizeObject
        interval: 0
        repeat: false
        onTriggered: {
            if (!motion.scrollAnimation.running
                    && !motion.cursorAnimation.running)
                motion.panel.finishCursorChromeTransition()
        }
    }

    property Timer cursorChromeRetargetTimer: Timer {
        id: cursorRetargetObject
        interval: 0
        repeat: false
        onTriggered: motion.panel.retargetCursorChromeAfterLayoutReset()
    }
}
