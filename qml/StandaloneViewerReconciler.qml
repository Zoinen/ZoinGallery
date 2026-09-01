pragma ComponentBehavior: Bound

import QtQuick

Item {
    required property Item viewer
    required property FlickableZoomable imageViewport
    visible: false

    Connections {
        target: viewer.shell
        function onStateChanged() {
            if (viewer.shell.state === "thumbnails") {
                if (!viewer.previousImageLocked) {
                    viewer.previousImageIndex = -1
                    viewer.previousImagePath = ""
                }
                viewer.resetViewerNavigation()
                viewer.endViewerNavigationGesture()
                imageViewport.rotationMode = 0
            }
        }
    }

    Connections {
        target: viewer.standaloneController
        function onCurrentPathChanged() {
            viewer.clearPreviousImage()
            viewer.resetViewerNavigation()
            viewer.endViewerNavigationGesture()
        }
    }

    Connections {
        target: viewer.sourceMasonry.view
        function onCountChanged() {
            if (viewer.shell.state === "viewer" && viewer.sourceMasonry.view.count === 0) {
                viewer.cancelShiftSelection()
                viewer.shell.closeViewer()
                return
            }
            if (viewer.previousImageLocked) {
                viewer.restorePreviousImageLockIndexes()
            }
            else if (viewer.previousImagePath !== "") {
                viewer.previousImageIndex = viewer.indexForPath(viewer.previousImagePath)
                if (viewer.previousImageIndex === -1) {
                    viewer.previousImagePath = ""
                }
            }

            if (viewer.shiftSelectionActive && viewer.shiftSelectionAnchorPath !== "") {
                let remappedAnchor = viewer.indexForPath(viewer.shiftSelectionAnchorPath)
                if (remappedAnchor === -1) {
                    viewer.cancelShiftSelection()
                }
                else {
                    viewer.shiftSelectionAnchorIndex = remappedAnchor
                }
            }

            if (viewer.viewerNavigationTargetPath !== "") {
                let remappedTarget = viewer.indexForPath(viewer.viewerNavigationTargetPath)
                if (remappedTarget === -1) {
                    viewer.resetViewerNavigation("target-removed-by-model-change")
                }
                else {
                    viewer.viewerNavigationTargetIndex = remappedTarget
                    viewer.updateViewerNavigationTargetSource()
                }
            }
        }

        function onImageCountChanged() {
            if (viewer.shell.state === "viewer") {
                viewer.updateTitle()
            }
        }

        function onCurrentImageIndexChanged() {
            if (viewer.shell.state === "viewer") {
                viewer.updateTitle()
            }
        }

        function onCurrentIndexChanged() {
            if (viewer.shell.state === "viewer") {
                if (viewer.sourceMasonry.view.currentIndex < 0 ||
                        viewer.sourceMasonry.view.count === 0) {
                    return
                }
                const currentPath =
                        viewer.pathForIndex(viewer.sourceMasonry.view.currentIndex)
                const currentFileWasPreserved =
                        currentPath !== "" && currentPath === viewer.lastKnownPath
                if (currentFileWasPreserved) {
                    imageViewport.remapImageIndex(
                                viewer.lastKnownIndex,
                                viewer.sourceMasonry.view.currentIndex)
                    viewer.lastKnownIndex = viewer.sourceMasonry.view.currentIndex
                    viewer.lastKnownPath = currentPath
                    viewer.updateTitle()
                    return
                }
                if (viewer.lastKnownPath !== "" && viewer.lastKnownPath !== currentPath) {
                    if (viewer.previousImageLocked) {
                        viewer.restorePreviousImageLockIndexes()
                        if (viewer.sourceMasonry.view.currentIndex !== viewer.previousImageIndex) {
                            viewer.lockedPreviousReturnIndex = viewer.sourceMasonry.view.currentIndex
                            viewer.lockedPreviousReturnPath = viewer.pathForIndex(viewer.sourceMasonry.view.currentIndex)
                        }
                    } else {
                        const remappedPreviousIndex =
                                viewer.indexForPath(viewer.lastKnownPath)
                        if (remappedPreviousIndex !== -1) {
                            viewer.previousImageIndex = remappedPreviousIndex
                            viewer.previousImagePath = viewer.lastKnownPath
                        }
                    }
                }
                viewer.lastKnownIndex = viewer.sourceMasonry.view.currentIndex
                viewer.lastKnownPath = currentPath

                let imageIdUrl = viewer.sourceMasonry.view.indexImageIdUrl(viewer.sourceMasonry.view.currentIndex)
                // console.log("ZZ INDEX CHANGE 2", viewer.sourceMasonry.view.currentIndex, imageIdUrl)
                if (imageIdUrl) {
                    viewer.setImage(imageIdUrl, viewer.sourceMasonry.view.indexOriginalSize(viewer.sourceMasonry.view.currentIndex), viewer.sourceMasonry.view.currentIndex, 0)
                    let viewportRestored = viewer.applyPendingPreviousImageViewport()
                    if (!viewportRestored && viewer.zoomFitView) {
                        imageViewport.zoomToFit(true)
                        // console.log("ZZ FIT ON CHANGE")
                    }
                    else if (!viewportRestored && !viewer.zoomFitView) {
                        imageViewport.fitViewerImageInViewportBounds()
                        // console.log("ZZ ELSE")
                    }
                }
                viewer.onCurrentIndexChanged()
            }
            else {
                viewer.lastKnownIndex = viewer.sourceMasonry.view.currentIndex
                viewer.lastKnownPath = viewer.lastKnownIndex >= 0
                        ? viewer.pathForIndex(viewer.lastKnownIndex) : ""
            }
        }
    }

    Connections {
        target: viewer.decodeModel
        function onViewerImageIdUrlChanged(newImageIdUrl, level) {
            viewer.setImage(newImageIdUrl, viewer.sourceMasonry.view.indexOriginalSize(viewer.sourceMasonry.view.currentIndex), viewer.sourceMasonry.view.currentIndex, level)
            viewer.applyPendingPreviousImageViewport()
        }

        function onViewerImageCacheChanged(index) {
            if (index === viewer.sourceIndexForViewIndex(viewer.viewerNavigationTargetIndex)) {
                viewer.updateViewerNavigationTargetSource()
            }
        }

        function onViewerReset() {
            if (viewer.shell.state !== "viewer") {
                return
            }
            Qt.callLater(function() {
                if (viewer.shell.state !== "viewer" ||
                        viewer.sourceMasonry.view.count === 0 ||
                        viewer.sourceMasonry.view.currentIndex < 0) {
                    return
                }
                const currentIndex = viewer.sourceMasonry.view.currentIndex
                const imageIdUrl =
                    viewer.sourceMasonry.view.indexImageIdUrl(currentIndex)
                if (imageIdUrl) {
                    viewer.setImage(
                                imageIdUrl,
                                viewer.sourceMasonry.view.indexOriginalSize(
                                    currentIndex),
                                currentIndex, 0)
                }
                viewer.onCurrentIndexChanged()
            })
        }
    }


}
