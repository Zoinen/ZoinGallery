pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import Qt.labs.platform as Platform

import ZoinGallery.MainWindow 1.0
import ZoinGallery 1.0
import ZoinGallery.Native 1.0

import QWindowKit 1.0

MainWindow {
    id: topLevelWindow
    objectName: "standaloneMainWindow"
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
                    root.masonryLayout.view.reReadAndDecodeThumbnails()
                }
                shellController.viewerDirty = true
            }
            else {
                if (viewerMode.zoomFitView && !viewerMode.sphericViewerMode) {
                    // console.log("onMainWindowResized")
                    viewerMode.imageContainer.zoomToFit(true)
                    shellController.viewerDecodeModel.cancelAllDecodeViewerRunners()
                    viewerMode.requestCurrentViewer(
                                viewerMode.width * dpr,
                                viewerMode.height * dpr)
                    shellController.viewerDirty = false
                }
                else {
                    shellController.viewerDirty = true
                }
                shellController.thumbnailsDirty = true
            }
            })
        }

        function onWindowIsReady() {
            root.masonryZoomSlider.updateTargetSize()
            viewerController.initialCd(Math.round(viewerMode.width * dpr), Math.round(viewerMode.height * dpr))
            if (root.state === "thumbnails")
                root.masonryLayout.focusView()
            schedulePlatformWindowEffects()
        }

        function onOpenSettingsRequested() {
            settingsDialog.open()
        }
    }

    Connections {
        target: viewerMode
        function onZoomFitViewChanged() {
            shellController.viewerDecodeModel.cancelAllDecodeViewerRunners()
            if (viewerMode.zoomFitView) {
                // The requested decode tier must follow the actual viewer
                // mode even when the current image was already clean.  A
                // native request here leaves swipe lookup biased toward the
                // full-size tier while the UI is visibly in Fit mode.
                viewerMode.requestCurrentViewer(
                            viewerMode.width * dpr,
                            viewerMode.height * dpr)
                shellController.viewerDirty = false
            }
            else {
                viewerMode.requestCurrentViewer()
            }
        }
    }

    Connections {
        target: viewerController
        function onCurrentPathChanged() {
            root.masonryZoomSlider.updateTargetSize()
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
                shellController.useMainViewerSource()
                Qt.callLater(() => shellController.tryOpenExternalInViewer(index))
            }
        }
    }

    Rectangle {
        id: root
        objectName: "standaloneShellContent"
        anchors.fill: parent
        color: topLevelWindow.useMacNativeTitleBar ? Style.macGlassWindowColor : Style.windowColor

        readonly property alias masonryLayout: thumbnailsPanels.masonry
        readonly property alias toolbarLayout: thumbnailToolbar.controlLayout
        readonly property alias masonryZoomSlider: thumbnailToolbar.zoomSlider

        StandaloneShellController {
            id: shellController
            shell: root
            hostWindow: topLevelWindow
            viewer: viewerMode
            galleryLayout: root.masonryLayout
            toolbarControlLayout: root.toolbarLayout
            thumbnailSurface: thumbnailsView
            thumbnailsBackground: thumbnailsViewBackground
            viewerBackgroundItem: viewerBackground
            titleBarItem: titleBar
            navigationController: viewerController
            catalogModel: fileListModel
            viewModel: galleryViewModel
            previewModel: imageModel
            createFolderDialog: shellOverlays.createDialog
            createFolderNameField: shellOverlays.nameField
            createFolderErrorLabel: shellOverlays.errorLabel
            dropErrorDialog: shellOverlays.dropDialog
        }
        StandaloneTitleBar {
            id: titleBar
            hostWindow: topLevelWindow
            windowAgentObject: windowAgent
            quickWindowKitEnabled: isQWK
            shellState: root.state
            animationDuration: viewerMode.animationDuration
            easingType: viewerMode.easingType
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
                    windowAgent.setSystemButtonArea(titleBar.macSystemButtonRegion)
                }

                for (var i = 0; i < titleBar.systemButtonsLayout.children.length; i++) {
                    if (typeof titleBar.systemButtonsLayout.children[i].isPartOfTitleBar === "undefined") {
                        windowAgent.setHitTestVisible(titleBar.systemButtonsLayout.children[i])
                    }
                }

                for (var i = 0; i < root.toolbarLayout.children.length; i++) {
                    if (typeof root.toolbarLayout.children[i].isPartOfTitleBar === "undefined") {
                        windowAgent.setHitTestVisible(root.toolbarLayout.children[i])
                    }
                }
            }

            StandaloneToolbar {
                id: thumbnailToolbar
                Layout.fillWidth: true
                Layout.preferredHeight: titleBar.thumbnailsHeight
                shell: shellController
                hostWindow: topLevelWindow
                titleBarItem: titleBar
                systemButtonsLayout: titleBar.systemButtonsLayout
                windowAgentObject: windowAgent
                galleryLayout: root.masonryLayout
                navigationController: viewerController
                catalogModel: fileListModel
                viewModel: galleryViewModel
                settingsWindow: settingsDialog
                historyWindow: selectionHistoryWindow
                quickWindowKitEnabled: isQWK
                devicePixelRatio: topLevelWindow.dpr
            }
            StandalonePanelsSurface {
                id: thumbnailsPanels
                Layout.fillWidth: true
                Layout.fillHeight: true
                catalogSession: gallerySession
                navigationController: viewerController
                catalogModel: fileListModel
                viewModel: galleryViewModel
                previewModel: imageModel
                selectedCatalogModel: selectedImagesModel
                selectedImagesPanelOpen: shellController.selectedImagesPanelOpen
                viewerShowAnimationRunning: shellController.viewerShowAnimationRunning
                viewerSourceMasonry: shellController.viewerSourceMasonry

                onOpenViewerRequested:
                    (source, mapper, decode, selection, filmstrip) =>
                        shellController.openViewerFrom(source, mapper, decode,
                                            selection, filmstrip)
                onFileDropFailed: (title, message) =>
                                      shellController.showFileDropError(title, message)
                onFullscreenRequested: topLevelWindow.toggleFullscreen()
                onCloseSelectedImagesRequested:
                    shellController.selectedImagesPanelOpen = false
            }
        }

        GalleryViewer {
            id: standaloneGalleryViewer
            objectName: "standaloneGalleryViewer"
            anchors.fill: parent
            session: gallerySession
            customContent: viewerMode

            ViewerMode {
                id: viewerMode
                objectName: "standaloneViewerMode"
                anchors.fill: parent
                devicePixelRatio: topLevelWindow.dpr
                shell: shellController
                hostWindow: topLevelWindow
                standaloneController: viewerController
                titleBarItem: titleBar
                viewerBackgroundItem: viewerBackground
                minimizeButton: titleBar.minimizeControl
                maximizeButton: titleBar.maximizeControl
                closeButton: titleBar.closeControl
                quickWindowKitEnabled: isQWK

                sourceContext: shellController.viewerSourceContext

                onPinchZoomOutToThumbnailsProgressed: (progress) => shellController.updateViewerPinchClose(progress)
                onPinchZoomOutToThumbnailsFinished: (commit) => shellController.finishViewerPinchClose(commit)
            }
        }

        SettingsDialog {
            id: settingsDialog
            galleryLayout: root.masonryLayout
        }

        StandaloneShellOverlays {
            id: shellOverlays
            anchors.fill: parent
            stateController: shellController
        }
        StandaloneSelectionHistoryWindow {
            id: selectionHistoryWindow
            hostWindow: topLevelWindow
            stateController: shellController
            galleryLayout: root.masonryLayout
            navigationController: viewerController
            catalogModel: fileListModel
        }
        state: "thumbnails"

        transitions: [
            Transition {
                from: "thumbnails"
                to: "viewer"
                SequentialAnimation {
                    PropertyAnimation { properties: "opacity"; duration: viewerMode.animationDuration; easing.type: viewerMode.easingType }
                    PropertyAction {
                        target: root.toolbarLayout
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
                        target: root.toolbarLayout
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
                    opacity: shellController.viewerPinchCloseActive ? shellController.viewerPinchCloseProgress : 0
                }
                PropertyChanges {
                    target: thumbnailsViewBackground
                    opacity: shellController.viewerPinchCloseActive ? shellController.viewerPinchCloseProgress : 0
                }
                PropertyChanges {
                    target: viewerBackground
                    opacity: shellController.viewerPinchCloseActive ? 1 - shellController.viewerPinchCloseProgress : 1
                }
                PropertyChanges {
                    target: titleBar
                    opacity: shellController.viewerPinchCloseActive ? shellController.viewerPinchCloseProgress : 0
                }
            }
        ]
    }
}
