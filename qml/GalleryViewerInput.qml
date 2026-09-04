pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: root

    required property Item viewer
    required property FlickableZoomable viewport

    function handleSelectionPressed(event) {
        if (event.key === Qt.Key_Shift && !event.isAutoRepeat) {
            viewer.beginShiftSelection()
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Backslash) {
            viewer.requestCurrentSelection("toggle")
        } else if (event.key === Qt.Key_Insert) {
            viewer.requestCurrentSelection("add")
        } else if (event.key === Qt.Key_Delete) {
            viewer.requestCurrentSelection("remove")
        } else {
            return false
        }
        return true
    }

    function isMotionKey(event) {
        const arrow = event.key === Qt.Key_Left
                || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up
                || event.key === Qt.Key_Down
        return (!viewport.zoomFitView && arrow)
                || event.key === Qt.Key_Plus
                || event.key === Qt.Key_Equal
                || event.key === Qt.Key_Minus
                || event.key === Qt.Key_Control
    }

    function handleMotionPressed(event) {
        if (!isMotionKey(event))
            return false
        if (event.isAutoRepeat) {
            event.accepted = true
            return true
        }
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
        viewer.updateHeldKeyMotion()
        return true
    }

    function handleViewerCommand(event, altPressed, controlModifier,
                                 shiftModifier) {
        let navigated = false
        if ((event.key === Qt.Key_Left
                    || event.key === Qt.Key_PageUp
                    || event.key === Qt.Key_Backspace
                    || event.key === Qt.Key_Up) && !altPressed) {
            navigated = viewer.navigate(-1)
        } else if ((event.key === Qt.Key_Right
                    || event.key === Qt.Key_PageDown
                    || event.key === Qt.Key_Space
                    || event.key === Qt.Key_Down) && !altPressed) {
            navigated = viewer.navigate(1)
        } else if (event.key === Qt.Key_Home) {
            navigated = viewer.navigateToEnd(-1)
        } else if (event.key === Qt.Key_End) {
            navigated = viewer.navigateToEnd(1)
        } else if (event.key === Qt.Key_F11 || event.key === Qt.Key_F
                   || event.key === Qt.Key_Clear
                   || ((event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) && altPressed)) {
            viewer.fullscreenToggleRequested()
        } else if (event.key === Qt.Key_Escape) {
            // Esc is a modal escape hatch in f4. Do not make the user wait
            // for the thumbnail return animation before the host can restore
            // the panel surface.
            viewer.requestImmediateClose()
        } else if (event.key === Qt.Key_Enter
                   || event.key === Qt.Key_Return
                   || (event.key === Qt.Key_Up && altPressed)
                   || (event.key === Qt.Key_PageUp && controlModifier)) {
            viewer.requestClose()
        } else if (event.key === Qt.Key_Asterisk
                   || event.key === Qt.Key_9) {
            viewport.zoomTo100()
        } else if (event.key === Qt.Key_1 && controlModifier) {
            viewport.zoomTo100()
        } else if (event.key === Qt.Key_2 && controlModifier) {
            viewport.zoomToScale(0.5)
        } else if (event.key === Qt.Key_3 && controlModifier) {
            viewport.zoomToScale(0.25)
        } else if (event.key === Qt.Key_0 && controlModifier) {
            viewport.zoomToFit()
        } else if (event.key === Qt.Key_Z || event.key === Qt.Key_Slash
                   || event.key === Qt.Key_0) {
            viewport.toggleZoomToFit()
        } else if (event.key === Qt.Key_Tab) {
            viewer.panelsVisible = !viewer.panelsVisible
        } else if (viewer.isTildeKey(event)) {
            if (shiftModifier) {
                viewer.togglePreviousImageLock(viewer.presentedIndex)
                event.accepted = true
                return
            }
            navigated = viewer.togglePreviousImage()
        } else if (event.key === Qt.Key_S || event.key === Qt.Key_P) {
            viewer.sphericViewerMode = !viewer.sphericViewerMode
        } else if (event.key === Qt.Key_BracketRight) {
            viewport.rotate(1)
        } else if (event.key === Qt.Key_BracketLeft) {
            viewport.rotate(3)
        }
        return navigated
    }

    function handlePressed(event) {
        if (!viewer.ownsKey(event)) {
            event.accepted = false
            return
        }
        const altPressed = Boolean(event.modifiers & Qt.AltModifier)
        const controlModifier = Boolean(
                                  event.modifiers & Qt.ControlModifier)
        const shiftModifier = Boolean(event.modifiers & Qt.ShiftModifier)
        if (event.key === Qt.Key_Shift) {
            if (handleSelectionPressed(event))
                event.accepted = true
            return
        }
        if (shiftModifier)
            viewer.beginShiftSelection()
        if (handleSelectionPressed(event)
                || handleMotionPressed(event)) {
            event.accepted = true
            return
        }

        const navigated = handleViewerCommand(
                            event, altPressed, controlModifier, shiftModifier)
        if (navigated && shiftModifier)
            viewer.updateShiftNavigationSelection(viewer.presentedIndex)
        event.accepted = true
    }

    function handleReleased(event) {
        if (viewer.customContent || !viewer.ownsKey(event)) {
            event.accepted = false
            return
        }
        if (event.isAutoRepeat)
            return
        let heldMotionKey = true
        if (event.key === Qt.Key_Left)
            viewer.leftPressed = false
        else if (event.key === Qt.Key_Right)
            viewer.rightPressed = false
        else if (event.key === Qt.Key_Up)
            viewer.upPressed = false
        else if (event.key === Qt.Key_Down)
            viewer.downPressed = false
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
            viewer.zoomInPressed = false
        else if (event.key === Qt.Key_Minus)
            viewer.zoomOutPressed = false
        else if (event.key === Qt.Key_Control)
            viewer.controlPressed = false
        else if (event.key === Qt.Key_Shift) {
            viewer.finishShiftSelection()
            heldMotionKey = false
        } else {
            heldMotionKey = false
        }
        if (!heldMotionKey) {
            event.accepted = true
            return
        }
        if (!viewport.zoomFitView) {
            viewer.updateHeldKeyMotion()
            if (event.key === Qt.Key_Control
                    && !viewer.leftPressed && !viewer.rightPressed
                    && !viewer.upPressed && !viewer.downPressed
                    && !viewer.zoomInPressed && !viewer.zoomOutPressed)
                viewport.onControlReleased()
        }
        event.accepted = true
    }
}
