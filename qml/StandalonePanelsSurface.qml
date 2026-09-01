import QtQuick

import QtQuick.Controls
import QtQuick.Layouts
import ZoinGallery 1.0
import ZoinGallery.Native 1.0

pragma ComponentBehavior: Bound

SplitView {
    id: panels

    required property QtObject catalogSession
    required property QtObject navigationController
    required property QtObject catalogModel
    required property QtObject viewModel
    required property QtObject previewModel
    required property QtObject selectedCatalogModel
    property bool selectedImagesPanelOpen: false
    property bool viewerShowAnimationRunning: false
    property var viewerSourceMasonry: null

    readonly property alias masonry: masonryLayout
    readonly property alias selectedPanel: selectedImagesPanel

    signal openViewerRequested(var masonry, var mapper, var decodeModel,
                               var selectionModel, var filmstripModel)
    signal fileDropFailed(string title, string message)
    signal fullscreenRequested()
    signal closeSelectedImagesRequested()

    Layout.fillWidth: true
    Layout.fillHeight: true
    orientation: Qt.Horizontal

    handle: Rectangle {
        id: thumbnailsSplitHandle
        implicitWidth: Math.min(
                           8,
                           Math.max(
                               0,
                               Math.round(
                                   selectedImagesPanelSlot.width /
                                   10)))
        enabled: !selectedImagesPanelSlot.transitioning
        z: 10
        color: "transparent"
        readonly property bool handleHovered: SplitHandle.hovered
        readonly property bool handlePressed: SplitHandle.pressed

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: thumbnailsSplitHandle.handlePressed ? 3 : 1
            color: thumbnailsSplitHandle.handlePressed
                   || thumbnailsSplitHandle.handleHovered
                   ? Style.persistentSelectionBorder
                   : Style.lighter2

            Behavior on width {
                NumberAnimation { duration: 80 }
            }
        }
    }

    onResizingChanged: {
        if (!resizing &&
                panels.selectedImagesPanelOpen &&
                !selectedImagesPanelSlot.transitioning &&
                selectedImagesPanelSlot.width >= 240) {
            selectedImagesPanelSlot.contentWidth =
                    selectedImagesPanelSlot.width
            AppSettings.selectedImagesPanelWidth =
                    selectedImagesPanelSlot.width
        }
    }

    Item {
        id: standaloneGalleryPanel
        objectName: "standaloneGalleryPanel"
        SplitView.fillWidth: true
        SplitView.minimumWidth: 320

        StandaloneGalleryPanel {
            id: masonryLayout
            objectName: "standaloneGalleryFacade"
            anchors.fill: parent
            session: panels.catalogSession
            viewerController: panels.navigationController
            selectionModel: panels.catalogModel
            selectionMapper: panels.viewModel
            viewerTransitionActive:
                panels.viewerShowAnimationRunning &&
                panels.viewerSourceMasonry === masonryLayout

            onToggleViewer: panels.openViewerRequested(
                                masonryLayout, panels.viewModel,
                                panels.catalogModel, panels.catalogModel,
                                panels.previewModel)
            onFileDropFailed: (title, message) =>
                                  panels.fileDropFailed(title, message)
            onFullscreenRequested:
                panels.fullscreenRequested()
        }
    }

    Item {
        id: selectedImagesPanelSlot

        property real contentWidth: Math.max(
                                        240,
                                        AppSettings.selectedImagesPanelWidth)
        property bool transitionVisible: false
        property bool transitioning: false
        readonly property real availableWidth: Math.max(
                                                   0,
                                                   panels.width -
                                                   320 - 8)
        readonly property real minimumOpenWidth: Math.min(
                                                     240,
                                                     availableWidth)

        SplitView.preferredWidth: 0
        SplitView.minimumWidth: transitioning
                                ? 0
                                : (panels.selectedImagesPanelOpen
                                   ? minimumOpenWidth
                                   : 0)
        SplitView.maximumWidth: availableWidth
        visible: transitionVisible
        clip: true

        function setOpen(open) {
            const currentWidth = Math.max(0, width)
            const wasTransitioning = transitioning

            panelWidthAnimation.stop()
            transitioning = true
            transitionVisible = true

            if (open) {
                contentWidth = Math.min(
                            Math.max(
                                minimumOpenWidth,
                                AppSettings.selectedImagesPanelWidth),
                            SplitView.maximumWidth)
                panelWidthAnimation.from = currentWidth
                panelWidthAnimation.to = contentWidth
            }
            else {
                if (!wasTransitioning) {
                    contentWidth = currentWidth
                }
                panelWidthAnimation.from = currentWidth
                panelWidthAnimation.to = 0
            }

            SplitView.preferredWidth = currentWidth
            panelWidthAnimation.restart()
        }

        NumberAnimation {
            id: panelWidthAnimation
            target: selectedImagesPanelSlot.SplitView
            property: "preferredWidth"
            duration: 220
            easing.type: Easing.InOutCubic

            onStopped: {
                selectedImagesPanelSlot.transitioning = false
                if (!panels.selectedImagesPanelOpen &&
                        selectedImagesPanelSlot.width < 1) {
                    selectedImagesPanelSlot.transitionVisible = false
                }
            }
        }

        SelectedImagesPanel {
            id: selectedImagesPanel
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }
            width: selectedImagesPanelSlot.transitioning
                   ? selectedImagesPanelSlot.contentWidth
                   : selectedImagesPanelSlot.width
            transparentGrid: masonryLayout.showTransparentGrid
            viewerTransitionActive:
                panels.viewerShowAnimationRunning &&
                panels.viewerSourceMasonry ===
                    selectedImagesPanel.masonryMode

            onCloseRequested: panels.closeSelectedImagesRequested()
            onImageActivated: panels.openViewerRequested(
                                  selectedImagesPanel.masonryMode,
                                  panels.selectedCatalogModel,
                                  panels.selectedCatalogModel,
                                  panels.selectedCatalogModel,
                                  panels.selectedCatalogModel)
        }
    }

    Connections {
        target: panels

        function onSelectedImagesPanelOpenChanged() {
            selectedImagesPanelSlot.setOpen(
                        panels.selectedImagesPanelOpen)
        }
    }
}
