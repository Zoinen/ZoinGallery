import QtQuick

import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects
import ZoinGallery 1.0

Item {
    id: viewerMode

    visible: false

    property int animationDuration: 150
    property int easingType: Easing.OutSine

    property real sphericViewerOpacity: 1
    property bool sphericViewerMode: false
    onSphericViewerModeChanged: {
        if (sphericViewerMode) {
            fileListModel.cancelAllDecodeViewerRunners()
            fileListModel.requestViewer(currentSourceIndex())
            sphericViewerLoader.sourceComponent = sphericViewerComponent
        }
        else {
            sphericViewerLoader.sourceComponent = undefined
        }
    }

    property bool panelsVisible: false
    property alias zoomFitView: flickableArea.zoomFitView
    readonly property real viewerChromeOpacity: root.state === "viewer" ?
            (root.viewerPinchCloseActive ? 1 - root.viewerPinchCloseProgress : 1) : 0

    property alias animation: viewerAnimation
    property alias imageContainer: flickableArea

    signal pinchZoomOutToThumbnailsProgressed(real progress)
    signal pinchZoomOutToThumbnailsFinished(bool commit)

    function sourceIndexForViewIndex(viewIndex) {
        return galleryViewModel.mapToSourceRow(viewIndex)
    }

    function currentSourceIndex() {
        return sourceIndexForViewIndex(masonryLayout.view.currentIndex)
    }

    readonly property bool currentItemSelected: fileListModel.selectedCount >= 0 &&
            fileListModel.isIndexSelected(currentSourceIndex())
    readonly property bool viewerNavigationTargetSelected: fileListModel.selectedCount >= 0 &&
            viewerNavigationTargetIndex !== -1 && fileListModel.isIndexSelected(sourceIndexForViewIndex(viewerNavigationTargetIndex))
    readonly property real viewerBackgroundOpacity: viewerBackground.opacity
    readonly property real currentSelectionHighlightPresence: !viewerNavigationActive || viewerNavigationTargetIndex === -1 ? 1 :
            (viewerNavigationDirection < 0 ? viewerNavigationCurrentOpacity : 1 - viewerNavigationProgress)
    readonly property real targetSelectionHighlightPresence: viewerNavigationActive && viewerNavigationTargetIndex !== -1 ?
            (viewerNavigationDirection < 0 ? viewerNavigationCoverProgress : viewerNavigationProgress) : 0
    readonly property real selectionHighlightNavigationOpacity:
            Math.min(1, (currentItemSelected ? currentSelectionHighlightPresence : 0) +
                        (viewerNavigationTargetSelected ? targetSelectionHighlightPresence : 0))
    property bool selectionHighlightAnimationSuppressed: false

    component BlurBackground : MultiEffect {
        id: blurItem
        source: ShaderEffectSource {
            sourceItem: sphericViewerMode ? sphericViewerLoader.item : flickableAreaContainer
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

        fragmentShader: "qrc:/resources/outline.frag.qsb"
    }

    // ZZZ: Viewer size request takes current image's full size for all future images!!!

    function fitCurrentImageWhenReady() {
        if (!zoomFitView || sphericViewerMode) {
            delayedFitTimer.stop()
            return
        }

        if (flickableArea.originalSize.width > 1 && flickableArea.originalSize.height > 1) {
            delayedFitTimer.stop()
            if (!flickableArea.viewportAnimationRunning) {
                flickableArea.zoomToFit(true)
            }
        }
        else {
            delayedFitTimer.restartWait()
        }
    }

    function setImage(imageIdUrl, originalSize, fromIndex, level) {
        flickableArea.setImage(imageIdUrl, originalSize, fromIndex, level)
    }

    Timer {
        id: delayedFitTimer
        interval: 50
        repeat: true
        property int attempts: 0

        function restartWait() {
            attempts = 0
            restart()
        }

        onTriggered: {
            let size = masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex)
            if (size.width > 1 && size.height > 1) {
                stop()
                let level = flickableArea.image.fromLevel >= 0 ? flickableArea.image.fromLevel : 0
                viewerMode.setImage(flickableArea.image.source, size, masonryLayout.view.currentIndex, level)
                fitCurrentImageWhenReady()
                fileListModel.cancelAllDecodeViewerRunners()
                fileListModel.requestViewer(currentSourceIndex(), viewerMode.width * dpr, viewerMode.height * dpr)
            }
            else if (++attempts >= 600) {
                stop()
            }
        }
    }

    function show(sphericViewer) {
        sphericViewerMode = sphericViewer === "True"
        onCurrentIndexChanged()
        visible = true
        fitCurrentImageWhenReady()
    }

    function completeInstantOpen() {
        delegateOutline.opacity = 0
        imageInfoPanel.opacity = 0
        sphericViewerOpacity = 1
        flickableArea.x = 0
        flickableArea.y = 0
        flickableArea.width = Qt.binding(() => viewerMode.width)
        flickableArea.height = Qt.binding(() => viewerMode.height)
        Qt.callLater(() => fitCurrentImageWhenReady())
    }

    function onCurrentIndexChanged() {
        let exif = masonryLayout.view.indexExif(masonryLayout.view.currentIndex)
        sphericViewerMode = exif["Panorama"] === "True"

        if (zoomFitView && !sphericViewerMode) {
            // console.log("onCIC FIT", viewerMode.width * dpr, viewerMode.height * dpr)
            fileListModel.requestViewer(currentSourceIndex(), viewerMode.width * dpr, viewerMode.height * dpr)
        }
        else {
            // console.log("onCIC ORIG", flickableArea.originalSize.width * dpr, flickableArea.originalSize.height * dpr)
            fileListModel.requestViewer(currentSourceIndex())
            flickableArea.forceShowScrollBars = true
            flickableArea.forceShowScrollBars = false
        }

        updateTitle()
    }

    function updateTitle() {
        topLevelWindow.title = masonryLayout.view.indexText(masonryLayout.view.currentIndex) + " [" +
                (masonryLayout.view.currentImageIndex + 1) + "/" + masonryLayout.view.imageCount + "] - ZoinGallery"
    }

    function toggleCurrentSelection() {
        fileListModel.toggleSelection(currentSourceIndex())
    }

    property bool leftPressed: false
    property bool rightPressed: false
    property bool upPressed: false
    property bool downPressed: false
    property bool zoomInPressed: false
    property bool zoomOutPressed: false
    property bool controlPressed: false
    property bool shiftSelectionActive: false
    property int shiftSelectionAnchorIndex: -1
    property bool shiftNavigationSelectionValue: true

    property int previousImageIndex: -1
    property int lastKnownIndex: -1
    property bool previousImageLocked: false
    property int lockedPreviousReturnIndex: -1
    property string lockedPreviousImageIdUrl: previousImageLocked && previousImageIndex !== -1 ? masonryLayout.view.indexImageIdUrl(previousImageIndex) : ""
    property string lockedPreviousPath: previousImageLocked && previousImageIndex !== -1 ? masonryLayout.view.indexFullPath(previousImageIndex) : ""

    property real viewerNavigationOffsetX: 0
    property real viewerNavigationOverdrag: 0
    property real viewerNavigationVelocityX: 0
    property real viewerNavigationLastTime: 0
    property bool viewerGestureLogging: true
    property bool viewerNavigationActive: false
    property bool viewerNavigationRevealed: false
    property bool viewerNavigationCommitAfterAnimation: false
    property bool viewerNavigationGestureActive: false
    property bool viewerNavigationGestureCommitted: false
    property bool viewerNavigationGestureHasPhase: false
    property bool viewerNavigationSuppressMomentum: false
    property int viewerNavigationGestureSerial: 0
    property int viewerNavigationDirection: 0 // -1 = previous image, 1 = next image
    property int viewerNavigationTargetIndex: -1
    property string viewerNavigationTargetSource: ""
    readonly property size viewerNavigationTargetOriginalSize: viewerNavigationTargetIndex !== -1 ?
            masonryLayout.view.indexOriginalSize(viewerNavigationTargetIndex) : Qt.size(0, 0)
    readonly property bool viewerNavigationTargetHasSize: viewerNavigationTargetOriginalSize.width > 1 &&
            viewerNavigationTargetOriginalSize.height > 1 && viewerMode.width > 0 && viewerMode.height > 0
    readonly property size viewerNavigationTargetDisplayOriginalSize: viewerNavigationTargetHasSize ?
            Qt.size(viewerNavigationTargetOriginalSize.width / dpr, viewerNavigationTargetOriginalSize.height / dpr) :
            Qt.size(0, 0)
    readonly property size viewerNavigationTargetEffectiveOriginalSize: viewerNavigationTargetHasSize ?
            (flickableArea.rotationMode % 2 === 1 ?
                 Qt.size(viewerNavigationTargetDisplayOriginalSize.height, viewerNavigationTargetDisplayOriginalSize.width) :
                 viewerNavigationTargetDisplayOriginalSize) : Qt.size(0, 0)
    readonly property bool viewerNavigationTargetKeepsZoom: viewerNavigationTargetHasSize && !flickableArea.zoomFitView
    readonly property real viewerNavigationTargetAspect: viewerNavigationTargetHasSize ?
            viewerNavigationTargetEffectiveOriginalSize.width / viewerNavigationTargetEffectiveOriginalSize.height : 1
    readonly property bool viewerNavigationTargetFitToHeight: viewerNavigationTargetHasSize ?
            viewerNavigationTargetAspect <= viewerMode.width / viewerMode.height : false
    readonly property real viewerNavigationTargetScale: !viewerNavigationTargetHasSize ? 1 : viewerNavigationTargetKeepsZoom ?
            flickableArea.zoomScale :
            (viewerNavigationTargetFitToHeight ?
                 viewerMode.height / viewerNavigationTargetEffectiveOriginalSize.height :
                 viewerMode.width / viewerNavigationTargetEffectiveOriginalSize.width)
    readonly property real viewerNavigationTargetDisplayWidth: viewerNavigationTargetHasSize ?
            viewerNavigationTargetEffectiveOriginalSize.width * viewerNavigationTargetScale :
            viewerMode.width
    readonly property real viewerNavigationTargetDisplayHeight: viewerNavigationTargetHasSize ?
            viewerNavigationTargetEffectiveOriginalSize.height * viewerNavigationTargetScale :
            viewerMode.height
    readonly property real viewerNavigationTargetPreservedImageX: viewerNavigationTargetDisplayWidth < viewerMode.width ?
            (viewerMode.width - viewerNavigationTargetDisplayWidth) * 0.5 :
            Math.min(0, Math.max(flickableArea.image.x, viewerMode.width - viewerNavigationTargetDisplayWidth))
    readonly property real viewerNavigationTargetLeftAlignedImageX: viewerNavigationTargetDisplayWidth < viewerMode.width ?
            (viewerMode.width - viewerNavigationTargetDisplayWidth) * 0.5 :
            0
    readonly property real viewerNavigationTargetRightAlignedImageX: viewerNavigationTargetDisplayWidth < viewerMode.width ?
            (viewerMode.width - viewerNavigationTargetDisplayWidth) * 0.5 :
            viewerMode.width - viewerNavigationTargetDisplayWidth
    readonly property real viewerNavigationTargetFinalImageX: viewerNavigationTargetKeepsZoom ?
            (viewerNavigationDirection < 0 ? viewerNavigationTargetRightAlignedImageX :
                 viewerNavigationDirection > 0 ? viewerNavigationTargetLeftAlignedImageX :
                     viewerNavigationTargetPreservedImageX) : viewerNavigationTargetPreservedImageX
    readonly property real viewerNavigationTargetFinalImageY: viewerNavigationTargetDisplayHeight < viewerMode.height ?
            (viewerMode.height - viewerNavigationTargetDisplayHeight) * 0.5 :
            Math.min(0, Math.max(flickableArea.image.y, viewerMode.height - viewerNavigationTargetDisplayHeight))
    readonly property real viewerNavigationTargetTravelDistance: viewerNavigationDirection < 0 ?
            Math.max(1, viewerNavigationTargetDisplayWidth + viewerNavigationTargetFinalImageX) : Math.max(1, viewerMode.width)
    readonly property real viewerNavigationProgress: Math.min(1, Math.abs(viewerNavigationOffsetX) / Math.max(1, viewerMode.width * 0.5))
    readonly property real viewerNavigationCoverProgress: Math.min(1, Math.abs(viewerNavigationOffsetX) / viewerNavigationTargetTravelDistance)
    readonly property real viewerNavigationTargetOpacity: viewerNavigationDirection < 0 ? 1 : viewerNavigationProgress
    readonly property real viewerNavigationCurrentOpacity: viewerNavigationDirection < 0 && viewerNavigationTargetIndex !== -1 ? 1 - viewerNavigationCoverProgress : 1
    readonly property real viewerNavigationCurrentOffsetX: viewerNavigationDirection < 0 && viewerNavigationTargetIndex !== -1 ? 0 : viewerNavigationOffsetX
    readonly property real viewerNavigationTargetImageX: viewerNavigationDirection < 0 ?
            -viewerNavigationTargetDisplayWidth + Math.min(Math.max(viewerNavigationOffsetX, 0), viewerNavigationTargetTravelDistance) :
            viewerNavigationTargetFinalImageX
    readonly property real viewerNavigationTargetImageY: viewerNavigationTargetFinalImageY
    readonly property real viewerNavigationOverdragThreshold: Math.min(48, viewerMode.width * 0.08)
    readonly property real viewerNavigationCommitThreshold: Math.min(120,
            Math.max(viewerNavigationOverdragThreshold * 1.35, viewerMode.width * 0.12))

    function isTildeKey(event) {
        return event.key === Qt.Key_QuoteLeft || event.key === Qt.Key_AsciiTilde || event.key === 1025
    }

    function viewerGestureNumber(value) {
        return Number(value).toFixed(2)
    }

    function viewerGesturePhaseName(phase) {
        if (phase === ViewerWheelArea.ScrollBegin) {
            return "begin"
        }
        if (phase === ViewerWheelArea.ScrollUpdate) {
            return "update"
        }
        if (phase === ViewerWheelArea.ScrollEnd) {
            return "end"
        }
        if (phase === ViewerWheelArea.ScrollMomentum) {
            return "momentum"
        }
        return "none"
    }

    function viewerGestureDirectionName(direction) {
        if (direction > 0) {
            return "next"
        }
        if (direction < 0) {
            return "previous"
        }
        return "none"
    }

    function viewerGestureSnapshot() {
        return "gesture=" + viewerNavigationGestureSerial +
                " gestureActive=" + viewerNavigationGestureActive +
                " committed=" + viewerNavigationGestureCommitted +
                " phaseAware=" + viewerNavigationGestureHasPhase +
                " suppressMomentum=" + viewerNavigationSuppressMomentum +
                " navActive=" + viewerNavigationActive +
                " revealed=" + viewerNavigationRevealed +
                " dir=" + viewerGestureDirectionName(viewerNavigationDirection) +
                " offset=" + viewerGestureNumber(viewerNavigationOffsetX) +
                " overdrag=" + viewerGestureNumber(viewerNavigationOverdrag) +
                " velocity=" + viewerGestureNumber(viewerNavigationVelocityX) +
                " current=" + masonryLayout.view.currentIndex +
                " target=" + viewerNavigationTargetIndex +
                " zoomFit=" + flickableArea.zoomFitView
    }

    function logViewerGesture(message) {
        if (!viewerGestureLogging) {
            return
        }
        console.log("[ViewerGesture] " + message + " | " + viewerGestureSnapshot())
    }

    function resetViewerNavigation(reason) {
        if (reason !== undefined && (viewerNavigationActive || Math.abs(viewerNavigationOffsetX) > 0.1 ||
                viewerNavigationOverdrag > 0.1 || viewerNavigationTargetIndex !== -1)) {
            logViewerGesture("reset reason=" + reason)
        }
        viewerNavigationFinishTimer.stop()
        viewerNavigationOffsetAnimation.stop()
        viewerNavigationOffsetX = 0
        viewerNavigationOverdrag = 0
        viewerNavigationVelocityX = 0
        viewerNavigationLastTime = 0
        viewerNavigationActive = false
        viewerNavigationRevealed = false
        viewerNavigationCommitAfterAnimation = false
        viewerNavigationDirection = 0
        viewerNavigationTargetIndex = -1
        viewerNavigationTargetSource = ""
    }

    function beginViewerNavigationGesture(forceNew, hasPhase) {
        if (hasPhase) {
            viewerNavigationGestureEndTimer.stop()
        }
        if (forceNew || !viewerNavigationGestureActive) {
            viewerNavigationResidualQuietTimer.stop()
            viewerNavigationGestureSerial += 1
            viewerNavigationGestureActive = true
            viewerNavigationGestureCommitted = false
            viewerNavigationSuppressMomentum = false
            viewerNavigationLastTime = 0
            logViewerGesture("gesture begin forceNew=" + forceNew + " phaseAware=" + hasPhase)
        }
        viewerNavigationGestureHasPhase = hasPhase
    }

    function continueViewerNavigationGesture(hasPhase) {
        beginViewerNavigationGesture(false, hasPhase)
        if (!hasPhase) {
            viewerNavigationGestureEndTimer.restart()
        }
    }

    function endViewerNavigationGesture(clearCommitted) {
        logViewerGesture("gesture end clearCommitted=" + (clearCommitted === undefined ? true : clearCommitted))
        viewerNavigationGestureEndTimer.stop()
        viewerNavigationGestureActive = false
        viewerNavigationGestureHasPhase = false
        if (clearCommitted === undefined || clearCommitted) {
            viewerNavigationGestureCommitted = false
            viewerNavigationSuppressMomentum = false
            viewerNavigationResidualQuietTimer.stop()
        }
        viewerNavigationLastTime = 0
    }

    function startViewerNavigationResidualSuppression(reason) {
        if (!viewerNavigationSuppressMomentum) {
            logViewerGesture("residual suppression begin reason=" + reason)
        }
        viewerNavigationSuppressMomentum = true
        viewerNavigationResidualQuietTimer.restart()
    }

    function clearViewerNavigationResidualSuppression(reason) {
        if (!viewerNavigationSuppressMomentum) {
            return
        }

        if (viewerNavigationOffsetAnimation.running || viewerNavigationCommitAfterAnimation) {
            viewerNavigationResidualQuietTimer.restart()
            return
        }

        logViewerGesture("residual suppression clear reason=" + reason)
        viewerNavigationSuppressMomentum = false
        if (!viewerNavigationGestureActive) {
            viewerNavigationGestureCommitted = false
        }
    }

    function hiddenNavigationOffset(overdrag) {
        return Math.min(overdrag * 0.35, viewerNavigationOverdragThreshold * 0.5)
    }

    function updateViewerNavigationTargetSource() {
        if (viewerNavigationTargetIndex !== -1) {
            viewerNavigationTargetSource = fileListModel.bestViewerImageUrlForIndex(sourceIndexForViewIndex(viewerNavigationTargetIndex))
        }
    }

    function beginViewerNavigation(direction) {
        let currentIndex = masonryLayout.view.currentIndex
        let targetIndex = masonryLayout.view.nextImageIndex(direction > 0, false)

        viewerNavigationActive = true
        viewerNavigationDirection = direction
        viewerNavigationTargetIndex = targetIndex !== currentIndex ? targetIndex : -1
        viewerNavigationTargetSource = ""
        flickableArea.cancelWheelPan()
        updateViewerNavigationTargetSource()
        logViewerGesture("navigation begin direction=" + viewerGestureDirectionName(direction) +
                         " current=" + currentIndex + " target=" + viewerNavigationTargetIndex +
                         " sourceReady=" + (viewerNavigationTargetSource !== ""))
    }

    function applyViewerNavigationDelta(deltaX) {
        if (Math.abs(deltaX) < 0.1) {
            return
        }

        let direction = deltaX < 0 ? 1 : -1
        if (!viewerNavigationActive || viewerNavigationDirection !== direction && !viewerNavigationRevealed) {
            resetViewerNavigation("new-direction direction=" + viewerGestureDirectionName(direction))
            beginViewerNavigation(direction)
        }

        let signedDelta = -viewerNavigationDirection * deltaX
        viewerNavigationOverdrag = Math.max(0, viewerNavigationOverdrag + signedDelta)

        if (viewerNavigationOverdrag <= 0.1) {
            resetViewerNavigation("overdrag-cleared deltaX=" + viewerGestureNumber(deltaX))
            return
        }

        if (viewerNavigationTargetIndex === -1) {
            viewerNavigationRevealed = false
            viewerNavigationOffsetX = -viewerNavigationDirection *
                    Math.min(viewerNavigationOverdrag * 0.25, viewerNavigationOverdragThreshold * 0.6)
            logViewerGesture("edge resistance noTarget deltaX=" + viewerGestureNumber(deltaX))
            return
        }

        if (viewerNavigationOverdrag < viewerNavigationOverdragThreshold) {
            viewerNavigationRevealed = false
            viewerNavigationOffsetX = viewerNavigationDirection < 0 ?
                    viewerNavigationOverdrag : -viewerNavigationDirection * hiddenNavigationOffset(viewerNavigationOverdrag)
            logViewerGesture("hidden overdrag deltaX=" + viewerGestureNumber(deltaX) +
                             " threshold=" + viewerGestureNumber(viewerNavigationOverdragThreshold))
            return
        }

        if (!viewerNavigationRevealed) {
            logViewerGesture("neighbor reveal")
        }
        viewerNavigationRevealed = true
        let visibleDistance = viewerNavigationDirection < 0 ?
                viewerNavigationOverdrag :
                hiddenNavigationOffset(viewerNavigationOverdragThreshold) +
                viewerNavigationOverdrag - viewerNavigationOverdragThreshold
        let maxOffset = viewerNavigationDirection < 0 ? viewerNavigationTargetTravelDistance : viewerMode.width
        viewerNavigationOffsetX = -viewerNavigationDirection * Math.min(visibleDistance, maxOffset)
        logViewerGesture("drag progress deltaX=" + viewerGestureNumber(deltaX) +
                         " visibleDistance=" + viewerGestureNumber(visibleDistance))
    }

    function viewerNavigationFinishAnimationDuration(targetOffset, shouldCommit) {
        if (!shouldCommit) {
            return viewerMode.animationDuration
        }

        let fullDistance = viewerNavigationDirection < 0 ? viewerNavigationTargetTravelDistance : viewerMode.width
        let remainingDistance = Math.abs(targetOffset - viewerNavigationOffsetX)
        let remainingRatio = Math.min(1, remainingDistance / Math.max(1, fullDistance))
        return viewerMode.animationDuration * (1 + remainingRatio)
    }

    function finishViewerNavigation() {
        viewerNavigationFinishTimer.stop()
        if (!viewerNavigationActive) {
            logViewerGesture("finish without active navigation")
            flickableArea.finishWheelPan()
            return
        }

        let signedVelocity = -viewerNavigationDirection * viewerNavigationVelocityX
        let signedOffset = -viewerNavigationDirection * viewerNavigationOffsetX
        let gestureDistance = viewerNavigationOverdrag
        let shouldCommit = viewerNavigationTargetIndex !== -1 && viewerNavigationRevealed &&
                (gestureDistance >= viewerNavigationCommitThreshold || signedVelocity > 900)

        if (shouldCommit) {
            viewerNavigationGestureCommitted = true
        }
        let targetOffset = shouldCommit ?
                (viewerNavigationDirection < 0 ? viewerNavigationTargetTravelDistance : -viewerNavigationDirection * viewerMode.width) : 0
        let finishDuration = viewerNavigationFinishAnimationDuration(targetOffset, shouldCommit)
        logViewerGesture("finish shouldCommit=" + shouldCommit +
                         " signedOffset=" + viewerGestureNumber(signedOffset) +
                         " gestureDistance=" + viewerGestureNumber(gestureDistance) +
                         " signedVelocity=" + viewerGestureNumber(signedVelocity) +
                         " threshold=" + viewerGestureNumber(viewerNavigationCommitThreshold) +
                         " duration=" + viewerGestureNumber(finishDuration))
        viewerNavigationCommitAfterAnimation = shouldCommit
        viewerNavigationOffsetAnimation.to = targetOffset
        viewerNavigationOffsetAnimation.duration = finishDuration
        viewerNavigationOffsetAnimation.restart()
    }

    function commitViewerNavigation() {
        let targetIndex = viewerNavigationTargetIndex
        let targetImageX = viewerNavigationTargetFinalImageX
        let targetImageY = viewerNavigationTargetFinalImageY
        logViewerGesture("commit target=" + targetIndex)
        viewerNavigationGestureCommitted = true
        startViewerNavigationResidualSuppression("commit")
        selectionHighlightAnimationSuppressed = true
        resetViewerNavigation("commit")
        if (targetIndex === -1 || targetIndex === masonryLayout.view.currentIndex) {
            Qt.callLater(() => selectionHighlightAnimationSuppressed = false)
            return
        }

        if (!flickableArea.zoomFitView) {
            flickableArea.image.x = targetImageX
            flickableArea.image.y = targetImageY
        }
        masonryLayout.setCurrentIndex(targetIndex)
        onCurrentIndexChanged()
        Qt.callLater(() => selectionHighlightAnimationSuppressed = false)
    }

    function finishViewerNavigationAnimationNow(reason) {
        if (!viewerNavigationOffsetAnimation.running && !viewerNavigationCommitAfterAnimation) {
            return
        }

        logViewerGesture("finish animation now reason=" + reason)
        viewerNavigationOffsetAnimation.stop()
        if (viewerNavigationCommitAfterAnimation) {
            commitViewerNavigation()
        }
        else {
            resetViewerNavigation("interrupted animation: " + reason)
            flickableArea.settlePan()
        }
    }

    function wheelDeltaPixels(pixelDelta, angleDelta) {
        if (pixelDelta !== 0) {
            return pixelDelta
        }

        return angleDelta / 120 * 80
    }

    function switchImageForLegacyWheel(angleDeltaY) {
        let nextIndex = -1
        let currentIndex = masonryLayout.view.currentIndex
        if (angleDeltaY < 0) {
            nextIndex = masonryLayout.moveInImageList(true, false)
        }
        else if (angleDeltaY > 0) {
            nextIndex = masonryLayout.moveInImageList(false, false)
        }

        if (nextIndex !== -1 && nextIndex !== currentIndex) {
            logViewerGesture("legacy wheel switch angleDeltaY=" + angleDeltaY + " target=" + nextIndex)
            onCurrentIndexChanged()
        }
    }

    function isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                                      phase, hasPixelDelta, nativeMomentum,
                                      nativePhase, nativeMomentumPhase) {
        if (angleDeltaY === 0 || angleDeltaX !== 0 || nativeMomentum) {
            return false
        }

        if (!hasPixelDelta) {
            return true
        }

        let phaseFree = phase === ViewerWheelArea.NoScrollPhase &&
                nativePhase === 0 && nativeMomentumPhase === 0
        if (!phaseFree) {
            return false
        }

        return Math.abs(pixelDeltaY) > 0 && Math.abs(pixelDeltaX) < Math.abs(pixelDeltaY) * 0.2
    }

    function panZoomedImageFromWheel(deltaX, deltaY, recordVelocity) {
        if (flickableArea.zoomFitView) {
            return Qt.point(deltaX, deltaY)
        }

        viewerWheelPanFinishTimer.stop()
        return flickableArea.panBy(deltaX, deltaY, recordVelocity)
    }

    function scheduleWheelPanFallbackFinish() {
        if (!flickableArea.zoomFitView) {
            viewerWheelPanFinishTimer.restart()
        }
    }

    function handleViewerWheel(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers,
                               buttons, hasPixelDelta, inverted, source, deviceType,
                               nativeMomentum, nativePhase, nativeMomentumPhase) {
        let phaseAware = phase !== ViewerWheelArea.NoScrollPhase
        let effectivePhase = nativeMomentum && phase === ViewerWheelArea.ScrollBegin ?
                ViewerWheelArea.ScrollMomentum : phase
        logViewerGesture("wheel phase=" + viewerGesturePhaseName(phase) +
                         " px=(" + viewerGestureNumber(pixelDeltaX) + "," + viewerGestureNumber(pixelDeltaY) + ")" +
                         " angle=(" + viewerGestureNumber(angleDeltaX) + "," + viewerGestureNumber(angleDeltaY) + ")" +
                         " hasPixel=" + hasPixelDelta +
                         " inverted=" + inverted +
                         " source=" + source +
                         " device=" + deviceType +
                         " nativeMomentum=" + nativeMomentum +
                         " nativePhase=" + nativePhase +
                         " nativeMomentumPhase=" + nativeMomentumPhase +
                         " modifiers=" + modifiers +
                         " buttons=" + buttons)

        let deltaX = wheelDeltaPixels(pixelDeltaX, angleDeltaX)
        let deltaY = wheelDeltaPixels(pixelDeltaY, angleDeltaY)

        if (nativeMomentum && !flickableArea.zoomFitView && !viewerNavigationActive &&
                !viewerNavigationCommitAfterAnimation && !viewerNavigationGestureCommitted &&
                !viewerNavigationSuppressMomentum) {
            panZoomedImageFromWheel(deltaX, deltaY, false)
            logViewerGesture("native momentum panned zoomed image deltaX=" + viewerGestureNumber(deltaX) +
                             " deltaY=" + viewerGestureNumber(deltaY))
            return
        }

        if (viewerNavigationSuppressMomentum && !viewerNavigationGestureActive) {
            let physicalScrollRestart = !nativeMomentum &&
                    effectivePhase !== ViewerWheelArea.ScrollMomentum &&
                    effectivePhase !== ViewerWheelArea.ScrollEnd
            if (physicalScrollRestart) {
                clearViewerNavigationResidualSuppression("new physical scroll " +
                                                         viewerGesturePhaseName(effectivePhase))
            }
            else {
                viewerNavigationResidualQuietTimer.restart()
                logViewerGesture("wheel suppressed as residual tail phase=" +
                                 viewerGesturePhaseName(effectivePhase))
                return
            }
        }

        if (nativeMomentum && !viewerNavigationGestureActive) {
            startViewerNavigationResidualSuppression("stray native momentum")
            logViewerGesture("native momentum suppressed")
            return
        }

        if (nativeMomentum && viewerNavigationGestureActive) {
            if (viewerNavigationActive) {
                logViewerGesture("native momentum begins; finishing active gesture")
                finishViewerNavigation()
                endViewerNavigationGesture(false)
                startViewerNavigationResidualSuppression("native momentum")
                logViewerGesture("native momentum tail suppressed after navigation finish")
                return
            }
            if (!flickableArea.zoomFitView) {
                endViewerNavigationGesture(true)
                panZoomedImageFromWheel(deltaX, deltaY, false)
                logViewerGesture("native momentum took over zoom pan deltaX=" + viewerGestureNumber(deltaX) +
                                 " deltaY=" + viewerGestureNumber(deltaY))
                return
            }
            endViewerNavigationGesture(false)
            startViewerNavigationResidualSuppression("native momentum")
            logViewerGesture("native momentum tail suppressed after fit-view gesture")
            return
        }

        if (effectivePhase === ViewerWheelArea.ScrollBegin) {
            if (viewerNavigationCommitAfterAnimation) {
                finishViewerNavigationAnimationNow("new gesture begin")
            }
            else if (viewerNavigationOffsetAnimation.running) {
                finishViewerNavigationAnimationNow("new gesture begin")
            }
            beginViewerNavigationGesture(true, true)
            if (!flickableArea.zoomFitView) {
                flickableArea.beginWheelPan()
            }
            viewerNavigationFinishTimer.stop()
            viewerWheelPanFinishTimer.stop()
            return
        }

        if (effectivePhase === ViewerWheelArea.ScrollEnd) {
            if (!viewerNavigationGestureActive) {
                logViewerGesture("duplicate end ignored")
                return
            }
            let hadActiveNavigation = viewerNavigationActive
            if (hadActiveNavigation) {
                finishViewerNavigation()
            }
            else if (!flickableArea.zoomFitView) {
                scheduleWheelPanFallbackFinish()
            }
            endViewerNavigationGesture(false)
            if (hadActiveNavigation || flickableArea.zoomFitView) {
                startViewerNavigationResidualSuppression("phase end")
                logViewerGesture("phase end suppressing following momentum")
            }
            else {
                logViewerGesture("phase end waiting briefly for zoom pan momentum")
            }
            return
        }

        if (effectivePhase === ViewerWheelArea.ScrollMomentum &&
                (viewerNavigationSuppressMomentum || !viewerNavigationGestureActive)) {
            startViewerNavigationResidualSuppression("stray momentum")
            logViewerGesture("momentum suppressed")
            return
        }

        if (viewerNavigationCommitAfterAnimation) {
            logViewerGesture("wheel ignored while commit animation is running")
            return
        }

        if (isLegacyWheelImageSwitch(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY,
                                     phase, hasPixelDelta, nativeMomentum,
                                     nativePhase, nativeMomentumPhase)) {
            logViewerGesture("legacy vertical wheel path hasPixel=" + hasPixelDelta)
            if (viewerNavigationOffsetAnimation.running) {
                finishViewerNavigationAnimationNow("legacy wheel")
            }
            endViewerNavigationGesture(true)
            flickableArea.cancelWheelPan()
            switchImageForLegacyWheel(angleDeltaY)
            return
        }

        continueViewerNavigationGesture(phaseAware)

        if (viewerNavigationGestureCommitted) {
            logViewerGesture("wheel ignored after commit")
            return
        }

        let horizontalIntent = viewerNavigationActive || Math.abs(deltaX) >= Math.abs(deltaY) * 0.6

        if (!horizontalIntent) {
            logViewerGesture("vertical intent deltaX=" + viewerGestureNumber(deltaX) +
                             " deltaY=" + viewerGestureNumber(deltaY))
            if (!flickableArea.zoomFitView) {
                panZoomedImageFromWheel(0, deltaY, true)
                if (!phaseAware) {
                    scheduleWheelPanFallbackFinish()
                }
            }
            return
        }

        if (!viewerNavigationActive && !flickableArea.zoomFitView) {
            let leftover = panZoomedImageFromWheel(deltaX, deltaY, true)
            logViewerGesture("zoom pan first deltaX=" + viewerGestureNumber(deltaX) +
                             " deltaY=" + viewerGestureNumber(deltaY) +
                             " leftoverX=" + viewerGestureNumber(leftover.x) +
                             " leftoverY=" + viewerGestureNumber(leftover.y))
            deltaX = leftover.x
        }

        if (Math.abs(deltaX) < 0.1) {
            logViewerGesture("horizontal delta consumed")
            if (!phaseAware) {
                scheduleWheelPanFallbackFinish()
            }
            return
        }

        let now = Date.now()
        let dt = viewerNavigationLastTime ? Math.max(1, now - viewerNavigationLastTime) : 16
        viewerNavigationVelocityX = deltaX / dt * 1000
        viewerNavigationLastTime = now

        applyViewerNavigationDelta(deltaX)
        if (!phaseAware) {
            viewerNavigationFinishTimer.restart()
        }
    }

    function beginShiftSelection() {
        if (shiftSelectionActive) {
            return
        }
        shiftSelectionActive = true
        shiftSelectionAnchorIndex = masonryLayout.view.currentIndex
        shiftNavigationSelectionValue = !fileListModel.isIndexSelected(sourceIndexForViewIndex(shiftSelectionAnchorIndex))
        fileListModel.beginSelectionPreview()
    }

    function updateShiftNavigationSelection(targetIndex) {
        beginShiftSelection()
        fileListModel.previewSelectionIndexes(
                    galleryViewModel.sourceRowsForViewRange(shiftSelectionAnchorIndex, targetIndex, false),
                    shiftNavigationSelectionValue ? 0 : 1)
    }

    function finishShiftSelection() {
        if (!shiftSelectionActive) {
            return
        }
        fileListModel.commitSelectionPreview(shiftNavigationSelectionValue ? "Range selection" : "Range deselection")
        shiftSelectionActive = false
        shiftSelectionAnchorIndex = -1
    }

    function clearPreviousImage() {
        previousImageIndex = -1
        previousImageLocked = false
        lockedPreviousReturnIndex = -1
    }

    function togglePreviousImageLock(index) {
        if (previousImageLocked) {
            if (previousImageIndex === index) {
                previousImageLocked = false
                previousImageIndex = lockedPreviousReturnIndex
                lockedPreviousReturnIndex = -1
            } else {
                lockedPreviousReturnIndex = previousImageIndex
                previousImageIndex = index
            }
            return
        }

        lockedPreviousReturnIndex = previousImageIndex !== index ? previousImageIndex : -1
        previousImageIndex = index
        previousImageLocked = true
    }

    function switchToPreviousImage(currentIndex) {
        if (previousImageLocked) {
            if (currentIndex === previousImageIndex) {
                if (lockedPreviousReturnIndex !== -1) {
                    masonryLayout.setCurrentIndex(lockedPreviousReturnIndex)
                    return lockedPreviousReturnIndex
                }
                return -1
            }

            lockedPreviousReturnIndex = currentIndex
        }

        // Capture the target before setCurrentIndex(), since changing the index
        // synchronously reassigns previousImageIndex via the view's onCurrentIndexChanged handler.
        let targetIndex = previousImageIndex
        masonryLayout.setCurrentIndex(targetIndex)
        return targetIndex
    }

    Connections {
        target: root
        function onStateChanged() {
            if (root.state === "thumbnails") {
                if (!previousImageLocked) {
                    previousImageIndex = -1
                }
                resetViewerNavigation()
                endViewerNavigationGesture()
                flickableArea.rotationMode = 0
            }
        }
    }

    Connections {
        target: viewerController
        function onCurrentPathChanged() {
            clearPreviousImage()
            resetViewerNavigation()
            endViewerNavigationGesture()
        }
    }

    Keys.onPressed:
        (event) => {
            let nextIndex = -1
            let currentIndex = masonryLayout.view.currentIndex
            if (event.key === Qt.Key_Shift && !event.isAutoRepeat) {
                beginShiftSelection()
                return
            }
            if (event.key === Qt.Key_Backslash) {
                toggleCurrentSelection()
            }
            else if (event.key === Qt.Key_Insert) {
                fileListModel.setSelection(sourceIndexForViewIndex(currentIndex), true)
            }
            else if (event.key === Qt.Key_Delete) {
                fileListModel.setSelection(sourceIndexForViewIndex(currentIndex), false)
            }
            else if (!zoomFitView && (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up ||
                                 event.key === Qt.Key_Down) ||
                                 event.key === Qt.Key_Plus || event.key === Qt.Key_Minus || event.key === Qt.Key_Equal ||
                                 event.key === Qt.Key_Control) {
                if (event.isAutoRepeat) {
                    return
                }
                if (event.key === Qt.Key_Left) {
                    leftPressed = true
                }
                else if (event.key === Qt.Key_Right) {
                    rightPressed = true
                }
                else if (event.key === Qt.Key_Up) {
                    upPressed = true
                }
                else if (event.key === Qt.Key_Down) {
                    downPressed = true
                }
                else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                    zoomInPressed = true
                }
                else if (event.key === Qt.Key_Minus) {
                    zoomOutPressed = true
                }
                else if (event.key === Qt.Key_Control) {
                    controlPressed = true
                }

                let speed = controlPressed ? 0.06 : 1
                flickableArea.startZoomScrollingAnimation(leftPressed ? speed : rightPressed ? -speed : 0,
                                                          upPressed ? speed : downPressed ? -speed : 0,
                                                          zoomInPressed ? speed : zoomOutPressed ? -speed : 0)
            }
            else if ((event.key === Qt.Key_Left || event.key === Qt.Key_PageUp || event.key === Qt.Key_Backspace ||
                 event.key === Qt.Key_Up) && !(event.modifiers & Qt.AltModifier)) {
                nextIndex = masonryLayout.moveInImageList(false, false)
            }
            else if ((event.key === Qt.Key_Right || event.key === Qt.Key_PageDown || event.key === Qt.Key_Space ||
                      event.key === Qt.Key_Down) && !(event.modifiers & Qt.AltModifier)) {
                nextIndex = masonryLayout.moveInImageList(true, false)
            }
            else if (event.key === Qt.Key_Home) {
                nextIndex = masonryLayout.moveInImageList(false, true)
            }
            else if (event.key === Qt.Key_End) {
                nextIndex = masonryLayout.moveInImageList(true, true)
            }
            else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F || event.key === Qt.Key_Clear /*Num_5*/ ||
                     (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier)) {
                topLevelWindow.toggleFullscreen()
            }
            else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Escape ||
                     event.key === Qt.Key_Up && (event.modifiers & Qt.AltModifier) ||
                     event.key === Qt.Key_PageUp && (event.modifiers & Qt.ControlModifier)) {
                root.toggleViewer()
            }
            else if (event.key === Qt.Key_Asterisk || event.key === Qt.Key_9) {
                flickableArea.zoomTo100()
            }
            else if (event.key === Qt.Key_1 && (event.modifiers & Qt.ControlModifier)) {
                flickableArea.zoomTo100()
            }
            else if (event.key === Qt.Key_2 && (event.modifiers & Qt.ControlModifier)) {
                flickableArea.zoomToScale(0.5)
            }
            else if (event.key === Qt.Key_3 && (event.modifiers & Qt.ControlModifier)) {
                flickableArea.zoomToScale(0.25)
            }
            else if (event.key === Qt.Key_0 && (event.modifiers & Qt.ControlModifier)) {
                flickableArea.zoomToFit()
            }
            else if (event.key === Qt.Key_Z || event.key === Qt.Key_Slash || event.key === Qt.Key_0) {
                flickableArea.toggleZoomToFit()
            }
            else if (event.key === Qt.Key_Tab) {
                panelsVisible = !panelsVisible
            }
            else if (isTildeKey(event)) {
                if (event.modifiers & Qt.ShiftModifier) {
                    togglePreviousImageLock(currentIndex)
                    return
                }

                if (previousImageIndex !== -1) {
                    nextIndex = switchToPreviousImage(currentIndex)
                } else {
                    let potentialNext = masonryLayout.view.nextImageIndex(true, false)
                    if (potentialNext !== currentIndex) {
                        nextIndex = masonryLayout.moveInImageList(true, false)
                    } else {
                        nextIndex = masonryLayout.moveInImageList(false, false)
                    }
                }
            }
            else if (event.key === Qt.Key_S || event.key === Qt.Key_P) {
                console.log("ZZ SP")
                sphericViewerMode = !sphericViewerMode
            }
            else if (event.key === Qt.Key_BracketRight) {
                flickableArea.rotate(1)
                updateTitle()
            }
            else if (event.key === Qt.Key_BracketLeft) {
                flickableArea.rotate(3)
                updateTitle()
            }
            else if (event.key === Qt.Key_C) {
                console.log("ZZ F12")
                fileListModel.dumpCurrentImage()
            }

            if (nextIndex !== -1 && nextIndex !== currentIndex) {
                if (event.modifiers & Qt.ShiftModifier) {
                    updateShiftNavigationSelection(nextIndex)
                }
                onCurrentIndexChanged()
            }
    }

    Keys.onReleased:
        (event) => {
            if (!event.isAutoRepeat) {
                if (event.key === Qt.Key_Left) {
                    leftPressed = false
                }
                else if (event.key === Qt.Key_Right) {
                    rightPressed = false
                }
                else if (event.key === Qt.Key_Up) {
                    upPressed = false
                }
                else if (event.key === Qt.Key_Down) {
                    downPressed = false
                }
                else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                    zoomInPressed = false
                }
                else if (event.key === Qt.Key_Minus) {
                    zoomOutPressed = false
                }
                else if (event.key === Qt.Key_Control) {
                    controlPressed = false
                }
                else if (event.key === Qt.Key_Shift) {
                    finishShiftSelection()
                }
            }

            if (!zoomFitView && (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up ||
                                 event.key === Qt.Key_Down ||
                                 event.key === Qt.Key_Plus || event.key === Qt.Key_Minus || event.key === Qt.Key_Equal ||
                                 event.key === Qt.Key_Control)) {
                if (event.isAutoRepeat) {
                    return
                }

                let speed = controlPressed ? 0.06 : 1
                flickableArea.startZoomScrollingAnimation(leftPressed ? speed : rightPressed ? -speed : 0,
                                                      upPressed ? speed : downPressed ? -speed : 0,
                                                      zoomInPressed ? speed : zoomOutPressed ? -speed : 0)
                if (event.key === Qt.Key_Control) {
                    if (!leftPressed && !rightPressed && !upPressed && !downPressed && !zoomInPressed && !zoomOutPressed) {
                        flickableArea.onControlReleased()
                    }
                }
            }
        }

    Connections {
        target: masonryLayout.view
        function onImageCountChanged() {
            if (root.state === "viewer") {
                viewerMode.updateTitle()
            }
        }

        function onCurrentImageIndexChanged() {
            if (root.state === "viewer") {
                viewerMode.updateTitle()
            }
        }

        function onCurrentIndexChanged() {
            if (root.state === "viewer") {
                if (lastKnownIndex !== -1 && lastKnownIndex !== masonryLayout.view.currentIndex) {
                    if (previousImageLocked) {
                        if (masonryLayout.view.currentIndex !== previousImageIndex) {
                            lockedPreviousReturnIndex = masonryLayout.view.currentIndex
                        }
                    } else {
                        previousImageIndex = lastKnownIndex
                    }
                }
                lastKnownIndex = masonryLayout.view.currentIndex

                let imageIdUrl = masonryLayout.view.indexImageIdUrl(masonryLayout.view.currentIndex)
                // console.log("ZZ INDEX CHANGE 2", masonryLayout.view.currentIndex, imageIdUrl)
                if (imageIdUrl) {
                    setImage(imageIdUrl, masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex), masonryLayout.view.currentIndex, 0)
                    if (zoomFitView) {
                        flickableArea.zoomToFit(true)
                        // console.log("ZZ FIT ON CHANGE")
                    }
                    else {
                        flickableArea.fitViewerImageInViewportBounds()
                        // console.log("ZZ ELSE")
                    }
                }
            }
            else {
                lastKnownIndex = masonryLayout.view.currentIndex
            }
        }
    }

    Connections {
        target: fileListModel
        function onViewerImageIdUrlChanged(newImageIdUrl, level) {
            viewerMode.setImage(newImageIdUrl, masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex), masonryLayout.view.currentIndex, level)
        }

        function onViewerImageCacheChanged(index) {
            if (index === sourceIndexForViewIndex(viewerNavigationTargetIndex)) {
                updateViewerNavigationTargetSource()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#ffd23a"
        opacity: 0.16 * viewerMode.viewerBackgroundOpacity * viewerMode.selectionHighlightNavigationOpacity
        visible: opacity > 0
        z: -2

        Behavior on opacity {
            enabled: !viewerMode.selectionHighlightAnimationSuppressed &&
                     !viewerNavigationActive &&
                     root.state === "viewer" &&
                     viewerMode.viewerBackgroundOpacity >= 0.999
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

            // visible: !sphericViewerMode
            width: parent.width
            height: parent.height
            animationDuration: viewerMode.animationDuration
            scrollBarsRightMargin: panelsVisible ? rightPanel.width : 0
            hideVerticalScrollBar: viewerNavigationActive || viewerNavigationOffsetAnimation.running ||
                    viewerNavigationCommitAfterAnimation || Math.abs(viewerNavigationOffsetX) > 0.1
            pinchZoomEnabled: !sphericViewerMode
            opacity: viewerNavigationCurrentOpacity
            transform: Translate { x: viewerNavigationCurrentOffsetX }
            onPinchZoomOutToThumbnailsProgressed: (progress) =>
                    viewerMode.pinchZoomOutToThumbnailsProgressed(progress)
            onPinchZoomOutToThumbnailsFinished: (commit) =>
                    viewerMode.pinchZoomOutToThumbnailsFinished(commit)

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

                    text: masonryLayout.view.indexText(masonryLayout.view.currentIndex)
                    textFormat: masonryLayout.quickSearchMode ? Text.RichText : Text.PlainText

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
            topRightRadius: 8 * (1 - topPanel.backgroundOpacity)
        }

        RectangleShadow {
            baseItem: rightPanel
            topLeftRadius: 8 * (1 - topPanel.backgroundOpacity)
            bottomLeftRadius: rightPanel.listContentsFitScreen ? 8 : 0
        }

        RectangleShadow {
            baseItem: topPanel
            opacity: topPanel.backgroundOpacity
        }
    }

    Loader {
        id: sphericViewerLoader
        x: flickableArea.x
        y: flickableArea.y
        width: flickableArea.width
        height: flickableArea.height
        opacity: viewerNavigationCurrentOpacity
        transform: Translate { x: viewerNavigationCurrentOffsetX }
    }

    Component {
        id: sphericViewerComponent

        SphericViewer {
            originalSize: flickableArea.originalSize
            source: flickableArea.textureSource
            opacity: sphericViewerOpacity
        }
    }

    Item {
        id: viewerNavigationNeighbor
        x: 0
        y: 0
        width: viewerMode.width
        height: viewerMode.height
        z: viewerNavigationDirection < 0 ? 1 : -1
        opacity: viewerNavigationTargetOpacity
        visible: opacity > 0 &&
                 viewerNavigationActive &&
                 viewerNavigationTargetIndex !== -1 && viewerNavigationTargetSource !== ""

        Image {
            x: viewerNavigationTargetImageX
            y: viewerNavigationTargetImageY
            width: viewerNavigationTargetDisplayWidth
            height: viewerNavigationTargetDisplayHeight
            source: viewerNavigationTargetSource
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            mipmap: true
        }
    }

    Timer {
        id: viewerNavigationFinishTimer
        interval: 140
        onTriggered: finishViewerNavigation()
    }

    Timer {
        id: viewerWheelPanFinishTimer
        interval: 70
        onTriggered: flickableArea.finishWheelPan()
    }

    Timer {
        id: viewerNavigationGestureEndTimer
        interval: 350
        onTriggered: endViewerNavigationGesture()
    }

    Timer {
        id: viewerNavigationResidualQuietTimer
        interval: 180
        onTriggered: clearViewerNavigationResidualSuppression("quiet")
    }

    NumberAnimation {
        id: viewerNavigationOffsetAnimation
        target: viewerMode
        property: "viewerNavigationOffsetX"
        easing.type: viewerMode.easingType

        onFinished: {
            if (viewerNavigationCommitAfterAnimation) {
                commitViewerNavigation()
            }
            else {
                resetViewerNavigation()
                flickableArea.settlePan()
            }
        }
    }

    ViewerWheelArea {
        id: viewerWheelArea
        anchors.fill: parent
        enabled: root.state === "viewer"
        z: 2

        onWheelReceived:
            (pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers, buttons,
             hasPixelDelta, inverted, source, deviceType, nativeMomentum, nativePhase, nativeMomentumPhase) => {
                handleViewerWheel(pixelDeltaX, pixelDeltaY, angleDeltaX, angleDeltaY, phase, modifiers,
                                  buttons, hasPixelDelta, inverted, source, deviceType,
                                  nativeMomentum, nativePhase, nativeMomentumPhase)
            }

        onWheelForwarded: {
            logViewerGesture("wheel forwarded to zoom/drag handler")
            finishViewerNavigation()
        }
    }

    MouseArea {
        id: viewerMouse
        anchors.fill: parent
        enabled: root.state === "viewer" // && zoomFitView

        acceptedButtons: Qt.LeftButton | Qt.MiddleButton

        onPressed:
            (mouse) => {
                if (mouse.button === Qt.MiddleButton) {
                    topLevelWindow.toggleFullscreen()
                }
                else if (mouse.button === Qt.LeftButton) {
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
            if (root.state === "thumbnails") {
                viewerMode.visible = false
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
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationY
            target: flickableArea
            property: "y"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationWidth
            target: flickableArea
            property: "width"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            id: viewerMaximizeAnimationHeight
            target: flickableArea
            property: "height"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
        }

        NumberAnimation {
            target: delegateOutline
            property: "opacity"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
            to: root.state === "viewer" ? 0 : 1
        }

        NumberAnimation {
            target: imageInfoPanel
            property: "opacity"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
            to: root.state === "viewer" ? 0 : 1
        }

        NumberAnimation {
            target: viewerMode
            property: "sphericViewerOpacity"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType
            to: root.state === "viewer" ? 1 : 0
        }

        onFinished: {
            if (root.state === "thumbnails") {
                viewerMode.visible = false
            }
            else {
                // image.x = Qt.binding(function() {return zoomFitView ? 0 : zoomCenterOffsetX})
                // image.y = Qt.binding(function() {return zoomFitView ? 0 : zoomCenterOffsetY})
                flickableArea.width = Qt.binding(() => {return viewerMode.width})
                flickableArea.height = Qt.binding(() => {return viewerMode.height})
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

    Item {
        id: topPanel

        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: titleBar.viewerHeight

        opacity: viewerMode.viewerChromeOpacity
        visible: opacity !== 0
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
        }
        property bool hovered: false
        property alias backgroundOpacity: topBarBackground.opacity

        property string fileName: {
            let rotationStr = ""
            if (flickableArea.rotationMode === 1) rotationStr = " [90°]"
            else if (flickableArea.rotationMode === 2) rotationStr = " [180°]"
            else if (flickableArea.rotationMode === 3) rotationStr = " [270°]"
            return masonryLayout.view.indexText(masonryLayout.view.currentIndex) + rotationStr
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
            opacity: topPanel.hovered ? viewerMode.viewerChromeOpacity : 0
            visible: opacity !== 0
            Behavior on opacity {
                NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
            }
            maskSource: topBarRect
        }

        Timer {
            repeat: true
            running: root.state === "viewer"
            interval: 50
            onTriggered: {
                let pos = topLevelWindow.mousePos()
                pos = titleBar.mapFromGlobal(pos.x, pos.y)
                let containsPos = pos.x >= titleBar.x && pos.y >= titleBar.y && pos.x <= titleBar.x + titleBar.width && pos.y <= titleBar.y + titleBar.height
                topPanel.hovered = topLevelWindow.isPressedOnTitleBar() && containsPos
            }
        }

        Item {
            id: topPanelRowContainer
            anchors.fill: parent

            RowLayout {
                id: topPanelRow
                anchors {
                    left: parent.left
                    leftMargin: topLevelWindow.macTitleBarLeftPadding
                    top: parent.top
                    right: parent.right
                    rightMargin: topLevelWindow.useMacNativeTitleBar ? 0 : titleBarButtonsRow.width
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
                    icon.source: "qrc:/resources/Sphere.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: Style.viewerMainText

                    visible: sphericViewerMode
                }

                Text {
                    id: text2
                    Layout.leftMargin: sphericViewerMode ? 5 : 13
                    Layout.rightMargin: 5
                    verticalAlignment: Text.AlignVCenter
                    Layout.bottomMargin: 1
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth

                    text: (sphericViewerMode ? (sphericViewerLoader.item ? (Math.round(sphericViewerLoader.item.fovVisual) + "°") : "") : ((zoomFitView ? "* " : "") + (Math.round(flickableArea.zoomScale * 100) + "%"))) +
                          " " + fileListModel.selectedCount
                    font.pixelSize: 14
                    color: Style.viewerMainText
                }

                Item {
                    id: previousLockIndicator

                    Layout.leftMargin: 8
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: titleBar.viewerHeight

                    visible: previousImageLocked && previousImageIndex !== -1

                    IconLabel {
                        anchors.centerIn: parent

                        icon.source: "qrc:/resources/TildeLock.svg"
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

                                source: lockedPreviousImageIdUrl
                                sourceSize.width: 96
                                sourceSize.height: 96
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                            }

                            Text {
                                Layout.maximumWidth: 260

                                text: lockedPreviousPath
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
                    implicitHeight: titleBar.viewerHeight

                    icon.source: "qrc:/resources/Settings.svg"
                    onClicked: panelsVisible = !panelsVisible
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
                    visible: !topLevelWindow.useMacNativeTitleBar
                }
            }

            Row {
                id: titleBarButtonsRow
                anchors {
                    right: parent.right
                    top: parent.top
                }
                visible: !topLevelWindow.useMacNativeTitleBar

                TitleProxyButton {
                    proxyControl: minButton
                    backgroundVisible: false
                }

                TitleProxyButton {
                    proxyControl: maxButton
                    backgroundVisible: false
                }

                TitleProxyButton {
                    proxyControl: closeButton
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
            visible: !topLevelWindow.useMacNativeTitleBar

            TitleProxyButton {
                proxyControl: minButton
                contentVisible: false
            }

            TitleProxyButton {
                proxyControl: maxButton
                contentVisible: false
            }

            TitleProxyButton {
                proxyControl: closeButton
                contentVisible: false
            }
        }
    }


    MouseArea {
        id: rightPanel
        anchors {
            top: parent.top
            topMargin: isQWK ? titleBar.viewerHeight : 0
            right: parent.right
        }
        width: 120
        property int fullContentHeight: filmstrip.count * (57 + filmstrip.spacing) + filmstrip.spacing
        height: Math.min(fullContentHeight, parent.height - y)

        property bool viewerOverlapsFilmstrip: x < flickableArea.image.x + flickableArea.image.width

        property bool listContentsFitScreen: rightPanel.fullContentHeight < rightPanel.parent.height - rightPanel.y

        opacity: (panelsVisible || rightPanel.containsMouse) ? viewerMode.viewerChromeOpacity : 0
        visible: opacity !== 0
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
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

            model: imageModel

            Connections {
                target: masonryLayout.view

                function onCurrentIndexChanged() {
                    filmstrip.positionViewAtIndex(imageModel.mapFromSourceRow(masonryLayout.view.currentIndex), ListView.Center)
                }
            }


            delegate: Item {
                width: 86
                height: 57

                property bool isCurrent: imageModel.mapFromSourceRow(masonryLayout.view.currentIndex) === index

                Image {
                    id: filmstripImage

                    width: parent.width
                    height: parent.height
                    source: model.imageIdUrlRole

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
                    property var viewportSize: Qt.size(width * dpr, height * dpr)
                    property real sharpenAmount: 2
                    property bool showCheckerboard: masonryLayout.view.showTransparentGrid
                    property int checkerboardSize: 4 * dpr
                    property real borderRadius: 4.1 * dpr

                    fragmentShader: "qrc:/resources/shader.frag.qsb"
                    visible: filmstripImage.source != ""
                }

                /*Image {
                    id: thumbnailImage
                    source: model.imageIdUrlRole
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
                    visible: model.selectedRole

                    color: "transparent"
                    border.width: 3
                    border.color: Style.persistentSelectionBorder
                    radius: 4
                    z: 3
                }

                MouseArea {
                    id: thumbnailMouse
                    anchors.fill: parent

                    hoverEnabled: true

                    onClicked: {
                        masonryLayout.setCurrentIndex(imageModel.mapToSourceRow(index))
                        onCurrentIndexChanged()
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
            to: masonryLayout.view.imageCount - 1
            value: masonryLayout.view.currentImageIndex
            stepSize: 1
            snapMode: Slider.SnapAlways

            rotation: 90
            transformOrigin: Item.TopLeft

            onValueChanged: {
                if (pressed) {
                    masonryLayout.view.currentImageIndex = Math.round(value)
                    masonryLayout.setCurrentIndex(masonryLayout.view.currentIndex)
                    onCurrentIndexChanged()
                }
            }

            Connections {
                target: masonryLayout.view
                function onCurrentImageIndexChanged() {
                    currentImageSlider.value = masonryLayout.view.currentImageIndex
                }
            }
        }
    }

    MouseArea {
        id: leftPanel
        anchors {
            top: parent.top
            topMargin: titleBar.viewerHeight
            left: parent.left
        }
        width: 180
        height: exifLayout.height > 0 ? (exifLayout.height + 10) : 0

        opacity: (panelsVisible || leftPanel.containsMouse) ? viewerMode.viewerChromeOpacity : 0
        visible: opacity !== 0
        Behavior on opacity {
            NumberAnimation { duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
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
            color: "transparent" // width < flickableArea.image.x || height < flickableArea.image.y ? Style.viewerPanel : Style.opaqueMasonryViewBackgroundWithOpacity
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
                model: masonryLayout.view.currentImageExif
                delegate: RowLayout {
                    property bool isTitle: modelData.title !== undefined

                    Layout.topMargin: !index ? 10 : isTitle ? 15 : 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: exifText.height + 2

                    spacing: 7

                    IconLabel {
                        visible: modelData.icon !== undefined
                        icon.source: modelData.icon !== undefined ? modelData.icon : ""
                        icon.width: 15
                        icon.height: 15
                        icon.color: Style.viewerSecondaryText
                    }

                    Text {
                        id: exifText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: modelData.url !== undefined ? modelData.text.replace(" ", "\n") : modelData.text
                        wrapMode: modelData.multiline !== undefined ? Text.Wrap : Text.NoWrap
                        color: !isTitle ? Style.viewerMainText : Style.viewerSecondaryText
                        font.pixelSize: 16
                        font.underline: modelData.url !== undefined

                        MouseArea {
                            id: exifMouse
                            anchors.fill: parent

                            hoverEnabled: true
                            cursorShape: modelData.url !== undefined ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onClicked: {
                                if (modelData.url !== undefined) {
                                    Qt.openUrlExternally(modelData.url)
                                }
                            }
                        }

                        ToolTip {
                            id: tooltip

                            visible: exifMouse.containsMouse && exifText.implicitWidth > exifText.width
                            text: modelData.text
                        }
                    }
                }
            }
        }
    }
}
