import QtQuick
import QtQuick.Controls

pragma ComponentBehavior: Bound

Item {
    id: controller

    required property Item shell
    required property QtObject hostWindow
    required property Item viewer
    required property Item galleryLayout
    required property Item toolbarControlLayout
    required property Item thumbnailSurface
    required property Item thumbnailsBackground
    required property Item viewerBackgroundItem
    required property Item titleBarItem
    required property QtObject navigationController
    required property QtObject catalogModel
    required property QtObject viewModel
    required property QtObject previewModel
    required property Popup createFolderDialog
    required property TextField createFolderNameField
    required property Text createFolderErrorLabel
    required property Popup dropErrorDialog

    state: shell.state

    property bool viewerPinchCloseActive: false
    property bool viewerDirty: false
    property bool thumbnailsDirty: false
    property bool selectedImagesPanelOpen: false
    property var pendingCreateDropUrls: []
    property int pendingCreateDropAction: Qt.CopyAction
    property bool createFolderForDrop: false
    property var viewerSourceContext: ({
        "masonry": controller.galleryLayout,
        "mapper": controller.viewModel,
        "decodeModel": controller.catalogModel,
        "selectionModel": controller.catalogModel,
        "filmstripModel": controller.previewModel
    })
    readonly property var viewerSourceMasonry:
        viewerSourceContext.masonry
    readonly property var viewerSourceMapper:
        viewerSourceContext.mapper
    readonly property var viewerDecodeModel:
        viewerSourceContext.decodeModel
    readonly property var viewerSelectionModel:
        viewerSourceContext.selectionModel
    readonly property var viewerFilmstripModel:
        viewerSourceContext.filmstripModel
    property bool viewerPinchCloseReturning: false
    property bool viewerPinchCloseFinishingCommit: false
    property real viewerPinchCloseProgress: 0

    function currentSourceIndex() {
        return controller.viewModel.mapToSourceRow(controller.galleryLayout.view.currentIndex)
    }

    function showFileDropError(title, message) {
        controller.dropErrorDialog.titleText = title || "File operation failed"
        controller.dropErrorDialog.messageText = message || "The operation could not be completed."
        controller.dropErrorDialog.open()
    }

    function beginFolderCreation(urls, action) {
        pendingCreateDropUrls = urls || []
        pendingCreateDropAction = action === Qt.MoveAction
                                  ? Qt.MoveAction : Qt.CopyAction
        createFolderForDrop = pendingCreateDropUrls.length > 0
        createFolderNameField.text = ""
        createFolderErrorLabel.text = ""
        controller.createFolderDialog.open()
        Qt.callLater(() => createFolderNameField.forceActiveFocus())
    }

    function confirmFolderCreation() {
        const result = createFolderForDrop
                     ? controller.catalogModel.createFolderAndDropUrls(
                           pendingCreateDropUrls,
                           controller.navigationController.currentPath,
                           createFolderNameField.text,
                           pendingCreateDropAction)
                     : controller.catalogModel.createFolder(
                           controller.navigationController.currentPath,
                           createFolderNameField.text)
        if (result.success) {
            controller.createFolderDialog.close()
            pendingCreateDropUrls = []
            controller.galleryLayout.focusProxy.forceActiveFocus()
        }
        else {
            createFolderErrorLabel.text = result.message || result.title
            createFolderNameField.selectAll()
            createFolderNameField.forceActiveFocus()
        }
    }

    function viewerSourceIndex() {
        return viewerSourceMapper.mapToSourceRow(
                    viewerSourceMasonry.view.currentIndex)
    }

    function setViewerSource(masonry, mapper, decodeModel,
                             selectionModel, filmstripModel) {
        viewerSourceContext = {
            "masonry": masonry,
            "mapper": mapper,
            "decodeModel": decodeModel,
            "selectionModel": selectionModel,
            "filmstripModel": filmstripModel
        }
    }

    function useMainViewerSource() {
        setViewerSource(controller.galleryLayout, controller.viewModel, controller.catalogModel,
                        controller.catalogModel, controller.previewModel)
    }

    function openViewerFrom(masonry, mapper, decodeModel,
                            selectionModel, filmstripModel) {
        if (controller.shell.state !== "thumbnails") {
            return
        }
        setViewerSource(masonry, mapper, decodeModel,
                        selectionModel, filmstripModel)
        toggleViewer()
    }
    property rect viewerPinchCloseStartGeometry: Qt.rect(0, 0, 0, 0)
    property rect viewerPinchCloseTargetGeometry: Qt.rect(0, 0, 0, 0)
    property bool viewerShowAnimationRunning: controller.viewer.animation.running || viewerPinchCloseActive

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
        if (controller.shell.state === "thumbnails") {
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
        controller.navigationController.clearPendingOpenInViewer()
        viewerDecodeModel.cancelAllDecodeViewerRunnersForViewerClose()
        switchToThumbnails(startGeometry)
        if (thumbnailsDirty) {
            thumbnailsDirty = false
            viewerSourceMasonry.view.reReadAndDecodeThumbnails()
        }
    }

    function validGeometry(geometry) {
        return geometry !== undefined && geometry.width > 1 && geometry.height > 1
    }

    function currentThumbnailGeometry() {
        if (!viewerSourceMasonry.view.currentItem) {
            return undefined
        }

        return controller.shell.mapFromItem(viewerSourceMasonry.view,
                                viewerSourceMasonry.currentItemImageGeometry())
    }

    function currentThumbnailImageGeometry() {
        let thumbnailGeometry = currentThumbnailGeometry()
        if (!validGeometry(thumbnailGeometry)) {
            return undefined
        }

        return controller.viewer.imageContainer.imageRectFittedInRect(thumbnailGeometry)
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

        if (controller.shell.state !== "viewer") {
            return false
        }

        let startGeometry = currentViewerImageGeometry()
        let targetGeometry = currentThumbnailImageGeometry()
        if (!validGeometry(startGeometry) || !validGeometry(targetGeometry)) {
            return false
        }

        controller.viewer.animation.stop()
        viewerPinchCloseProgressAnimation.stop()
        viewerPinchCloseFinalizeTimer.stop()
        viewerPinchCloseStartGeometry = startGeometry
        viewerPinchCloseTargetGeometry = targetGeometry
        viewerPinchCloseReturning = false
        viewerPinchCloseFinishingCommit = false
        viewerPinchCloseActive = true
        controller.toolbarControlLayout.visible = true

        controller.viewer.imageContainer.x = 0
        controller.viewer.imageContainer.y = 0
        controller.viewer.imageContainer.width = Qt.binding(() => controller.viewer.width)
        controller.viewer.imageContainer.height = Qt.binding(() => controller.viewer.height)
        controller.viewer.imageContainer.setImageRect(startGeometry)
        return true
    }

    function cancelViewerPinchCloseDuringGesture() {
        viewerPinchCloseProgressAnimation.stop()
        viewerPinchCloseFinalizeTimer.stop()
        viewerPinchCloseActive = false
        viewerPinchCloseReturning = false
        viewerPinchCloseFinishingCommit = false
        viewerPinchCloseProgress = 0
        controller.toolbarControlLayout.visible = false
        controller.viewer.imageContainer.x = 0
        controller.viewer.imageContainer.y = 0
        controller.viewer.imageContainer.width = Qt.binding(() => controller.viewer.width)
        controller.viewer.imageContainer.height = Qt.binding(() => controller.viewer.height)
    }

    function completeViewerPinchCloseReturn() {
        viewerPinchCloseProgressAnimation.stop()
        viewerPinchCloseFinalizeTimer.stop()
        viewerPinchCloseActive = false
        viewerPinchCloseReturning = false
        viewerPinchCloseFinishingCommit = false
        viewerPinchCloseProgress = 0
        controller.toolbarControlLayout.visible = false
        controller.viewer.imageContainer.zoomToFit()
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
        controller.viewer.imageContainer.setImageRect(Qt.rect(
                lerp(viewerPinchCloseStartGeometry.x, viewerPinchCloseTargetGeometry.x, progress),
                lerp(viewerPinchCloseStartGeometry.y, viewerPinchCloseTargetGeometry.y, progress),
                lerp(viewerPinchCloseStartGeometry.width, viewerPinchCloseTargetGeometry.width, progress),
                lerp(viewerPinchCloseStartGeometry.height, viewerPinchCloseTargetGeometry.height, progress)))
    }

    function enterThumbnailsAfterViewerPinchClose() {
        controller.navigationController.clearPendingOpenInViewer()
        viewerDecodeModel.cancelAllDecodeViewerRunnersForViewerClose()
        viewerSourceMasonry.focusView()
        if (controller.shell.state !== "thumbnails") {
            controller.shell.state = "thumbnails"
        }

        controller.toolbarControlLayout.visible = true
        if (thumbnailsDirty) {
            thumbnailsDirty = false
            viewerSourceMasonry.view.reReadAndDecodeThumbnails()
        }
        controller.viewer.panelsVisible = false
        controller.hostWindow.title = "ZoinGallery"
        controller.thumbnailSurface.opacity = 1
        controller.thumbnailsBackground.opacity = 1
        controller.viewerBackgroundItem.opacity = 0
        controller.titleBarItem.opacity = 1
    }

    function completeViewerPinchClose() {
        viewerPinchCloseProgressAnimation.stop()
        viewerPinchCloseFinalizeTimer.stop()
        enterThumbnailsAfterViewerPinchClose()
        controller.viewer.visible = false
        viewerPinchCloseActive = false
        viewerPinchCloseReturning = false
        viewerPinchCloseFinishingCommit = false
        viewerPinchCloseProgress = 0
        controller.viewer.imageContainer.x = 0
        controller.viewer.imageContainer.y = 0
        controller.viewer.imageContainer.width = Qt.binding(() => controller.viewer.width)
        controller.viewer.imageContainer.height = Qt.binding(() => controller.viewer.height)
        controller.viewer.imageContainer.zoomToFit(true)
        controller.viewer.zoomFitView = true
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
        target: controller.shell
        property: "viewerPinchCloseProgress"
        duration: controller.viewer.animationDuration
        easing.type: controller.viewer.easingType

        onFinished: {
            if (viewerPinchCloseFinishingCommit) {
                completeViewerPinchClose()
            }
            else if (viewerPinchCloseReturning && controller.shell.state === "viewer") {
                completeViewerPinchCloseReturn()
            }
        }
    }

    Timer {
        id: viewerPinchCloseFinalizeTimer
        interval: controller.viewer.animationDuration + 50
        repeat: false
        onTriggered: {
            if (viewerPinchCloseFinishingCommit) {
                completeViewerPinchClose()
            }
            else if (viewerPinchCloseReturning && controller.shell.state === "viewer") {
                completeViewerPinchCloseReturn()
            }
        }
    }

    function tryOpenExternalInViewer(targetIndex, attempts) {
        if (attempts === undefined) {
            attempts = 0
        }
        if (controller.galleryLayout.view.currentIndex !== targetIndex) {
            if (attempts < 300) {
                Qt.callLater(() => controller.tryOpenExternalInViewer(targetIndex, attempts + 1))
            }
            else {
                console.warn("External image open did not reach target index", targetIndex,
                             "current", controller.galleryLayout.view.currentIndex)
            }
            return
        }
        let size = controller.galleryLayout.view.indexOriginalSize(controller.galleryLayout.view.currentIndex)
        if ((size.width <= 1 || size.height <= 1) && attempts < 300) {
            Qt.callLater(() => controller.tryOpenExternalInViewer(targetIndex, attempts + 1))
            return
        }
        controller.switchToViewer(false)
    }

    function switchToViewer(animated = true) {
        let currentItem = viewerSourceMasonry.view.currentItem
        if (!currentItem || !currentItem.model) {
            return
        }

        viewerPinchCloseProgressAnimation.stop()
        viewerPinchCloseFinalizeTimer.stop()
        viewerPinchCloseActive = false
        viewerPinchCloseReturning = false
        viewerPinchCloseFinishingCommit = false
        viewerPinchCloseProgress = 0
        controller.viewer.forceActiveFocus()
        controller.shell.state = "viewer"
        controller.viewer.zoomFitView = true

        if (animated) {
            if (!controller.viewer.animation.running) {
                let mappedGeometry = controller.shell.mapFromItem(
                            viewerSourceMasonry.view,
                            viewerSourceMasonry.currentItemImageGeometry())

                controller.viewer.imageContainer.x = mappedGeometry.x
                controller.viewer.imageContainer.y = mappedGeometry.y
                controller.viewer.imageContainer.width = mappedGeometry.width
                controller.viewer.imageContainer.height = mappedGeometry.height
            }
        }
        else {
            controller.viewer.imageContainer.x = 0
            controller.viewer.imageContainer.y = 0
            controller.viewer.imageContainer.width = controller.viewer.width
            controller.viewer.imageContainer.height = controller.viewer.height
        }

        controller.viewer.setImage(currentItem.model.imageIdUrl,
                            viewerSourceMasonry.view.indexOriginalSize(
                                viewerSourceMasonry.view.currentIndex),
                            viewerSourceMasonry.view.currentIndex, 0)
        let exif = viewerSourceMasonry.view.indexExif(
                    viewerSourceMasonry.view.currentIndex)
        controller.viewer.show(exif["Panorama"])

        if (animated) {
            controller.viewer.animation.x = 0
            controller.viewer.animation.y = 0
            controller.viewer.animation.width = controller.viewer.width
            controller.viewer.animation.height = controller.viewer.height
            controller.viewer.animation.restart()
        }
        else {
            controller.viewer.completeInstantOpen()
        }
    }

    function currentViewerImageGeometry() {
        let image = controller.viewer.imageContainer.image
        if (image.width <= 1 || image.height <= 1) {
            return undefined
        }

        return controller.shell.mapFromItem(controller.viewer.imageContainer,
                                Qt.rect(image.x, image.y, image.width, image.height))
    }

    function switchToThumbnails(startGeometry) {
        viewerSourceMasonry.focusView()
        controller.shell.state = "thumbnails"

        if (viewerSourceMasonry.view.currentItem) {
            let mappedGeometry = controller.shell.mapFromItem(
                        viewerSourceMasonry.view,
                        viewerSourceMasonry.currentItemImageGeometry())

            if (startGeometry !== undefined && startGeometry.width > 1 && startGeometry.height > 1) {
                controller.viewer.animation.stop()
                controller.viewer.imageContainer.x = startGeometry.x
                controller.viewer.imageContainer.y = startGeometry.y
                controller.viewer.imageContainer.width = startGeometry.width
                controller.viewer.imageContainer.height = startGeometry.height
                controller.viewer.imageContainer.zoomToFit(true)
            }
            else if (!controller.viewer.zoomFitView) {
                controller.viewer.imageContainer.zoomToFit(true) // TODO: Smooth animation
            }

            controller.viewer.animation.x = mappedGeometry.x
            controller.viewer.animation.y = mappedGeometry.y
            controller.viewer.animation.width = mappedGeometry.width
            controller.viewer.animation.height = mappedGeometry.height
            controller.viewer.animation.restart()
        }
        else {
            controller.viewer.visible = false
            controller.viewer.completeInstantOpen()
        }

        controller.hostWindow.title = "ZoinGallery"
    }

}
