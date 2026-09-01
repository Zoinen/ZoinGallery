pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property Item viewer
    required property FlickableZoomable viewport
    required property GalleryViewerMotion motion

    visible: false

    Connections {
        target: root.viewer.session
        ignoreUnknownSignals: true

        function onCurrentIndexChanged() {
            if (!root.viewer.session)
                return
            const nextEntryId = root.viewer.session.cursorEntryId
            const authorityConfirmed =
                    root.viewer.pendingAuthorityEntryId !== ""
                    && root.viewer.pendingAuthorityEntryId === nextEntryId
            if (authorityConfirmed) {
                root.viewer.pendingAuthorityIndex = -1
                root.viewer.pendingAuthorityEntryId = ""
                root.motion.authorityTimer.stop()
            }
            if (!authorityConfirmed
                    && root.viewer.presentedEntryId !== nextEntryId) {
                root.viewer.recordPresentedTransition(
                            root.viewer.presentedEntryId, nextEntryId, false)
            }
            root.viewer.resetViewerNavigation()
            root.viewer.setPresentedIndex(
                        root.viewer.session.currentIndex, false)
        }

        function onViewerSourceChanged() {
            root.viewer.refreshCurrentSource()
        }

        function onViewerSourceAtChanged(index) {
            if (index === root.viewer.presentedIndex)
                root.viewer.refreshCurrentSource()
            if (index === root.viewer.viewerNavigationTargetIndex)
                root.viewer.refreshNeighborSource()
        }

        function onCatalogRevisionChanged() {
            if (root.viewer.session.currentIndex < 0) {
                root.viewer.presentedIndex = -1
                root.viewer.presentedEntryId = ""
                root.viewer.refreshCurrentSource()
                return
            }

            const remappedPresented = root.viewer.indexForEntryId(
                        root.viewer.presentedEntryId)
            if (remappedPresented < 0) {
                root.viewer.setPresentedIndex(
                            root.viewer.session.currentIndex, false)
                return
            }

            const previousIndex = root.viewer.presentedIndex
            if (previousIndex !== remappedPresented) {
                root.viewport.remapImageIndex(previousIndex,
                                              remappedPresented)
                if (root.viewer.appliedPresentedIndex === previousIndex)
                    root.viewer.appliedPresentedIndex = remappedPresented
                root.viewer.presentedIndex = remappedPresented
            }
            if (root.viewer.pendingAuthorityEntryId !== "") {
                root.viewer.pendingAuthorityIndex =
                        root.viewer.indexForEntryId(
                            root.viewer.pendingAuthorityEntryId)
            }
            root.viewer.refreshCurrentSource()
            root.viewer.requestImage()
        }

        function onViewerPreviousStateChanged() {
            const previous = root.viewer.previousViewport()
            if (previous.targetEntryId === root.viewer.presentedEntryId)
                root.motion.previousViewportTimer.restart()
        }
    }
}
