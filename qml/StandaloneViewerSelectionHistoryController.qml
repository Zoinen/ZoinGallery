pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property Item viewer
    required property FlickableZoomable imageViewport

    function beginShiftSelection() {
        if (viewer.shiftSelectionActive) {
            return
        }
        viewer.shiftSelectionActive = true
        viewer.shiftSelectionAnchorIndex = viewer.sourceMasonry.view.currentIndex
        viewer.shiftSelectionAnchorPath = pathForIndex(viewer.shiftSelectionAnchorIndex)
        viewer.shiftNavigationSelectionValue = !viewer.selectionModel.isIndexSelected(viewer.sourceIndexForViewIndex(viewer.shiftSelectionAnchorIndex))
        viewer.selectionModel.beginSelectionPreview()
    }

    function updateShiftNavigationSelection(targetIndex) {
        beginShiftSelection()
        viewer.selectionModel.previewSelectionIndexes(
                    viewer.sourceMapper.sourceRowsForViewRange(viewer.shiftSelectionAnchorIndex, targetIndex, false),
                    viewer.shiftNavigationSelectionValue ? 0 : 1)
    }

    function finishShiftSelection() {
        if (!viewer.shiftSelectionActive) {
            return
        }
        viewer.selectionModel.commitSelectionPreview(viewer.shiftNavigationSelectionValue ? "Range selection" : "Range deselection")
        viewer.shiftSelectionActive = false
        viewer.shiftSelectionAnchorIndex = -1
        viewer.shiftSelectionAnchorPath = ""
    }

    function cancelShiftSelection() {
        if (!viewer.shiftSelectionActive) {
            return
        }
        viewer.selectionModel.cancelSelectionPreview()
        viewer.shiftSelectionActive = false
        viewer.shiftSelectionAnchorIndex = -1
        viewer.shiftSelectionAnchorPath = ""
    }

    function clearPreviousImage() {
        viewer.previousImageIndex = -1
        viewer.previousImagePath = ""
        viewer.previousImageLocked = false
        viewer.lockedPreviousReturnIndex = -1
        viewer.lockedPreviousImagePath = ""
        viewer.lockedPreviousReturnPath = ""
    }

    function pathForIndex(index) {
        return index !== -1 ? viewer.sourceMasonry.view.indexFullPath(index) : ""
    }

    function effectiveSizeFromOriginalSize(originalSize) {
        if (originalSize.width <= 1 || originalSize.height <= 1) {
            return Qt.size(0, 0)
        }
        let displaySize = Qt.size(originalSize.width / viewer.devicePixelRatio, originalSize.height / viewer.devicePixelRatio)
        return imageViewport.rotationMode % 2 === 1 ? Qt.size(displaySize.height, displaySize.width) : displaySize
    }

    function fitScaleForEffectiveSize(size) {
        if (size.width <= 1 || size.height <= 1 || viewer.width <= 1 || viewer.height <= 1) {
            return 1
        }
        return size.width / size.height <= viewer.width / viewer.height ?
                    viewer.height / size.height :
                    viewer.width / size.width
    }

    function indexForPath(path) {
        if (path === "") {
            return -1
        }
        for (let i = 0; i < viewer.sourceMasonry.view.count; i++) {
            if (viewer.sourceMasonry.view.indexFullPath(i) === path) {
                return i
            }
        }
        return -1
    }

    function restorePreviousImageLockIndexes() {
        if (!viewer.previousImageLocked) {
            return
        }
        if (viewer.lockedPreviousImagePath !== "") {
            viewer.previousImageIndex = indexForPath(viewer.lockedPreviousImagePath)
            viewer.previousImagePath = viewer.previousImageIndex !== -1 ?
                        viewer.lockedPreviousImagePath : ""
        }
        if (viewer.lockedPreviousReturnPath !== "") {
            viewer.lockedPreviousReturnIndex = indexForPath(viewer.lockedPreviousReturnPath)
        }
    }

    function rememberViewportForPreviousImageSwitch(targetIndex) {
        viewer.pendingPreviousImageViewport = null
        if (targetIndex === -1 || imageViewport.zoomFitView ||
                imageViewport.effectiveOriginalSize.width <= 1 ||
                imageViewport.effectiveOriginalSize.height <= 1) {
            return
        }

        let targetSize = effectiveSizeFromOriginalSize(viewer.sourceMasonry.view.indexOriginalSize(targetIndex))
        if (targetSize.width <= 1 || targetSize.height <= 1) {
            return
        }

        let sourceSize = imageViewport.effectiveOriginalSize
        let sourceFitScale = fitScaleForEffectiveSize(sourceSize)
        viewer.pendingPreviousImageViewport = {
            targetPath: pathForIndex(targetIndex),
            centerRatioX: ((viewer.width / 2 - imageViewport.image.x) / imageViewport.zoomScale) / sourceSize.width,
            centerRatioY: ((viewer.height / 2 - imageViewport.image.y) / imageViewport.zoomScale) / sourceSize.height,
            zoomToFitRatio: imageViewport.zoomScale / sourceFitScale
        }
    }

    function applyPendingPreviousImageViewport() {
        if (!viewer.pendingPreviousImageViewport) {
            return false
        }
        if (viewer.pendingPreviousImageViewport.targetPath !== "" &&
                viewer.pendingPreviousImageViewport.targetPath !== pathForIndex(viewer.sourceMasonry.view.currentIndex)) {
            viewer.pendingPreviousImageViewport = null
            return false
        }
        if (imageViewport.effectiveOriginalSize.width <= 1 || imageViewport.effectiveOriginalSize.height <= 1) {
            return false
        }

        let targetSize = imageViewport.effectiveOriginalSize
        let targetZoom = imageViewport.clampZoomScale(
                    fitScaleForEffectiveSize(targetSize) * viewer.pendingPreviousImageViewport.zoomToFitRatio)
        let targetX = viewer.width / 2 -
                targetSize.width * viewer.pendingPreviousImageViewport.centerRatioX * targetZoom
        let targetY = viewer.height / 2 -
                targetSize.height * viewer.pendingPreviousImageViewport.centerRatioY * targetZoom

        imageViewport.setViewport(targetZoom, targetX, targetY)
        viewer.pendingPreviousImageViewport = null
        return true
    }

    function togglePreviousImageLock(index) {
        restorePreviousImageLockIndexes()
        if (viewer.previousImageLocked) {
            if (viewer.previousImageIndex === index) {
                viewer.previousImageLocked = false
                viewer.previousImageIndex = viewer.lockedPreviousReturnIndex
                viewer.previousImagePath = viewer.lockedPreviousReturnPath
                viewer.lockedPreviousReturnIndex = -1
                viewer.lockedPreviousImagePath = ""
                viewer.lockedPreviousReturnPath = ""
            } else {
                viewer.lockedPreviousReturnIndex = viewer.previousImageIndex
                viewer.lockedPreviousReturnPath = viewer.lockedPreviousImagePath
                viewer.previousImageIndex = index
                viewer.previousImagePath = pathForIndex(index)
                viewer.lockedPreviousImagePath = pathForIndex(index)
            }
            return
        }

        viewer.lockedPreviousReturnIndex = viewer.previousImageIndex !== index ? viewer.previousImageIndex : -1
        viewer.lockedPreviousReturnPath = pathForIndex(viewer.lockedPreviousReturnIndex)
        viewer.previousImageIndex = index
        viewer.previousImagePath = pathForIndex(index)
        viewer.lockedPreviousImagePath = pathForIndex(index)
        viewer.previousImageLocked = true
    }

    function switchToPreviousImage(currentIndex) {
        if (viewer.previousImageLocked) {
            restorePreviousImageLockIndexes()
            if (viewer.previousImageIndex === -1) {
                return -1
            }
            if (currentIndex === viewer.previousImageIndex) {
                restorePreviousImageLockIndexes()
                if (viewer.lockedPreviousReturnIndex !== -1) {
                    rememberViewportForPreviousImageSwitch(viewer.lockedPreviousReturnIndex)
                    viewer.sourceMasonry.setCurrentIndex(viewer.lockedPreviousReturnIndex)
                    return viewer.lockedPreviousReturnIndex
                }
                return -1
            }

            viewer.lockedPreviousReturnIndex = currentIndex
            viewer.lockedPreviousReturnPath = pathForIndex(currentIndex)
        }
        else if (viewer.previousImagePath !== "") {
            viewer.previousImageIndex = indexForPath(viewer.previousImagePath)
        }
        if (viewer.previousImageIndex === -1) {
            return -1
        }

        // Capture the target before setCurrentIndex(), since changing the index
        // synchronously reassigns viewer.previousImageIndex via the view's onCurrentIndexChanged handler.
        let targetIndex = viewer.previousImageIndex
        rememberViewportForPreviousImageSwitch(targetIndex)
        viewer.sourceMasonry.setCurrentIndex(targetIndex)
        return targetIndex
    }


}
