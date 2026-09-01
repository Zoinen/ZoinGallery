pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property Item viewer
    required property FlickableZoomable imageViewport

    function isMotionKey(event) {
        const arrow = event.key === Qt.Key_Left
                || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up
                || event.key === Qt.Key_Down
        return (!viewer.zoomFitView && arrow)
                || event.key === Qt.Key_Plus
                || event.key === Qt.Key_Minus
                || event.key === Qt.Key_Equal
                || event.key === Qt.Key_Control
    }

    function updateHeldMotion() {
        const speed = viewer.controlPressed ? 0.06 : 1
        imageViewport.startZoomScrollingAnimation(
                    viewer.leftPressed ? speed
                        : viewer.rightPressed ? -speed : 0,
                    viewer.upPressed ? speed
                        : viewer.downPressed ? -speed : 0,
                    viewer.zoomInPressed ? speed
                        : viewer.zoomOutPressed ? -speed : 0)
    }

    function handleMotionPressed(event) {
        if (!isMotionKey(event))
            return false
        if (event.isAutoRepeat)
            return true
        if (event.key === Qt.Key_Left)
            viewer.leftPressed = true
        else if (event.key === Qt.Key_Right)
            viewer.rightPressed = true
        else if (event.key === Qt.Key_Up)
            viewer.upPressed = true
        else if (event.key === Qt.Key_Down)
            viewer.downPressed = true
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
            viewer.zoomInPressed = true
        else if (event.key === Qt.Key_Minus)
            viewer.zoomOutPressed = true
        else if (event.key === Qt.Key_Control)
            viewer.controlPressed = true
        updateHeldMotion()
        return true
    }

    function handleSelectionPressed(event, currentIndex) {
        if (event.key === Qt.Key_Shift && !event.isAutoRepeat) {
            viewer.beginShiftSelection()
            return true
        }
        if (event.key === Qt.Key_Backslash) {
            viewer.toggleCurrentSelection()
        } else if (event.key === Qt.Key_Insert) {
            viewer.selectionModel.setSelection(
                        viewer.sourceIndexForViewIndex(currentIndex), true)
        } else if (event.key === Qt.Key_Delete) {
            viewer.selectionModel.setSelection(
                        viewer.sourceIndexForViewIndex(currentIndex), false)
        } else {
            return false
        }
        return true
    }

    function navigationIndexForKey(event) {
        const alt = Boolean(event.modifiers & Qt.AltModifier)
        if (!alt && (event.key === Qt.Key_Left
                || event.key === Qt.Key_PageUp
                || event.key === Qt.Key_Backspace
                || event.key === Qt.Key_Up)) {
            return viewer.sourceMasonry.moveInImageList(false, false)
        }
        if (!alt && (event.key === Qt.Key_Right
                || event.key === Qt.Key_PageDown
                || event.key === Qt.Key_Space
                || event.key === Qt.Key_Down)) {
            return viewer.sourceMasonry.moveInImageList(true, false)
        }
        if (event.key === Qt.Key_Home)
            return viewer.sourceMasonry.moveInImageList(false, true)
        if (event.key === Qt.Key_End)
            return viewer.sourceMasonry.moveInImageList(true, true)
        return -1
    }

    function previousImageIndex(event, currentIndex) {
        if (!viewer.isTildeKey(event))
            return -1
        if (event.modifiers & Qt.ShiftModifier) {
            viewer.togglePreviousImageLock(currentIndex)
            return -1
        }
        if (viewer.previousImageIndex !== -1)
            return viewer.switchToPreviousImage(currentIndex)
        const potentialNext =
                viewer.sourceMasonry.view.nextImageIndex(true, false)
        return potentialNext !== currentIndex
                ? viewer.sourceMasonry.moveInImageList(true, false)
                : viewer.sourceMasonry.moveInImageList(false, false)
    }

    function handleViewerCommand(event) {
        const alt = Boolean(event.modifiers & Qt.AltModifier)
        const control = Boolean(event.modifiers & Qt.ControlModifier)
        if (event.key === Qt.Key_F11 || event.key === Qt.Key_F
                || event.key === Qt.Key_Clear
                || ((event.key === Qt.Key_Return
                     || event.key === Qt.Key_Enter) && alt)) {
            viewer.hostWindow.toggleFullscreen()
        } else if (event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Return
                   || event.key === Qt.Key_Escape
                   || (event.key === Qt.Key_Up && alt)
                   || (event.key === Qt.Key_PageUp && control)) {
            viewer.shell.toggleViewer()
        } else if (event.key === Qt.Key_Asterisk
                   || event.key === Qt.Key_9
                   || (event.key === Qt.Key_1 && control)) {
            imageViewport.zoomTo100()
        } else if (event.key === Qt.Key_2 && control) {
            imageViewport.zoomToScale(0.5)
        } else if (event.key === Qt.Key_3 && control) {
            imageViewport.zoomToScale(0.25)
        } else if (event.key === Qt.Key_0 && control) {
            imageViewport.zoomToFit()
        } else if (event.key === Qt.Key_Z
                   || event.key === Qt.Key_Slash
                   || event.key === Qt.Key_0) {
            imageViewport.toggleZoomToFit()
        } else if (event.key === Qt.Key_Tab) {
            viewer.panelsVisible = !viewer.panelsVisible
        } else if (event.key === Qt.Key_S || event.key === Qt.Key_P) {
            viewer.sphericViewerMode = !viewer.sphericViewerMode
        } else if (event.key === Qt.Key_BracketRight) {
            imageViewport.rotate(1)
            viewer.updateTitle()
        } else if (event.key === Qt.Key_BracketLeft) {
            imageViewport.rotate(3)
            viewer.updateTitle()
        } else if (event.key === Qt.Key_C
                   && viewer.decodeModel.dumpCurrentImage) {
            viewer.decodeModel.dumpCurrentImage()
        }
    }

    function handlePressed(event) {
        const currentIndex = viewer.sourceMasonry.view.currentIndex
        if (handleSelectionPressed(event, currentIndex)
                || handleMotionPressed(event))
            return
        let nextIndex = navigationIndexForKey(event)
        if (nextIndex < 0 && viewer.isTildeKey(event))
            nextIndex = previousImageIndex(event, currentIndex)
        if (nextIndex < 0)
            handleViewerCommand(event)
        if (nextIndex !== -1 && nextIndex !== currentIndex
                && Boolean(event.modifiers & Qt.ShiftModifier)) {
            viewer.updateShiftNavigationSelection(nextIndex)
        }
    }


    function handleReleased(event) {
            if (!event.isAutoRepeat) {
                if (event.key === Qt.Key_Left) {
                    viewer.leftPressed = false
                }
                else if (event.key === Qt.Key_Right) {
                    viewer.rightPressed = false
                }
                else if (event.key === Qt.Key_Up) {
                    viewer.upPressed = false
                }
                else if (event.key === Qt.Key_Down) {
                    viewer.downPressed = false
                }
                else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                    viewer.zoomInPressed = false
                }
                else if (event.key === Qt.Key_Minus) {
                    viewer.zoomOutPressed = false
                }
                else if (event.key === Qt.Key_Control) {
                    viewer.controlPressed = false
                }
                else if (event.key === Qt.Key_Shift) {
                    viewer.finishShiftSelection()
                }
            }

            if (!viewer.zoomFitView && (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up ||
                                 event.key === Qt.Key_Down ||
                                 event.key === Qt.Key_Plus || event.key === Qt.Key_Minus || event.key === Qt.Key_Equal ||
                                 event.key === Qt.Key_Control)) {
                if (event.isAutoRepeat) {
                    return
                }

                let speed = viewer.controlPressed ? 0.06 : 1
                imageViewport.startZoomScrollingAnimation(viewer.leftPressed ? speed : viewer.rightPressed ? -speed : 0,
                                                      viewer.upPressed ? speed : viewer.downPressed ? -speed : 0,
                                                      viewer.zoomInPressed ? speed : viewer.zoomOutPressed ? -speed : 0)
                if (event.key === Qt.Key_Control) {
                    if (!viewer.leftPressed && !viewer.rightPressed && !viewer.upPressed && !viewer.downPressed && !viewer.zoomInPressed && !viewer.zoomOutPressed) {
                        imageViewport.onControlReleased()
                    }
                }
            }
        }


}
