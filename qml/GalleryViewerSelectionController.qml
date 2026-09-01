pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property Item viewer

    function requestCurrentSelection(mode) {
        const entryId = viewer.entryIdAt(viewer.presentedIndex)
        if (entryId === "")
            return false
        viewer.selectionRequested(mode, [entryId])
        return true
    }

    function beginShiftSelection() {
        if (viewer.shiftSelectionActive || !viewer.session || viewer.presentedIndex < 0)
            return
        viewer.shiftSelectionActive = true
        viewer.shiftSelectionAnchorIndex = viewer.presentedIndex
        viewer.shiftSelectionTargetIndex = viewer.presentedIndex
        viewer.shiftSelectionAdds = typeof viewer.session.isSelectedAt === "function"
                ? !viewer.session.isSelectedAt(viewer.presentedIndex) : true
    }

    function updateShiftNavigationSelection(targetIndex) {
        beginShiftSelection()
        if (viewer.shiftSelectionActive && targetIndex >= 0)
            viewer.shiftSelectionTargetIndex = targetIndex
    }

    function finishShiftSelection() {
        if (!viewer.shiftSelectionActive)
            return
        const anchor = viewer.shiftSelectionAnchorIndex
        const target = viewer.shiftSelectionTargetIndex
        viewer.shiftSelectionActive = false
        viewer.shiftSelectionAnchorIndex = -1
        viewer.shiftSelectionTargetIndex = -1
        if (!viewer.session || anchor < 0 || target < 0 || anchor === target)
            return
        const first = Math.min(anchor, target)
        const last = Math.max(anchor, target)
        const entryIds = []
        for (let index = first; index <= last; ++index) {
            const entryId = viewer.entryIdAt(index)
            if (entryId !== "")
                entryIds.push(entryId)
        }
        if (entryIds.length > 0)
            viewer.selectionRequested(viewer.shiftSelectionAdds ? "add" : "remove",
                               entryIds)
    }

    function cancelShiftSelection() {
        viewer.shiftSelectionActive = false
        viewer.shiftSelectionAnchorIndex = -1
        viewer.shiftSelectionTargetIndex = -1
    }


}
