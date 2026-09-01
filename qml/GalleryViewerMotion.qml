pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

Item {
    id: root

    required property Item viewer
    required property FlickableZoomable viewport

    visible: false

    readonly property alias transitionAnimation: transition
    readonly property alias transitionFinalizeTimer: transitionFinalize
    readonly property alias pinchCloseFinalizeTimer: pinchFinalize
    readonly property alias pinchCloseProgressAnimation: pinchProgress
    readonly property alias navigationOffsetAnimation: navigationOffset
    readonly property alias decodeRequestTimer: decodeRequest
    readonly property alias committedViewportTimer: committedViewport
    readonly property alias previousViewportTimer: previousViewport
    readonly property alias authorityTimer: authority
    readonly property alias navigationFinishTimer: navigationFinish
    readonly property alias wheelPanFinishTimer: wheelPanFinish
    readonly property alias navigationGestureEndTimer: navigationGestureEnd
    readonly property alias navigationResidualQuietTimer:
        navigationResidualQuiet

    NumberAnimation {
        id: transition
        objectName: "galleryViewerTransitionAnimation"
        target: root.viewer
        property: "transitionProgress"
        easing.type: Easing.OutSine
        onFinished: root.viewer.completeTransition()
    }

    EventLoopTimer {
        id: transitionFinalize
        objectName: "galleryViewerTransitionFinalizeTimer"
        interval: Math.max(1, root.viewer.animationDuration + 75)
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: root.viewer.completeTransition()
    }

    EventLoopTimer {
        id: pinchFinalize
        interval: root.viewer.animationDuration + 50
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: {
            if (root.viewer.pinchCloseFinishingCommit)
                root.viewer.completePinchCloseCommit()
        }
    }

    NumberAnimation {
        id: pinchProgress
        target: root.viewer
        property: "pinchCloseProgress"
        easing.type: Easing.OutSine
        onFinished: {
            if (root.viewer.pinchCloseFinishingCommit)
                root.viewer.completePinchCloseCommit()
        }
    }

    NumberAnimation {
        id: navigationOffset
        target: root.viewer
        property: "viewerNavigationOffsetX"
        easing.type: Easing.OutSine
        onFinished: {
            if (root.viewer.viewerNavigationCommitAfterAnimation)
                root.viewer.commitViewerNavigation()
            else {
                root.viewer.resetViewerNavigation()
                root.viewport.settlePan()
            }
        }
    }

    EventLoopTimer {
        id: decodeRequest
        interval: 80
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: root.viewer.requestImage()
    }

    Timer {
        id: committedViewport
        interval: 5
        onTriggered: root.viewer.applyPendingCommittedViewport()
    }

    EventLoopTimer {
        id: previousViewport
        interval: 5
        singleShot: true
        timerType: Qt.PreciseTimer
        onTimeout: root.viewer.applyPendingPreviousImageViewport()
    }

    Timer {
        id: authority
        interval: 1500
        onTriggered: {
            if (root.viewer.session
                    && root.viewer.session.cursorEntryId
                       !== root.viewer.presentedEntryId) {
                root.viewer.setPresentedIndex(
                            root.viewer.session.currentIndex, false)
            }
            root.viewer.pendingAuthorityIndex = -1
            root.viewer.pendingAuthorityEntryId = ""
        }
    }

    Timer {
        id: navigationFinish
        interval: 140
        onTriggered: root.viewer.finishViewerNavigation()
    }

    Timer {
        id: wheelPanFinish
        interval: 70
        onTriggered: root.viewport.finishWheelPan()
    }

    Timer {
        id: navigationGestureEnd
        interval: 350
        onTriggered: root.viewer.endViewerNavigationGesture()
    }

    Timer {
        id: navigationResidualQuiet
        interval: 180
        onTriggered:
            root.viewer.clearViewerNavigationResidualSuppression()
    }
}
