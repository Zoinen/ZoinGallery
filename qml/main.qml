import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import Qt.labs.platform as Platform

import ZoinGallery.MainWindow 1.0

import QWindowKit 1.0

MainWindow {
    id: topLevelWindow
    // visible: true
    property bool isQWKLegacy: false
    property bool windowAgentReady: false
    property int macWindowEffectApplyAttempts: 0
    readonly property bool supportsTransparentWindowBackground: isQWK && (Qt.platform.os === "windows" || Qt.platform.os === "osx")
    readonly property bool useTransparentWindowBackground: supportsTransparentWindowBackground && !isQWKLegacy
    readonly property bool useMacNativeTitleBar: isQWK && Qt.platform.os === "osx"
    readonly property string macWindowGlassEffect: useMacNativeTitleBar ? "regular" : "none"
    readonly property string macWindowFallbackBlurEffect: useMacNativeTitleBar ? (Style.isDarkTheme ? "dark" : "light") : "none"
    readonly property int macTitleBarLeftPadding: useMacNativeTitleBar && visibility !== Window.FullScreen ? 86 : 0
    readonly property int macSystemButtonAreaLeftMargin: 12
    readonly property int thumbnailsTitleBarSidePadding: 8
    color: useTransparentWindowBackground ? "transparent" : (isQWKLegacy ? Style.windowBackgroundQWKLegacy : Style.windowBackgroundNoQWK)
    title: "Zoin Gallery"

    property bool viewerDirty: false
    property bool thumbnailsDirty: false

    onClosing: (closeEvent) => {
        console.info("[Shutdown] QML onClosing explicitQuitRequested=" + viewerController.explicitQuitRequested
                     + " backgroundMode=" + viewerController.backgroundMode
                     + " visible=" + topLevelWindow.visible
                     + " visibility=" + topLevelWindow.visibility)
        if (viewerController.explicitQuitRequested) {
            console.info("[Shutdown] QML onClosing accepting explicit quit")
            return
        }

        closeEvent.accepted = false
        console.info("[Shutdown] QML onClosing hiding window and keeping close event unaccepted")
        topLevelWindow.hide()
        if (viewerController.backgroundMode) {
            console.info("[Shutdown] QML onClosing calling hideToTray")
            viewerController.hideToTray()
        }
        else {
            console.info("[Shutdown] QML onClosing calling prepareToClose")
            viewerController.prepareToClose()
        }
    }

    // CacheViewer {
    // }

    WindowAgent {
        id: windowAgent
    }

    Component.onCompleted: {
        windowAgent.setup(topLevelWindow)
        windowAgentReady = true

        if (Qt.platform.os === "windows") {
            isQWKLegacy = windowAgent.setWindowAttribute("mica-alt", true) !== true
        } else if (useMacNativeTitleBar) {
            applyPlatformWindowEffects()
        }
    }

    onMacWindowFallbackBlurEffectChanged: schedulePlatformWindowEffects()

    Timer {
        id: macWindowEffectRetryTimer
        interval: 100
        repeat: false
        onTriggered: applyPlatformWindowEffects()
    }

    function schedulePlatformWindowEffects() {
        if (!windowAgentReady) {
            return
        }
        macWindowEffectApplyAttempts = 0
        applyPlatformWindowEffects()
    }

    function applyPlatformWindowEffects() {
        if (!windowAgentReady) {
            return
        }
        if (useMacNativeTitleBar) {
            windowAgent.setWindowAttribute("blur-effect", "none")
            windowAgent.setWindowAttribute("glass-corner-radius", 0)
            windowAgent.setWindowAttribute("glass-tint-color", "none")

            const glassApplied = windowAgent.setWindowAttribute("glass-effect", macWindowGlassEffect) === true
            const applied = glassApplied || windowAgent.setWindowAttribute("blur-effect", macWindowFallbackBlurEffect) === true
            isQWKLegacy = !applied
            if (!applied && macWindowEffectApplyAttempts < 10) {
                macWindowEffectApplyAttempts += 1
                macWindowEffectRetryTimer.restart()
            }
        }
    }

    Connections {
        target: topLevelWindow

        function onMainWindowResized(isWidthChanged) {
            Qt.callLater(() => {
            console.log("main window resized", topLevelWindow.width, topLevelWindow.height)
            if (root.state === "thumbnails") {
                if (isWidthChanged) {
                    masonryLayout.view.reReadAndDecodeThumbnails()
                }
                viewerDirty = true
            }
            else {
                if (viewerMode.zoomFitView && !viewerMode.sphericViewerMode) {
                    // console.log("onMainWindowResized")
                    viewerMode.imageContainer.zoomToFit(true)
                    fileListModel.cancelAllDecodeViewerRunners()
                    fileListModel.requestViewer(galleryViewModel.mapToSourceRow(masonryLayout.view.currentIndex),
                                                viewerMode.width * dpr, viewerMode.height * dpr)
                    viewerDirty = false
                }
                else {
                    viewerDirty = true
                }
                thumbnailsDirty = true
            }
            })
        }

        function onWindowIsReady() {
            masonryZoomSlider.updateTargetSize()
            viewerController.initialCd(Math.round(viewerMode.width * dpr), Math.round(viewerMode.height * dpr))
            schedulePlatformWindowEffects()
        }

        function onOpenSettingsRequested() {
            settingsDialog.open()
        }
    }

    Connections {
        target: viewerMode
        function onZoomFitViewChanged() {
            if (viewerMode.zoomFitView && viewerDirty) {
                fileListModel.cancelAllDecodeViewerRunners()
                fileListModel.requestViewer(galleryViewModel.mapToSourceRow(masonryLayout.view.currentIndex),
                                            viewerMode.width * dpr, viewerMode.height * dpr)
                viewerDirty = false
            }
            else {
                fileListModel.cancelAllDecodeViewerRunners()
                fileListModel.requestViewer(galleryViewModel.mapToSourceRow(masonryLayout.view.currentIndex))
            }
        }
    }

    Connections {
        target: viewerController
        function onCurrentPathChanged() {
            masonryZoomSlider.updateTargetSize()
        }

        function onExternalActivateRequested() {
            topLevelWindow.showAndActivate()
        }

        function onExternalFileOpened() {
            viewerController.openPendingExternalFileInViewer(Math.round(viewerMode.width * dpr), Math.round(viewerMode.height * dpr))
            topLevelWindow.showAndActivate()
        }

        function onSetCurrentIndex(index) {
            if (viewerController.pendingOpenInViewer) {
                viewerController.clearPendingOpenInViewer()
                Qt.callLater(() => root.tryOpenExternalInViewer(index))
            }
        }
    }

    Rectangle {
        id: root
        anchors.fill: parent
        color: topLevelWindow.useMacNativeTitleBar ? Style.macGlassWindowColor : Style.windowColor

        property bool viewerPinchCloseActive: false
        property bool viewerPinchCloseReturning: false
        property bool viewerPinchCloseFinishingCommit: false
        property real viewerPinchCloseProgress: 0

        function currentSourceIndex() {
            return galleryViewModel.mapToSourceRow(masonryLayout.view.currentIndex)
        }
        property rect viewerPinchCloseStartGeometry: Qt.rect(0, 0, 0, 0)
        property rect viewerPinchCloseTargetGeometry: Qt.rect(0, 0, 0, 0)
        property bool viewerShowAnimationRunning: viewerMode.animation.running || viewerPinchCloseActive

        onViewerPinchCloseProgressChanged: {
            applyViewerPinchCloseProgress()
            if (viewerPinchCloseFinishingCommit && viewerPinchCloseProgress >= 0.999) {
                Qt.callLater(() => {
                    if (viewerPinchCloseFinishingCommit) {
                        completeViewerPinchClose()
                    }
                })
            }
        }

        function toggleViewer() {
            if (root.state === "thumbnails") {
                if (viewerDirty) {
                    viewerDirty = false
                    console.log("viewer dirty")
                }
                switchToViewer()
            }
            else {
                closeViewer()
            }
        }

        function closeViewer(startGeometry) {
            switchToThumbnails(startGeometry)
            if (thumbnailsDirty) {
                thumbnailsDirty = false
                masonryLayout.view.reReadAndDecodeThumbnails()
            }
            fileListModel.cancelAllDecodeViewerRunners()
        }

        function validGeometry(geometry) {
            return geometry !== undefined && geometry.width > 1 && geometry.height > 1
        }

        function currentThumbnailGeometry() {
            if (!masonryLayout.view.currentItem) {
                return undefined
            }

            return root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())
        }

        function currentThumbnailImageGeometry() {
            let thumbnailGeometry = currentThumbnailGeometry()
            if (!validGeometry(thumbnailGeometry)) {
                return undefined
            }

            return viewerMode.imageContainer.imageRectFittedInRect(thumbnailGeometry)
        }

        function lerp(start, end, progress) {
            return start + (end - start) * progress
        }

        function easeViewerPinchCloseProgress(progress) {
            return Math.sin(Math.max(0, Math.min(1, progress)) * Math.PI / 2)
        }

        function beginViewerPinchClose() {
            if (viewerPinchCloseActive) {
                return true
            }

            if (root.state !== "viewer") {
                return false
            }

            let startGeometry = currentViewerImageGeometry()
            let targetGeometry = currentThumbnailImageGeometry()
            if (!validGeometry(startGeometry) || !validGeometry(targetGeometry)) {
                return false
            }

            viewerMode.animation.stop()
            viewerPinchCloseProgressAnimation.stop()
            viewerPinchCloseFinalizeTimer.stop()
            viewerPinchCloseStartGeometry = startGeometry
            viewerPinchCloseTargetGeometry = targetGeometry
            viewerPinchCloseReturning = false
            viewerPinchCloseFinishingCommit = false
            viewerPinchCloseActive = true
            toolbarLayout.visible = true

            viewerMode.imageContainer.x = 0
            viewerMode.imageContainer.y = 0
            viewerMode.imageContainer.width = Qt.binding(() => viewerMode.width)
            viewerMode.imageContainer.height = Qt.binding(() => viewerMode.height)
            viewerMode.imageContainer.setImageRect(startGeometry)
            return true
        }

        function cancelViewerPinchCloseDuringGesture() {
            viewerPinchCloseProgressAnimation.stop()
            viewerPinchCloseFinalizeTimer.stop()
            viewerPinchCloseActive = false
            viewerPinchCloseReturning = false
            viewerPinchCloseFinishingCommit = false
            viewerPinchCloseProgress = 0
            toolbarLayout.visible = false
            viewerMode.imageContainer.x = 0
            viewerMode.imageContainer.y = 0
            viewerMode.imageContainer.width = Qt.binding(() => viewerMode.width)
            viewerMode.imageContainer.height = Qt.binding(() => viewerMode.height)
        }

        function completeViewerPinchCloseReturn() {
            viewerPinchCloseProgressAnimation.stop()
            viewerPinchCloseFinalizeTimer.stop()
            viewerPinchCloseActive = false
            viewerPinchCloseReturning = false
            viewerPinchCloseFinishingCommit = false
            viewerPinchCloseProgress = 0
            toolbarLayout.visible = false
            viewerMode.imageContainer.zoomToFit()
        }

        function updateViewerPinchClose(progress) {
            if (viewerPinchCloseFinishingCommit) {
                return
            }

            let clampedProgress = Math.max(0, Math.min(1, progress))
            if (clampedProgress <= 0) {
                if (viewerPinchCloseActive) {
                    cancelViewerPinchCloseDuringGesture()
                }
                return
            }

            if (!beginViewerPinchClose()) {
                return
            }

            viewerPinchCloseReturning = false
            viewerPinchCloseFinishingCommit = false
            viewerPinchCloseProgressAnimation.stop()
            viewerPinchCloseFinalizeTimer.stop()
            viewerPinchCloseProgress = clampedProgress
        }

        function applyViewerPinchCloseProgress() {
            if (!viewerPinchCloseActive ||
                    !validGeometry(viewerPinchCloseStartGeometry) ||
                    !validGeometry(viewerPinchCloseTargetGeometry)) {
                return
            }

            let progress = easeViewerPinchCloseProgress(viewerPinchCloseProgress)
            viewerMode.imageContainer.setImageRect(Qt.rect(
                    lerp(viewerPinchCloseStartGeometry.x, viewerPinchCloseTargetGeometry.x, progress),
                    lerp(viewerPinchCloseStartGeometry.y, viewerPinchCloseTargetGeometry.y, progress),
                    lerp(viewerPinchCloseStartGeometry.width, viewerPinchCloseTargetGeometry.width, progress),
                    lerp(viewerPinchCloseStartGeometry.height, viewerPinchCloseTargetGeometry.height, progress)))
        }

        function enterThumbnailsAfterViewerPinchClose() {
            masonryLayout.focusProxy.forceActiveFocus()
            if (root.state !== "thumbnails") {
                root.state = "thumbnails"
            }

            toolbarLayout.visible = true
            if (thumbnailsDirty) {
                thumbnailsDirty = false
                masonryLayout.view.reReadAndDecodeThumbnails()
            }
            fileListModel.cancelAllDecodeViewerRunners()
            viewerMode.panelsVisible = false
            topLevelWindow.title = "ZoinGallery"
            thumbnailsView.opacity = 1
            thumbnailsViewBackground.opacity = 1
            viewerBackground.opacity = 0
            titleBar.opacity = 1
        }

        function completeViewerPinchClose() {
            viewerPinchCloseProgressAnimation.stop()
            viewerPinchCloseFinalizeTimer.stop()
            enterThumbnailsAfterViewerPinchClose()
            viewerMode.visible = false
            viewerPinchCloseActive = false
            viewerPinchCloseReturning = false
            viewerPinchCloseFinishingCommit = false
            viewerPinchCloseProgress = 0
            viewerMode.imageContainer.x = 0
            viewerMode.imageContainer.y = 0
            viewerMode.imageContainer.width = Qt.binding(() => viewerMode.width)
            viewerMode.imageContainer.height = Qt.binding(() => viewerMode.height)
            viewerMode.imageContainer.zoomToFit(true)
            viewerMode.zoomFitView = true
        }

        function finishViewerPinchClose(commit) {
            if (!viewerPinchCloseActive) {
                return
            }

            let targetGeometry = currentThumbnailImageGeometry()
            if (commit && validGeometry(targetGeometry)) {
                viewerPinchCloseReturning = false
                viewerPinchCloseFinishingCommit = true
                viewerPinchCloseTargetGeometry = targetGeometry
                viewerPinchCloseProgressAnimation.stop()
                viewerPinchCloseFinalizeTimer.stop()
                if (viewerPinchCloseProgress >= 0.999) {
                    viewerPinchCloseProgress = 1
                    Qt.callLater(() => {
                        if (viewerPinchCloseFinishingCommit) {
                            completeViewerPinchClose()
                        }
                    })
                    return
                }

                viewerPinchCloseProgressAnimation.to = 1
                viewerPinchCloseProgressAnimation.restart()
                viewerPinchCloseFinalizeTimer.restart()
                return
            }

            completeViewerPinchCloseReturn()
        }

        NumberAnimation {
            id: viewerPinchCloseProgressAnimation
            target: root
            property: "viewerPinchCloseProgress"
            duration: viewerMode.animationDuration
            easing.type: viewerMode.easingType

            onFinished: {
                if (viewerPinchCloseFinishingCommit) {
                    completeViewerPinchClose()
                }
                else if (viewerPinchCloseReturning && root.state === "viewer") {
                    completeViewerPinchCloseReturn()
                }
            }
        }

        Timer {
            id: viewerPinchCloseFinalizeTimer
            interval: viewerMode.animationDuration + 50
            repeat: false
            onTriggered: {
                if (viewerPinchCloseFinishingCommit) {
                    completeViewerPinchClose()
                }
                else if (viewerPinchCloseReturning && root.state === "viewer") {
                    completeViewerPinchCloseReturn()
                }
            }
        }

        function tryOpenExternalInViewer(targetIndex, attempts) {
            if (attempts === undefined) {
                attempts = 0
            }
            if (masonryLayout.view.currentIndex !== targetIndex) {
                if (attempts < 300) {
                    Qt.callLater(() => root.tryOpenExternalInViewer(targetIndex, attempts + 1))
                }
                else {
                    console.warn("External image open did not reach target index", targetIndex,
                                 "current", masonryLayout.view.currentIndex)
                }
                return
            }
            let size = masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex)
            if ((size.width <= 1 || size.height <= 1) && attempts < 300) {
                Qt.callLater(() => root.tryOpenExternalInViewer(targetIndex, attempts + 1))
                return
            }
            root.switchToViewer(false)
        }

        function switchToViewer(animated = true) {
            let currentItem = masonryLayout.view.currentItem
            if (!currentItem || !currentItem.model) {
                return
            }

            viewerPinchCloseProgressAnimation.stop()
            viewerPinchCloseFinalizeTimer.stop()
            viewerPinchCloseActive = false
            viewerPinchCloseReturning = false
            viewerPinchCloseFinishingCommit = false
            viewerPinchCloseProgress = 0
            viewerMode.forceActiveFocus()
            root.state = "viewer"
            viewerMode.zoomFitView = true

            if (animated) {
                if (!viewerMode.animation.running) {
                    let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                    viewerMode.imageContainer.x = mappedGeometry.x
                    viewerMode.imageContainer.y = mappedGeometry.y
                    viewerMode.imageContainer.width = mappedGeometry.width
                    viewerMode.imageContainer.height = mappedGeometry.height
                }
            }
            else {
                viewerMode.imageContainer.x = 0
                viewerMode.imageContainer.y = 0
                viewerMode.imageContainer.width = viewerMode.width
                viewerMode.imageContainer.height = viewerMode.height
            }

            viewerMode.setImage(currentItem.model.imageIdUrl,
                                masonryLayout.view.indexOriginalSize(masonryLayout.view.currentIndex), masonryLayout.view.currentIndex, 0)
            let exif = masonryLayout.view.indexExif(masonryLayout.view.currentIndex)
            viewerMode.show(exif["Panorama"])

            if (animated) {
                viewerMode.animation.x = 0
                viewerMode.animation.y = 0
                viewerMode.animation.width = viewerMode.width
                viewerMode.animation.height = viewerMode.height
                viewerMode.animation.restart()
            }
            else {
                viewerMode.completeInstantOpen()
            }
        }

        function currentViewerImageGeometry() {
            let image = viewerMode.imageContainer.image
            if (image.width <= 1 || image.height <= 1) {
                return undefined
            }

            return root.mapFromItem(viewerMode.imageContainer,
                                    Qt.rect(image.x, image.y, image.width, image.height))
        }

        function switchToThumbnails(startGeometry) {
            masonryLayout.focusProxy.forceActiveFocus()
            root.state = "thumbnails"

            if (masonryLayout.view.currentItem) {
                let mappedGeometry = root.mapFromItem(masonryLayout.view, masonryLayout.currentItemImageGeometry())

                if (startGeometry !== undefined && startGeometry.width > 1 && startGeometry.height > 1) {
                    viewerMode.animation.stop()
                    viewerMode.imageContainer.x = startGeometry.x
                    viewerMode.imageContainer.y = startGeometry.y
                    viewerMode.imageContainer.width = startGeometry.width
                    viewerMode.imageContainer.height = startGeometry.height
                    viewerMode.imageContainer.zoomToFit(true)
                }
                else if (!viewerMode.zoomFitView) {
                    viewerMode.imageContainer.zoomToFit(true) // TODO: Smooth animation
                }

                viewerMode.animation.x = mappedGeometry.x
                viewerMode.animation.y = mappedGeometry.y
                viewerMode.animation.width = mappedGeometry.width
                viewerMode.animation.height = mappedGeometry.height
                viewerMode.animation.restart()
            }

            topLevelWindow.title = "ZoinGallery"
        }

        Item {
            id: titleBar
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            property int viewerHeight: 32
            property int thumbnailsHeight: 48

            height: root.state === "thumbnails" ? thumbnailsHeight : viewerHeight
            Behavior on height {
                NumberAnimation {
                    duration: viewerMode.animationDuration
                    easing.type: viewerMode.easingType
                }
            }

            z: 1
            visible: isQWK

            Item {
                id: macSystemButtonArea
                visible: false
                x: topLevelWindow.macSystemButtonAreaLeftMargin
                y: 0
                width: 70
                height: titleBar.height
            }

            RowLayout {
                id: titleBarButtonsLayout
                anchors {
                    top: parent.top
                    right: parent.right
                    bottom: parent.bottom
                }
                spacing: 0
                visible: !topLevelWindow.useMacNativeTitleBar

                TitleButton {
                    id: minButton

                    Layout.alignment: Qt.AlignTop

                    source: "qrc:/resources/WindowMinimize.svg"
                    onClicked: topLevelWindow.showMinimized()
                    Component.onCompleted: {
                        if (!topLevelWindow.useMacNativeTitleBar) {
                            windowAgent.setSystemButton(WindowAgent.Minimize, minButton)
                        }
                    }
                }

                TitleButton {
                    id: maxButton

                    Layout.alignment: Qt.AlignTop

                    source: topLevelWindow.visibility === Window.Maximized ? "qrc:/resources/WindowRestore.svg" :
                            topLevelWindow.visibility === Window.FullScreen ? "qrc:/resources/WindowFullscreen.svg" :"qrc:/resources/WindowMaximize.svg"
                    onClicked: {
                        if (topLevelWindow.visibility === Window.FullScreen) {
                            topLevelWindow.toggleFullscreen()
                        }
                        else if (topLevelWindow.visibility === Window.Maximized) {
                            topLevelWindow.showNormal()
                        }
                        else {
                            topLevelWindow.showMaximized()
                        }
                    }
                    Component.onCompleted: {
                        if (!topLevelWindow.useMacNativeTitleBar) {
                            windowAgent.setSystemButton(WindowAgent.Maximize, maxButton)
                        }
                    }
                }

                TitleButton {
                    id: closeButton

                    Layout.alignment: Qt.AlignTop

                    source: "qrc:/resources/WindowClose.svg"
                    icon.color: closeButton.hovered ? Style.closeButtonHoveredIcon : Style.text
                    backgroundColor: {
                        if (!closeButton.enabled) {
                            return "gray";
                        }
                        if (closeButton.pressed) {
                            return Style.closeButtonPressed;
                        }
                        if (closeButton.hovered) {
                            return Style.closeButtonHovered;
                        }
                        return "transparent";
                    }
                    onClicked: topLevelWindow.close()

                    Component.onCompleted: {
                        if (!topLevelWindow.useMacNativeTitleBar) {
                            windowAgent.setSystemButton(WindowAgent.Close, closeButton)
                        }
                    }
                }
            }

        }

        Rectangle {
            id: thumbnailsViewBackground
            anchors {
                fill: thumbnailsView
                // margins: 5
                topMargin: titleBar.thumbnailsHeight
                leftMargin: Style.isDarkTheme ? 0 : -1
                rightMargin: Style.isDarkTheme ? 0 : -1
                bottomMargin: Style.isDarkTheme ? 0 : -1
            }
            radius: 7
            border.width: 1
            border.color: Style.masonryViewBackgroundBorder
            color: Style.masonryViewBackground
        }

        Rectangle {
            id: viewerBackground
            anchors.fill: thumbnailsView
            color: thumbnailsViewBackground.color
            opacity: 0
        }


        ColumnLayout {
            id: thumbnailsView
            anchors.fill: parent

            spacing: 0

            Component.onCompleted: {
                windowAgent.setTitleBar(titleBar)
                if (topLevelWindow.useMacNativeTitleBar) {
                    windowAgent.setSystemButtonArea(macSystemButtonArea)
                }

                for (var i = 0; i < titleBarButtonsLayout.children.length; i++) {
                    if (typeof titleBarButtonsLayout.children[i].isPartOfTitleBar === "undefined") {
                        windowAgent.setHitTestVisible(titleBarButtonsLayout.children[i])
                    }
                }

                for (var i = 0; i < toolbarLayout.children.length; i++) {
                    if (typeof toolbarLayout.children[i].isPartOfTitleBar === "undefined") {
                        windowAgent.setHitTestVisible(toolbarLayout.children[i])
                    }
                }
            }

            Item {
                id: toolbar
                Layout.fillWidth: true
                Layout.preferredHeight: titleBar.thumbnailsHeight
                z: 1

                MultiEffect {
                    id: titleBarBlurBehind
                    source: ShaderEffectSource {
                        sourceItem: masonryLayout
                        width: titleBarBlurBehind.width
                        height: titleBarBlurBehind.height
                        sourceRect: Qt.rect(0, -height, width, height)
                    }

                    anchors.fill: parent
                    opacity: 0.3
                    contrast: Style.isDarkTheme ? -0.5 : 0
                    brightness: Style.isDarkTheme ? 0 : 0.3
                    saturation: topLevelWindow.active ? 0 : -1
                    Behavior on saturation {
                        NumberAnimation {
                            duration: 300
                            easing.type: Easing.InOutQuad
                        }
                    }

                    colorization: Style.isDarkTheme ? 0.4 : 0
                    colorizationColor: Style.windowBackgroundNoQWK
                    autoPaddingEnabled: false
                    blurEnabled: true
                    blurMax: 64
                    blur: 1.0
                }

                RowLayout {
                    id: toolbarLayout
                    anchors.fill: parent
                    anchors.leftMargin: topLevelWindow.macTitleBarLeftPadding + topLevelWindow.thumbnailsTitleBarSidePadding
                    anchors.rightMargin: (isQWK && !topLevelWindow.useMacNativeTitleBar ? titleBarButtonsLayout.width : 0) + topLevelWindow.thumbnailsTitleBarSidePadding
                    spacing: 0
                    clip: true

                    component Separator : Rectangle {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        implicitWidth: 1
                        implicitHeight: 32
                        color: Style.lighter2
                    }

                    component ToolbarButton : Button {
                        id: toolbarButton
                        Layout.alignment: Qt.AlignVCenter

                        implicitWidth: 36
                        implicitHeight: titleBar.thumbnailsHeight

                        signal rightReleased

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: toolbarButton.rightReleased()
                        }
                    }

                    Button {
                        id: appIcon
                        implicitWidth: 46
                        implicitHeight: parent.height
                        visible: isQWK && !topLevelWindow.useMacNativeTitleBar

                        icon.width: 18
                        icon.height: 18

                        colorfulIcon: true
                        icon.source: "qrc:/resources/Logo.svg"
                        onClicked: {
                            // windowAgent.showSystemMenu(mapToGlobal(0, height))
                            // fileListModel.startScanner()
                            settingsDialog.open()
                        }
                        // Component.onCompleted: windowAgent.setSystemButton(WindowAgent.WindowIcon, appIcon)
                    }

                    Text {
                        property bool isPartOfTitleBar: true
                        visible: isQWK

                        Layout.rightMargin: 15

                        text: "ZoinGallery"
                        color: Style.text
                        renderType: Text.NativeRendering

                        opacity: topLevelWindow.active ? 1 : 0.5
                        Behavior on opacity {
                            NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/Back.svg"
                        ToolTip.text: "Go Back\tAlt+←"
                        inactive: !viewerController.canBack

                        implicitWidth: 46
                        centerOffset: 5
                        leftPadding: 18

                        onReleased: {
                            if (inactive) {
                                return
                            }

                            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                            viewerController.back()
                            masonryLayout.view.loadSavedState()
                        }

                        onRightReleased: {
                            if (inactive) {
                                return
                            }

                            backMenu.popup()
                        }

                        Menu {
                            id: backMenu

                            Timer {
                                id: delayedBackRightClick
                                property int index: -1
                                interval: 0
                                onTriggered: {
                                    viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                                    viewerController.jumpBack(index)
                                    masonryLayout.view.loadSavedState()
                                }
                            }

                            Repeater {
                                model: viewerController.backMenu

                                MenuItem {
                                    text: modelData
                                    onTriggered: {
                                        delayedBackRightClick.index = index
                                        delayedBackRightClick.start()
                                        masonryLayout.focusProxy.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/Forward.svg"
                        ToolTip.text: "Go Forward\tAlt+→"
                        inactive: !viewerController.canForward

                        onReleased: {
                            if (inactive) {
                                return
                            }

                            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                            viewerController.forward()
                            masonryLayout.view.loadSavedState()
                        }

                        onRightReleased: {
                            if (inactive) {
                                return
                            }

                            forwardMenu.popup()
                        }

                        Menu {
                            id: forwardMenu

                            Timer {
                                id: delayedForwardRightClick
                                property int index: -1
                                interval: 0
                                onTriggered: {
                                    viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                                    viewerController.jumpForward(index)
                                    masonryLayout.view.loadSavedState()
                                }
                            }

                            Repeater {
                                model: viewerController.forwardMenu

                                MenuItem {
                                    text: modelData
                                    onTriggered: {
                                        delayedForwardRightClick.index = index
                                        delayedForwardRightClick.start()
                                        masonryLayout.focusProxy.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/Up.svg"
                        ToolTip.text: "Go Up\tBackspace"
                        inactive: !viewerController.canUp

                        onReleased: {
                            masonryLayout.disableAnimation = true
                            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
                            masonryLayout.setCurrentIndex(viewerController.up())
                            masonryLayout.disableAnimation = false
                            masonryLayout.view.loadSavedState()
                        }
                    }

                    PathControl {
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        clip: true

                        onEditModeChanged: {
                            if (!editMode) {
                                masonryLayout.focusProxy.forceActiveFocus()
                            }
                        }
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32

                        text: viewerController.currentPath
                    }

                    Shortcut {
                        sequence: "F12"
                        onActivated: {
                            fileListModel.runningTasksDebug = !fileListModel.runningTasksDebug
                        }
                    }

                    Component {
                        id: runningTasksDebugView
                        Text {
                            id: runningTasks

                            Connections {
                                target: fileListModel
                                function onRunningTasksChanged(tasks, tasksInfo) {
                                    runningTasks.text = tasks
                                    infoWindow.title = tasks
                                    infoList.model = tasksInfo
                                }
                            }
                            text: "0/0"
                            color: Style.text
                            Layout.preferredWidth: 45
                            Layout.rightMargin: 5
                            horizontalAlignment: Text.AlignRight

                            Window {
                                id: infoWindow
                                x: topLevelWindow.x + topLevelWindow.width
                                y: 20
                                width: 700
                                height: 1000
                                visible: true
                                color: Style.windowBackgroundNoQWK

                                ListView {
                                    id: infoList
                                    anchors {
                                        fill: parent
                                        margins: 10
                                    }
                                    delegate: Text {
                                        height: 12
                                        color: modelData.endsWith(" E") ? "#80ff80" : Style.text
                                        text: modelData
                                    }
                                }
                            }
                        }
                    }

                    Loader {
                        sourceComponent: fileListModel.runningTasksDebug ? runningTasksDebugView : undefined
                    }

                    Slider {
                        id: masonryZoomSlider
                        Layout.preferredWidth: 100
                        implicitHeight: 30 // To work around a warning
                        Layout.preferredHeight: 30
                        Layout.alignment: Qt.AlignVCenter
                        from: 40
                        value: masonryLayout.view.targetHeight
                        to: 500
                        stepSize: 1

                        function updateTargetSize() {
                            if (masonryLayout.view.listView) {
                                // 36 is hardcoded and comes from BrickDelegate's layout folderViewDelegate
                                fileListModel.setFolderViewImageSize(0, (masonryZoomSlider.value - 36) * dpr)
                            }
                            else {
                                // this is taken from commented out actualGridSize item in BrickDelegate

                                let spacing = 2
                                let canvasWidth = (masonryLayout.view.width - 1) - masonryLayout.view.paddingLeft - masonryLayout.view.paddingRight
                                let columns = Math.floor(canvasWidth / masonryLayout.view.targetHeight)
                                let averageCellWidth = canvasWidth / columns
                                // console.log("ZZ COLUMNS", columns, canvasWidth, masonryLayout.view.targetHeight)
                                if (columns >= masonryLayout.view.count) {
                                    // console.log("ZZ TOO FEW" , columns, masonryLayout.view.count)
                                    averageCellWidth = masonryLayout.view.targetHeight
                                }

                                let targetWidth = averageCellWidth - masonryLayout.view.spacing - spacing
                                let targetHeight = averageCellWidth - masonryLayout.view.spacing * 2 - spacing - Math.round(targetWidth/20) - 17

                                let dimensions = targetWidth < 80 ? 1 :
                                                 targetWidth < 150 ? 2 :
                                                 targetWidth < 300 ? 3 : 4

                                targetWidth = (targetWidth - spacing * (dimensions + 1)) / dimensions
                                targetHeight = (targetHeight - spacing * (dimensions + 1)) / dimensions

                                if (targetWidth > 0 && targetHeight > 0) {
                                    // console.log("qml dimens", Math.round(targetWidth * dpr), Math.round(targetHeight * dpr), dimensions)

                                    fileListModel.setFolderViewImageSize(Math.round(targetWidth * dpr), Math.round(targetHeight * dpr))
                                }
                            }
                        }

                        onValueChanged: {
                            masonryLayout.view.targetHeight = masonryZoomSlider.value
                            updateTargetSize()
                        }
                        property int lastValue: value
                        onPressedChanged: {
                            if (pressed) {
                                lastValue = value
                            }
                            else if (lastValue !== value) {
                                masonryLayout.view.reReadAndDecodeThumbnails()
                            }
                        }
                    }

                    Separator {
                    }

                    TabBar {
                        spacing: 0
                        Layout.alignment: Qt.AlignVCenter

                        Shortcut {
                            sequence: "F8"
                            onActivated: {
                                masonryLayout.view.listView = !masonryLayout.view.listView
                            }
                        }

                        TabButton {
                            implicitWidth: 32
                            implicitHeight: titleBar.thumbnailsHeight

                            icon.source: "qrc:/resources/ListView.svg"
                            icon.width: 16
                            icon.height: 16
                            ToolTip.text: "List View\tF8"

                            checked: masonryLayout.view.listView

                            onReleased: {
                                if (!masonryLayout.view.listView) {
                                    masonryLayout.view.listView = true
                                    masonryZoomSlider.updateTargetSize()
                                    masonryLayout.view.layoutReset()
                                }
                            }
                        }
                        TabButton {
                            implicitWidth: 32
                            implicitHeight: titleBar.thumbnailsHeight

                            icon.source: "qrc:/resources/GridView.svg"
                            icon.width: 16
                            icon.height: 16
                            ToolTip.text: "Grid View\tF8"

                            checked: !masonryLayout.view.listView

                            onReleased: {
                                if (masonryLayout.view.listView) {
                                    masonryLayout.view.listView = false
                                    masonryZoomSlider.updateTargetSize()
                                    masonryLayout.view.layoutReset()
                                }
                            }
                        }
                    }

                    ToolbarButton {
                        id: sortButton
                        Layout.leftMargin: 8
                        icon.source: "qrc:/resources/Sort.svg"
                        ToolTip.text: "Sort: " + galleryViewModel.sortModeLabel +
                                      (galleryViewModel.selectedOnly ? "\nShowing selected only" : "")

                        onReleased: {
                            sortMenu.open()
                        }

                        Platform.Menu {
                            id: sortMenu

                            Platform.MenuItemGroup {
                                id: sortModeMenuGroup
                                exclusive: true
                            }

                            Platform.MenuItem {
                                text: "Show selected items only"
                                checkable: true
                                checked: galleryViewModel.selectedOnly
                                shortcut: "Ctrl+\\"
                                onTriggered: galleryViewModel.selectedOnly = !galleryViewModel.selectedOnly
                            }

                            Platform.MenuSeparator {}

                            Platform.MenuItem {
                                text: "Name A-Z"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 0
                                onTriggered: galleryViewModel.sortMode = 0
                            }

                            Platform.MenuItem {
                                text: "Name Z-A"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 1
                                onTriggered: galleryViewModel.sortMode = 1
                            }

                            Platform.MenuItem {
                                text: "Modified oldest first"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 2
                                onTriggered: galleryViewModel.sortMode = 2
                            }

                            Platform.MenuItem {
                                text: "Modified newest first"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 3
                                onTriggered: galleryViewModel.sortMode = 3
                            }

                            Platform.MenuItem {
                                text: "Extension A-Z"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 4
                                onTriggered: galleryViewModel.sortMode = 4
                            }

                            Platform.MenuItem {
                                text: "Extension Z-A"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 5
                                onTriggered: galleryViewModel.sortMode = 5
                            }

                            Platform.MenuItem {
                                text: "Size smallest first"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 6
                                onTriggered: galleryViewModel.sortMode = 6
                            }

                            Platform.MenuItem {
                                text: "Size largest first"
                                group: sortModeMenuGroup
                                checkable: true
                                checked: galleryViewModel.sortMode === 7
                                onTriggered: galleryViewModel.sortMode = 7
                            }
                        }
                    }

                    ToolbarButton {
                        Layout.leftMargin: 8
                        icon.source: "qrc:/resources/RecursiveView.svg"
                        ToolTip.text: "Recursive View\tF10"

                        onReleased: {
                            viewerController.enterRecursiveView()
                        }
                    }

                    ToolbarButton {
                        icon.source: "qrc:/resources/SelectionHistory.svg"
                        ToolTip.text: "Selection history\tCtrl+Shift+H"
                        Layout.rightMargin: isQWK ? 0 : 7

                        onReleased: {
                            selectionHistoryWindow.visible = !selectionHistoryWindow.visible
                            if (selectionHistoryWindow.visible) {
                                selectionHistoryWindow.refresh(true)
                            }
                        }
                    }

                    Separator {
                        Layout.rightMargin: 7
                        visible: isQWK && !topLevelWindow.useMacNativeTitleBar
                    }
                }

                // Rectangle {
                //     anchors {
                //         left: parent.left
                //         right: parent.right
                //         bottom: parent.bottom
                //         bottomMargin: -4
                //     }
                //     height: 4
                //     gradient: Gradient {
                //         GradientStop { position: 0.0; color: Style.isDarkTheme ? "#fff" : "#000" }
                //         GradientStop { position: 0.5; color: Style.isDarkTheme ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(0, 0, 0, 0.2) }
                //         GradientStop { position: 1.0; color: "transparent" }
                //     }

                //     opacity: masonryLayout.scrolled ? (Style.isDarkTheme ? 0.08 : 0.10) : 0
                //     Behavior on opacity {
                //         NumberAnimation {
                //             duration: 300
                //             easing.type: Easing.InOutQuad
                //         }
                //     }
                // }
            }

            MasonryMode {
                id: masonryLayout
                Layout.fillWidth: true
                Layout.fillHeight: true

                onToggleViewer: root.toggleViewer()
            }
        }

        ViewerMode {
            id: viewerMode
            anchors.fill: parent

            onPinchZoomOutToThumbnailsProgressed: (progress) => root.updateViewerPinchClose(progress)
            onPinchZoomOutToThumbnailsFinished: (commit) => root.finishViewerPinchClose(commit)
        }

        SettingsDialog {
            id: settingsDialog
        }

        Shortcut {
            sequence: "Ctrl+Shift+H"
            onActivated: {
                selectionHistoryWindow.visible = !selectionHistoryWindow.visible
                if (selectionHistoryWindow.visible) {
                    selectionHistoryWindow.refresh(true)
                }
            }
        }

        Window {
            id: selectionHistoryWindow
            width: 520
            height: 620
            x: topLevelWindow.x + Math.max(0, topLevelWindow.width - width - 40)
            y: topLevelWindow.y + 80
            title: "Selection history"
            visible: false
            color: Style.windowBackgroundNoQWK

            property var historyModel: []
            property int historyIndex: -1

            function refresh(scrollToNewest = false) {
                historyModel = fileListModel.selectionHistoryForIndex(root.currentSourceIndex())
                historyIndex = fileListModel.selectionHistoryIndexForIndex(root.currentSourceIndex())
                if (scrollToNewest) {
                    scrollHistoryToBottom()
                }
            }

            function scrollHistoryToBottom() {
                Qt.callLater(function() {
                    if (selectionHistoryList.count > 0) {
                        selectionHistoryList.positionViewAtEnd()
                    }
                })
            }

            Connections {
                target: fileListModel
                function onSelectionChanged() {
                    if (selectionHistoryWindow.visible) {
                        selectionHistoryWindow.refresh()
                    }
                }
                function onSelectionHistoryChanged() {
                    if (selectionHistoryWindow.visible) {
                        selectionHistoryWindow.refresh(true)
                    }
                }
            }

            Connections {
                target: masonryLayout.view
                function onCurrentIndexChanged() {
                    if (selectionHistoryWindow.visible) {
                        selectionHistoryWindow.refresh()
                    }
                }
            }

            Connections {
                target: viewerController
                function onCurrentPathChanged() {
                    if (selectionHistoryWindow.visible) {
                        selectionHistoryWindow.refresh()
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: fileListModel.selectionContainerForIndex(root.currentSourceIndex())
                            color: Style.text
                            elide: Text.ElideMiddle
                            maximumLineCount: 1
                        }

                        Text {
                            text: fileListModel.selectedCount + " selected"
                            color: Style.viewerSecondaryText
                            font.pixelSize: 12
                        }
                    }

                    Button {
                        implicitWidth: 36
                        implicitHeight: 32
                        icon.source: "qrc:/resources/Back.svg"
                        inactive: selectionHistoryWindow.historyIndex <= 0
                        onClicked: {
                            if (!inactive) {
                                fileListModel.selectionHistoryBack(root.currentSourceIndex())
                            }
                        }
                    }

                    Button {
                        implicitWidth: 36
                        implicitHeight: 32
                        icon.source: "qrc:/resources/Forward.svg"
                        inactive: selectionHistoryWindow.historyIndex < 0 ||
                                  selectionHistoryWindow.historyIndex >= selectionHistoryWindow.historyModel.length - 1
                        onClicked: {
                            if (!inactive) {
                                fileListModel.selectionHistoryForward(root.currentSourceIndex())
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Style.lighter2
                }

                ListView {
                    id: selectionHistoryList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: selectionHistoryWindow.historyModel
                    rightMargin: selectionHistoryScrollBar.width

                    ScrollBar.vertical: ScrollBar {
                        id: selectionHistoryScrollBar
                        policy: ScrollBar.AlwaysOn
                    }

                    delegate: Rectangle {
                        width: selectionHistoryList.width - selectionHistoryScrollBar.width
                        height: 48
                        radius: 4
                        color: modelData.current ? Style.brickSelected : historyMouse.containsMouse ? Style.lighter : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            Text {
                                Layout.preferredWidth: 34
                                text: modelData.index
                                color: Style.viewerSecondaryText
                                horizontalAlignment: Text.AlignRight
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.description
                                    color: Style.text
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.timestamp
                                    color: Style.viewerSecondaryText
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                Layout.preferredWidth: 86
                                text: modelData.selectedCount + " selected"
                                color: Style.viewerSecondaryText
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        MouseArea {
                            id: historyMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: fileListModel.jumpSelectionHistory(root.currentSourceIndex(), modelData.index)
                        }
                    }
                }
            }
        }

        state: "thumbnails"

        transitions: [
            Transition {
                from: "thumbnails"
                to: "viewer"
                SequentialAnimation {
                    PropertyAnimation { properties: "opacity"; duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
                    PropertyAction {
                        target: toolbarLayout
                        property: "visible"
                        value: false
                    }
                }
            },
            Transition {
                from: "viewer"
                to: "thumbnails"
                SequentialAnimation {
                    PropertyAction {
                        target: toolbarLayout
                        property: "visible"
                        value: true
                    }
                    PropertyAnimation { properties: "opacity"; duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
                }
            }
        ]


        states: [
            State {
                name: "thumbnails"
            },
            State {
                name: "viewer"
                PropertyChanges {
                    target: thumbnailsView
                    opacity: root.viewerPinchCloseActive ? root.viewerPinchCloseProgress : 0
                }
                PropertyChanges {
                    target: thumbnailsViewBackground
                    opacity: root.viewerPinchCloseActive ? root.viewerPinchCloseProgress : 0
                }
                PropertyChanges {
                    target: viewerBackground
                    opacity: root.viewerPinchCloseActive ? 1 - root.viewerPinchCloseProgress : 1
                }
                PropertyChanges {
                    target: titleBar
                    opacity: root.viewerPinchCloseActive ? root.viewerPinchCloseProgress : 0
                }
            }
        ]
    }
}
