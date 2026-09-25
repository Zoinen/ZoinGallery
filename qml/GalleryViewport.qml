pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import ZoinGallery.Native 1.0

Item {
    id: viewportRoot

    required property Item panelRoot
    required property GalleryPanelController controller
    property real contentHorizontalInset: 0
    property bool presentationStateReady: false
    readonly property real groupHeaderScrollClearance:
        galleryLayout.presentationMode !== GalleryViewportItem.Columns
            && panelRoot.groupHeaderRightInset > 0
        ? panelRoot.groupHeaderRightInset + 4 : 0

    property alias layout: galleryLayout
    property alias targetHeight: galleryLayout.targetHeight
    property alias density: galleryLayout.density
    property alias detailsHeader: detailsHeader
    property alias pointerLayer: pointerLayer
    property alias scrollingStarted: pointerLayer.scrollingStarted
    property alias scrollingStartedAtY: pointerLayer.scrollingStartedAtY
    property alias scrollingMode: pointerLayer.scrollingMode
    readonly property font iconLabelFont: iconLabelFontProbe.font
    readonly property bool advancedEntryActions:
        controller.dragEnabled || controller.directoryDropEnabled
        || controller.canRemoveEntries

    function nativePresentationMode(mode) {
        switch (mode) {
        case "columns": return GalleryViewportItem.Columns
        case "details": return GalleryViewportItem.Details
        case "grid": return GalleryViewportItem.Grid
        case "icons": return GalleryViewportItem.Icons
        default: return GalleryViewportItem.Masonry
        }
    }

    function applyPresentationState(switchingMode, requestedMode) {
        const panel = viewportRoot.panelRoot
        const mode = requestedMode === undefined
                ? panel.presentationMode : String(requestedMode)
        const details = mode === "details"
        const verticalContentInset = mode === "masonry"
                || mode === "grid" || mode === "icons"
        const embeddingInset = Math.max(
                    0, Number(contentHorizontalInset) || 0)
        const horizontalPadding = details ? 0 : 6 + embeddingInset

        panel.beginPresentationStateUpdate(Boolean(switchingMode))
        try {
            detailsHeader.visible = details && panel.showDetailsHeader
            galleryLayout.anchors.leftMargin = 0
            galleryLayout.anchors.topMargin = detailsHeader.visible
                    ? detailsHeader.height
                    : (verticalContentInset || details ? 0 : 6)
            galleryLayout.anchors.bottomMargin = details
                    || verticalContentInset ? 0 : 6
            galleryLayout.anchors.rightMargin = 0
            galleryLayout.paddingLeft = horizontalPadding
            galleryLayout.paddingRight = horizontalPadding
            galleryLayout.paddingTop = verticalContentInset ? 6 : 0
            galleryLayout.paddingBottom = verticalContentInset ? 6 : 0
            galleryLayout.presentationMode = nativePresentationMode(mode)
            if (panel.benchmarkTracingEnabled)
                panel.traceBenchmarkStage(
                            "layout.content-inset",
                            {"fix": "[FIX:panel-content-inset]",
                             "embeddingInset": embeddingInset,
                             "paddingLeft": horizontalPadding,
                             "paddingRight": horizontalPadding,
                             "layoutX": galleryLayout.x,
                             "layoutWidth": galleryLayout.width})
        } finally {
            panel.endPresentationStateUpdate(true)
        }
    }

    // The shared pointer layer is deliberately above the materialized
    // delegates, so it remains the single hit-test owner while rows recycle.
    // Group headers are children of the layout and cannot receive a press
    // from that sibling layer. Resolve the bounded header model here before
    // asking the native layout for a file index.
    function groupHeaderKeyAtPointer(pointerX, pointerY) {
        if (!galleryLayout || !galleryLayout.visibleGroupHeaders
                || galleryLayout.visibleGroupHeaders.length === 0)
            return ""
        const point = groupHeaderLayer.mapFromItem(
                    pointerLayer, pointerX, pointerY)
        const headers = galleryLayout.visibleGroupHeaders
        const horizontal = galleryLayout.presentationMode
                === GalleryViewportItem.Columns
        const dpr = Math.max(0.01, Number(viewportRoot.panelRoot.devicePixelRatio) || 1)
        const topSpacing = Math.round(8 * dpr) / dpr
        for (let i = 0; i < headers.length; ++i) {
            const data = headers[i]
            const offset = Number(data.offset || 0)
            const extent = Number(data.extent || 0)
            const width = Number(data.width || 0)
            const height = Number(data.height || 0)
            let rawX = Number(data.x || 0)
            let rawY = Number(data.y || 0)
            if (!horizontal) {
                // The native header model is ordered by section offset, so
                // the immediate successor is the only header that can push
                // this one away. Keep pointer lookup O(H), not O(H^2).
                const nextOffset = i + 1 < headers.length
                        ? Number(headers[i + 1].offset || 0)
                        : Number.POSITIVE_INFINITY
                const sticky = offset + topSpacing <= galleryLayout.contentY
                        && nextOffset + topSpacing > galleryLayout.contentY
                if (sticky)
                    rawY = Math.min(
                                -topSpacing,
                                nextOffset - galleryLayout.contentY - height)
            } else {
                const groupOffset = Number(data.groupOffset !== undefined
                                           ? data.groupOffset : offset)
                const groupExtent = Number(data.groupExtent !== undefined
                                           ? data.groupExtent : extent)
                const groupLeft = groupOffset - galleryLayout.contentY
                const groupRight = groupLeft + groupExtent
                rawX = Math.max(groupLeft,
                                Math.min(rawX, groupRight - width))
            }
            const origin = groupHeaderLayer.sceneOrigin
            rawX = Math.round((origin.x + rawX) * dpr) / dpr - origin.x
            rawY = Math.round((origin.y + rawY) * dpr) / dpr - origin.y
            const right = Math.round((origin.x + rawX + width
                                     - groupHeaderScrollClearance) * dpr) / dpr - origin.x
            if (point.x >= rawX && point.x < right
                    && point.y >= rawY + topSpacing && point.y < rawY + height)
                return String(data.key || "")
        }
        return ""
    }

    function groupHeaderLucideIconSource(name, size, tint) {
        const panel = viewportRoot.panelRoot
        const provider = panel.iconProvider
        const dpr = panel.devicePixelRatio > 0
                ? panel.devicePixelRatio : 1
        if (provider && typeof provider.rasterizedLucideSource === "function")
            return provider.rasterizedLucideSource(name, size, dpr, tint)
        const resolver = panel.iconResolver
        if (resolver && typeof resolver.resolve === "function")
            return resolver.resolve(name, "", false, false, false, false)
        return ""
    }

    Rectangle {
        anchors.fill: parent
        color: viewportRoot.panelRoot.backgroundColor
    }

    // Capture empty-space presses inside the semantic panel. Without this,
    // an unhandled click can reach an embedder's hidden fallback surface.
    MouseArea {
        objectName: "galleryBackgroundPointerArea"
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => {
            viewportRoot.panelRoot.forceActiveFocus()
            viewportRoot.panelRoot.activateRequested()
            mouse.accepted = true
        }
    }

    GalleryDetailsHeader {
        id: detailsHeader
        visible: false
        anchors.left: parent.left
        anchors.top: parent.top
        width: viewportRoot.panelRoot.fileFieldPresentationHelper.detailsPixelExtent(parent.width)
        height: viewportRoot.panelRoot.detailsHeaderHeight
        pixelGridOffset: galleryLayout.parentPixelGridOffset
        z: 5
        columnSchema: viewportRoot.panelRoot.columnSchema
        hoverColor: viewportRoot.panelRoot.headerHoverColor
        textColor: viewportRoot.panelRoot.headerTextColor
        mutedTextColor: viewportRoot.panelRoot.mutedColor
        separatorColor: viewportRoot.panelRoot.separatorColor
        cellInset: viewportRoot.panelRoot.detailsHeaderCellInset
        separatorWidth: viewportRoot.panelRoot.detailsSeparatorWidth
        separatorVerticalMargin:
            viewportRoot.panelRoot.detailsSeparatorVerticalMargin
        textPixelSize: viewportRoot.panelRoot.detailsHeaderFontPixelSize
        devicePixelRatio: viewportRoot.panelRoot.devicePixelRatio
        onSortRequested: (sortMode, contextMenu) =>
            viewportRoot.panelRoot.sortRequested(sortMode, contextMenu)
        onColumnResizePreviewed: columns =>
            viewportRoot.panelRoot.previewColumnSchema(columns)
        onColumnResizeRequested: columns =>
            viewportRoot.panelRoot.columnResizeRequested(columns)
    }

    Label {
        id: iconLabelFontProbe
        visible: false
        text: "M"
    }

    GalleryViewportItem {
        id: galleryLayout
        objectName: "galleryViewportItem"
        clip: true
        persistSettings: false
        visible: !viewportRoot.panelRoot.presentationLayoutHidden
        opacity: viewportRoot.panelRoot.pathViewportPlacementPending ? 0 : 1
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            leftMargin: 0
            topMargin: 0
            bottomMargin: 0
            rightMargin: 0
        }
        paddingTop: 6
        paddingBottom: 6
        model: viewportRoot.controller.catalogModel
        groupRanges: viewportRoot.panelRoot.groupRanges
        groupHeaderHeight: viewportRoot.panelRoot.groupHeaderHeight
        groups: viewportRoot.panelRoot.groupDescriptors
        groupStateKey: viewportRoot.panelRoot.groupStateKey
        groupSession: viewportRoot.controller.session
        currentIndex: viewportRoot.controller.currentIndex
        presentationMode: GalleryViewportItem.Masonry
        columnCount: Math.max(
                         2, Math.min(3,
                                     viewportRoot.panelRoot.columnCount))
        spacing: viewportRoot.panelRoot.itemSpacing
        listView: viewportRoot.panelRoot.listView
        showTransparentGrid: viewportRoot.panelRoot.showTransparentGrid
        animateResizing: viewportRoot.panelRoot.animateLayoutChanges
                         && !viewportRoot.panelRoot.presentationSwitchPending
                         && !viewportRoot.panelRoot.pathViewportPlacementPending
        devicePixelRatio: viewportRoot.panelRoot.devicePixelRatio
        iconLabelFont: viewportRoot.iconLabelFont
        deferDelegateRefreshOnReset:
            !viewportRoot.controller.catalogReady

        onLayoutReset: {
            const panel = viewportRoot.panelRoot
            panel.traceBenchmarkStage("layout.reset", {})
            panel.resetMasonryPageSequence()
            if (panel.cursorChromeTransitionActive)
                panel.scheduleCursorChromeLayoutRetarget()
            panel.resetCurrentItemCenter(
                        viewportRoot.controller.currentIndex)
            if (!panel.panelScrollAnimationRunning)
                panel.pendingVisualCursorIndex = -1
            panel.coordinateVisualCursor(
                        viewportRoot.controller.currentIndex,
                        panel.visualCursorIndex)
            if (!panel.presentationSwitchPending) {
                if (panel.pathViewportPlacementPending)
                    panel.schedulePathViewportPlacement("layout-reset")
                else if (!panel.densityViewportTransaction)
                    panel.scheduleViewportUpdate(false)
            }
            panel.refreshHoveredIndex()
        }
        onVisibleIndexesChanged: {
            viewportRoot.panelRoot.publishMetadataVisibleRange()
            viewportRoot.panelRoot.refreshHoveredIndex()
        }
        onCountChanged: {
            const panel = viewportRoot.panelRoot
            panel.traceBenchmarkStage("layout.count.changed", {})
            panel.cancelCursorChromeTransition()
            panel.resetCurrentItemCenter(
                        viewportRoot.controller.currentIndex)
            if (!panel.panelScrollAnimationRunning)
                panel.pendingVisualCursorIndex = -1
            panel.coordinateVisualCursor(
                        viewportRoot.controller.currentIndex,
                        panel.visualCursorIndex)
            if (panel.pathViewportPlacementPending)
                panel.schedulePathViewportPlacement("count-changed")
            else
                panel.scheduleViewportUpdate(false)
        }
        onContentHeightChanged: {
            const panel = viewportRoot.panelRoot
            panel.traceBenchmarkStage("layout.content-height.changed", {})
            panel.resetMasonryPageSequence()
            if (panel.pathViewportPlacementPending)
                panel.schedulePathViewportPlacement("content-height-changed")
            else if (!panel.densityViewportTransaction)
                panel.scheduleViewportUpdate(false)
        }
        onContentYChanged: {
            viewportRoot.panelRoot.updateVisualCursorForViewport()
            viewportRoot.panelRoot.refreshHoveredIndex()
            if (viewportRoot.panelRoot.pathViewportPlacementPending) {
                viewportRoot.panelRoot.traceBenchmarkStage(
                            "layout.content-y.changed", {})
            }
        }
        onWidthChanged: {
            viewportRoot.panelRoot.refreshHoveredIndex()
            viewportRoot.panelRoot.resetMasonryPageSequence()
            if (width > 0 && count > 0)
                viewportRoot.panelRoot.scheduleThumbnailResizeDecode()
        }
        onHeightChanged: {
            viewportRoot.panelRoot.refreshHoveredIndex()
            viewportRoot.panelRoot.invalidateMasonryPageGeometry()
        }
        onDensityChanged: {
            viewportRoot.panelRoot.resetMasonryPageSequence()
            viewportRoot.panelRoot.viewportController.densityLayoutCommitted()
        }
        onLayoutBandsChanged: {
            if (galleryLayout.presentationMode === GalleryViewportItem.Masonry) {
                viewportRoot.panelRoot.invalidateMasonryPageGeometry()
            } else if (viewportRoot.panelRoot.cursorChromeTransitionActive) {
                viewportRoot.panelRoot.scheduleCursorChromeLayoutRetarget()
            }
        }

        delegate: viewportRoot.advancedEntryActions
                  ? interactiveEntryDelegate : standardEntryDelegate

        Component {
            id: standardEntryDelegate
            GalleryEntryDelegateBase {
                panelRoot: viewportRoot.panelRoot
            }
        }

        Component {
            id: interactiveEntryDelegate
            GalleryInteractiveEntryDelegate {
                panelRoot: viewportRoot.panelRoot
            }
        }
    }

    // Section headers are layout elements, not catalog rows. The native
    // layout only returns the viewport/overscan intersection, so this layer
    // stays bounded even for very large grouped catalogs.
    Item {
        id: groupHeaderLayer
        readonly property point sceneOrigin: {
            // mapToItem does not subscribe to ancestor translations itself.
            let dependency = 0
            for (let ancestor = groupHeaderLayer; ancestor; ancestor = ancestor.parent)
                dependency += ancestor.x + ancestor.y + ancestor.scale + ancestor.rotation
            return groupHeaderLayer.mapToItem(null, dependency * 0, dependency * 0)
        }
        objectName: "galleryGroupHeaderLayer"
        parent: viewportRoot
        x: galleryLayout.x
        y: galleryLayout.y
        width: galleryLayout.width
        height: galleryLayout.height
        z: 8
        clip: true

        Repeater {
            model: galleryLayout.visibleGroupHeaders

            delegate: Item {
                id: groupHeader
                required property var modelData
                required property int index
                readonly property string groupKey:
                    String(modelData.key || "")
                readonly property bool horizontal:
                    galleryLayout.presentationMode === GalleryViewportItem.Columns
                readonly property bool currentSticky: {
                    if (horizontal
                            || Number(modelData.offset) + topSpacing
                               > galleryLayout.contentY)
                        return false
                    const headers = galleryLayout.visibleGroupHeaders
                    for (let i = 0; i < headers.length; ++i) {
                        if (Number(headers[i].offset) > Number(modelData.offset)
                                && Number(headers[i].offset) + topSpacing
                                   <= galleryLayout.contentY)
                            return false
                    }
                    return true
                }
                readonly property real nextHeaderViewportPosition: {
                    let next = Number.POSITIVE_INFINITY
                    const headers = galleryLayout.visibleGroupHeaders
                    for (let i = 0; i < headers.length; ++i) {
                        const candidate = Number(headers[i].offset)
                        if (candidate > Number(modelData.offset))
                            next = Math.min(next, candidate - galleryLayout.contentY)
                    }
                    return next
                }
                readonly property real horizontalGroupLeft:
                    Number(modelData.groupOffset !== undefined
                           ? modelData.groupOffset : modelData.offset)
                    - galleryLayout.contentY
                readonly property real horizontalGroupRight:
                    horizontalGroupLeft
                    + Number(modelData.groupExtent !== undefined
                             ? modelData.groupExtent : modelData.extent)
                readonly property string toggleIconName:
                    modelData.collapsed ? "chevron-right" : "chevron-down"
                // Keep pinned headings legible over scrolling content with a
                // flat, opaque theme-derived fill. Ordinary headings retain
                // the regular header color; hover uses its own flat fill.
                readonly property color backgroundColor:
                    currentSticky
                    ? Qt.lighter(
                          viewportRoot.panelRoot.opaqueHeaderBackgroundColor,
                          1.2)
                    : viewportRoot.panelRoot.headerColor
                readonly property url toggleIconSource:
                    viewportRoot.groupHeaderLucideIconSource(
                        toggleIconName, 14,
                        viewportRoot.panelRoot.headerTextColor)
                readonly property real rawX: horizontal
                    ? Math.max(horizontalGroupLeft,
                               Math.min(Number(modelData.x || 0),
                                        horizontalGroupRight - width))
                    : Number(modelData.x || 0)
                readonly property real rawY: currentSticky
                   ? Math.min(-topSpacing,
                              nextHeaderViewportPosition - height)
                   : Number(modelData.y || 0)
                readonly property point layerSceneOrigin:
                    groupHeaderLayer.sceneOrigin
                readonly property real snappedX:
                    Math.round((layerSceneOrigin.x + rawX) * hostDpr)
                        / hostDpr - layerSceneOrigin.x
                readonly property real snappedY:
                    Math.round((layerSceneOrigin.y + rawY) * hostDpr)
                        / hostDpr - layerSceneOrigin.y
                function snapLogical(value) {
                    return Math.round(value * hostDpr) / hostDpr
                }
                readonly property real topSpacing: snapLogical(8)
                readonly property real interactiveWidth:
                    Math.max(0, snapLogical(width - viewportRoot.groupHeaderScrollClearance))
                x: snappedX
                y: snappedY
                width: snapLogical(Number(modelData.width || 0))
                height: snapLogical(Number(modelData.height || 0))
                objectName: "galleryGroupHeader-"
                            + viewportRoot.panelRoot.groupHeaderObjectSuffix
                            + "-" + groupKey
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: String(modelData.title || "")
                                 + ", " + String(modelData.count || 0)
                                 + " items"
                Accessible.description: modelData.collapsed
                                        ? "Collapsed group. Activate to expand."
                                        : "Expanded group. Activate to collapse."
                Accessible.focusable: true
                Accessible.focused: activeFocus
                Accessible.onPressAction:
                    viewportRoot.panelRoot.toggleGalleryGroup(groupKey)
                onCurrentStickyChanged: {
                    const panel = viewportRoot.panelRoot
                    if (panel.benchmarkTracingEnabled)
                        panel.traceBenchmarkStage("group-header.sticky",
                            {"fix": "[FIX:sticky-header-background]",
                             "key": groupKey,
                             "sticky": currentSticky,
                             "backgroundColor": backgroundColor,
                             "pointerHovered": pointerHovered,
                             "contentY": galleryLayout.contentY})
                }

                Rectangle {
                    id: groupHeaderBackground
                    objectName: groupHeader.objectName + "-background"
                    anchors.fill: parent
                    z: -1
                    color: groupHeader.backgroundColor
                }

                Rectangle {
                    objectName: groupHeader.objectName + "-hover"
                    y: groupHeader.topSpacing
                    width: groupHeader.interactiveWidth
                    height: parent.height - y
                    z: 0
                    color: viewportRoot.panelRoot.headerHoverColor
                    visible: groupHeaderPointer.containsMouse || groupHeader.pointerHovered
                }

                Rectangle {
                    objectName: groupHeader.objectName + "-separator"
                    parent: viewportRoot
                    visible: !groupHeader.currentSticky
                    readonly property real viewportSceneX:
                        groupHeader.layerSceneOrigin.x - galleryLayout.x
                    readonly property real scrollClearance:
                        viewportRoot.groupHeaderScrollClearance
                    x: groupHeader.snapLogical(viewportSceneX) - viewportSceneX
                    y: galleryLayout.y + groupHeader.y + groupHeader.topSpacing
                    width: Math.max(0, groupHeader.snapLogical(
                        viewportSceneX + viewportRoot.width - scrollClearance)
                        - groupHeader.snapLogical(viewportSceneX))
                    height: groupHeader.hostDpr > 0
                            ? 1 / groupHeader.hostDpr : 1
                    color: viewportRoot.panelRoot.separatorColor
                    z: groupHeaderLayer.z
                }

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter
                            || event.key === Qt.Key_Space) {
                        viewportRoot.panelRoot.toggleGalleryGroup(groupKey)
                        event.accepted = true
                    }
                }

                readonly property real hostDpr:
                    viewportRoot.panelRoot.devicePixelRatio > 0
                    ? viewportRoot.panelRoot.devicePixelRatio : 1
                readonly property bool pointerHovered: {
                    const panel = viewportRoot.panelRoot
                    if (!panel.hoverPointerInside)
                        return false
                    const point = pointerLayer.mapFromItem(
                                panel, panel.hoverPointerX,
                                panel.hoverPointerY)
                    return viewportRoot.groupHeaderKeyAtPointer(
                                point.x, point.y) === groupKey
                    }

                    Item {
                        id: groupHeaderContent
                        anchors.fill: parent
                        z: 1
                        anchors.topMargin: groupHeader.topSpacing
                        anchors.leftMargin: groupHeader.snapLogical(8)
                        anchors.rightMargin: groupHeader.snapLogical(Math.max(8,
                            groupHeader.horizontal ? 0
                            : viewportRoot.panelRoot.groupHeaderRightInset + 4))

                    Text {
                        id: groupHeaderTitle
                        objectName: groupHeader.objectName + "-title"
                        anchors.left: parent.left
                        width: Math.max(0, Math.min(
                            Math.ceil(implicitWidth * groupHeader.hostDpr) / groupHeader.hostDpr,
                            groupHeaderCount.x - groupHeaderChevron.width
                                - groupHeader.snapLogical(14)))
                        height: implicitHeight
                        readonly property rect inkBounds: titleMetrics.tightBoundingRect(text)
                        y: groupHeader.snapLogical(parent.height / 2 - baselineOffset
                                                   - inkBounds.y - inkBounds.height / 2)
                        FontMetrics {
                            id: titleMetrics
                            font: groupHeaderTitle.font
                        }
                        text: String(modelData.title || "")
                        color: viewportRoot.panelRoot.headerTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignTop
                        renderType: Text.NativeRendering
                        ToolTip.visible: (groupHeaderPointer.containsMouse
                                          || groupHeader.pointerHovered)
                                         && truncated
                        ToolTip.text: text
                        readonly property bool truncated:
                            paintedWidth > width
                    }
                    Image {
                        id: groupHeaderChevron
                        objectName: groupHeader.objectName + "-chevron"
                        anchors.left: groupHeaderTitle.right
                        anchors.leftMargin: groupHeader.snapLogical(6)
                        width: groupHeader.snapLogical(14)
                        height: width
                        y: groupHeader.snapLogical((parent.height - height) / 2)
                        source: groupHeader.toggleIconSource
                        // Qt applies the window DPR to sourceSize. Supplying
                        // physical pixels here scales twice and resamples the glyph.
                        sourceSize: Qt.size(14, 14)
                        fillMode: Image.PreserveAspectFit
                        smooth: false
                        mipmap: false
                    }
                    Text {
                        id: groupHeaderCount
                        objectName: groupHeader.objectName + "-count"
                        anchors.right: parent.right
                        anchors.rightMargin: groupHeader.snapLogical(4)
                        width: Math.ceil(implicitWidth * groupHeader.hostDpr) / groupHeader.hostDpr
                        height: implicitHeight
                        readonly property rect inkBounds: countMetrics.tightBoundingRect(text)
                        y: groupHeader.snapLogical(parent.height / 2 - baselineOffset
                                                   - inkBounds.y - inkBounds.height / 2)
                        FontMetrics {
                            id: countMetrics
                            font: groupHeaderCount.font
                        }
                        text: String(modelData.count || 0)
                        color: viewportRoot.panelRoot.mutedColor
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignTop
                        renderType: Text.NativeRendering
                    }
                }

                MouseArea {
                    id: groupHeaderPointer
                    anchors.fill: parent
                    z: 2
                    anchors.topMargin: groupHeader.topSpacing
                    anchors.rightMargin: groupHeader.width - groupHeader.interactiveWidth
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: viewportRoot.panelRoot.toggleGalleryGroup(groupKey)
                }
            }
        }
    }

    GalleryCursorChrome {
        parent: galleryLayout
        anchors.fill: parent
        active: viewportRoot.panelRoot.cursorChromeTransitionActive
        cursorVisible: viewportRoot.panelRoot.showCursor
        geometry: viewportRoot.panelRoot.cursorChromeRect
        cornerRadius: viewportRoot.panelRoot.cursorChromeRadius
        fillColor: viewportRoot.panelRoot.cursorChromeFillColor
        outlineColor: viewportRoot.panelRoot.cursorChromeBorderColor
        outlineWidth: viewportRoot.panelRoot.cursorChromeBorderWidth
    }

    Label {
        anchors.centerIn: parent
        visible: viewportRoot.panelRoot.emptyStateEnabled
                 && galleryLayout.count === 0
        text: viewportRoot.panelRoot.emptyStateText
        color: viewportRoot.panelRoot.mutedColor
    }

    GalleryPointerLayer {
        id: pointerLayer
        anchors.fill: parent
        layout: galleryLayout
        wheelMode: viewportRoot.panelRoot.mouseWheelMode
        presentationMode: viewportRoot.panelRoot.presentationMode
        densityAdjustmentEnabled:
            viewportRoot.panelRoot.densityAdjustmentEnabled
        primarySelectionEnabled:
            viewportRoot.panelRoot.controllerReady
            && !viewportRoot.controller.dragEnabled
            && !viewportRoot.controller.directoryDropEnabled
            && !viewportRoot.controller.canRemoveEntries
        autoScrollExtent: viewportRoot.panelRoot.Window.window
                          ? viewportRoot.panelRoot.Window.window.height
                          : (presentationMode === "columns"
                             ? galleryLayout.width : galleryLayout.height)

        onWheelRequested: (x, y, pixelDeltaY, angleDeltaY, modifiers,
                           pixelDeltaX, angleDeltaX) =>
            viewportRoot.panelRoot.handlePanelWheel(
                pixelDeltaY, angleDeltaY, modifiers,
                pixelDeltaX, angleDeltaX)
        onConsoleWheelRequested: (x, y, angleDeltaY, modifiers) =>
            viewportRoot.panelRoot.consoleWheelRequested(
                x, y, angleDeltaY, modifiers)
        onHoverMoved: (x, y) =>
            viewportRoot.panelRoot.updateHoveredIndexAt(x, y)
        onHoverExited: viewportRoot.panelRoot.clearHoveredIndex()
        onMiddlePressed: (x, y, modifiers) =>
            viewportRoot.panelRoot.handlePanelMiddlePress(x, y, modifiers)
        onMiddleReleased: (x, y, modifiers) =>
            viewportRoot.panelRoot.handlePanelMiddleRelease(x, y, modifiers)
        onConsoleMiddleCanceled: (x, y) =>
            viewportRoot.panelRoot.consoleMouseButtonRequested(
                x, y, Qt.MiddleButton, false, Qt.NoModifier)
        onPinchStarted: viewportRoot.panelRoot.beginThumbnailPinch()
        onPinchUpdated: scale =>
            viewportRoot.panelRoot.updateThumbnailPinch(scale)
        onPinchFinished: viewportRoot.panelRoot.finishThumbnailPinch()
        onPrimaryPressed: (x, y, button, modifiers) => {
            const groupKey = viewportRoot.groupHeaderKeyAtPointer(x, y)
            if (groupKey !== "") {
                viewportRoot.panelRoot.traceBenchmarkStage(
                            "group.header.pointer",
                            {"fix": "[FIX:grouped-pointer-routing]",
                             "key": groupKey,
                             "button": button})
                viewportRoot.panelRoot.forceActiveFocus()
                viewportRoot.panelRoot.toggleGalleryGroup(groupKey)
                return
            }
            const point = galleryLayout.mapFromItem(pointerLayer, x, y)
            const index = galleryLayout.indexAtViewport(point.x, point.y)
            if (index >= 0) {
                viewportRoot.panelRoot.handlePointerPress(index, button,
                                                          modifiers)
            } else {
                viewportRoot.panelRoot.forceActiveFocus()
                viewportRoot.panelRoot.activateRequested()
            }
        }
        onPrimaryDragged: (x, y) => {
            const point = pointerLayer.mapToItem(
                        viewportRoot.panelRoot, x, y)
            viewportRoot.panelRoot.handlePointerDrag(point.x, point.y)
        }
        onPrimaryReleased: viewportRoot.panelRoot.endPointerDrag()
        onPrimaryDoubleClicked: (x, y, button) => {
            const point = galleryLayout.mapFromItem(pointerLayer, x, y)
            const index = galleryLayout.indexAtViewport(point.x, point.y)
            if (index < 0)
                return
            if ((button & Qt.LeftButton) !== 0)
                viewportRoot.panelRoot.selectIndex(index, true)
            else if ((button & Qt.RightButton) !== 0)
                viewportRoot.panelRoot.invertPanelSelection()
        }
    }

    Connections {
        target: viewportRoot.panelRoot
        function onShowDetailsHeaderChanged() {
            viewportRoot.applyPresentationState(false)
        }
    }

    onContentHorizontalInsetChanged: {
        if (presentationStateReady)
            applyPresentationState(false)
    }

    Component.onCompleted: {
        presentationStateReady = true
        applyPresentationState(false, viewportRoot.panelRoot.presentationMode)
    }
}
