pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import "../ZGStyle" as ZGS

Item {
    id: pathRoot
    property string text
    // Embedded hosts can own navigation while reusing the complete original
    // path control. The standalone application keeps its historical
    // viewerController/masonryLayout transaction when no callback is set.
    property var navigationHandler: null
    // Embedded hosts may display a friendly path title (for example an iOS
    // device name) while navigation still needs the filesystem's canonical
    // path. When set, this property supplies that canonical path without
    // changing the breadcrumb text shown to the user.
    property string navigationPath: ""
    // A host may name the root without changing its canonical address or
    // the segment indices used by child breadcrumb navigation.
    property string rootBreadcrumbLabel: ""
    // Keep the standalone control's original appearance by default while
    // allowing embedded hosts to share their own chrome and content grid.
    property bool backgroundOnHoverOnly: false
    property real leadingInset: 15
    // Embedded hosts may place the drive selector beside the path control
    // while keeping the standalone control's original drive icon by default.
    property bool showDriveIcon: true
    // Hosts may align breadcrumb labels with their ordinary UI typography
    // without changing the editable path field or standalone defaults.
    property real breadcrumbFontPixelSize: 14
    property alias breadcrumbFont: rootFolder.font
    property color pathBackgroundColor: Style.pathBackground
    // Actual composited surface, for embedded hosts with translucent chrome.
    property color pathSurfaceColor: pathBackgroundColor
    property color pathTextColor: Style.text
    property color pathHoveredColor: Style.pathBackgroundHovered
    property color pathItemHoveredColor: Style.pathItemHovered
    property color pathItemPressedColor: Style.pathItemPressed
    property url breadcrumbSeparatorIconSource:
        "qrc:/ZoinGallery/resources/PathSeparator.svg"
    property url localDriveIconSource: "qrc:/ZoinGallery/resources/DriveIcon.svg"
    property url networkDriveIconSource: "qrc:/ZoinGallery/resources/NetworkDriveIcon.svg"
    readonly property url currentDriveIconSource:
        isNetworkDrive ? networkDriveIconSource : localDriveIconSource
    property real devicePixelRatio:
        pathRoot.Window.window && pathRoot.Window.window.screen
        ? pathRoot.Window.window.screen.devicePixelRatio : 1.0
    // Retained for host compatibility; fading is now per-label and does not
    // require capturing the subtree (including image-provider icons).
    property bool breadcrumbMaskEnabled:
        !String(isNetworkDrive ? networkDriveIconSource
                               : localDriveIconSource).startsWith("image://")
    // Standalone ZoinGallery supplies canonical '/' paths, while embedded
    // Windows hosts can naturally expose native '\\' paths. Accept both forms
    // before building breadcrumbs and keep '/' as the navigation contract.
    property bool windowsPathSeparators: Qt.platform.os === "windows"
    readonly property string normalizedText:
        windowsPathSeparators ? text.replace(/\\/g, "/") : text
    property bool isNetworkDrive: normalizedText.startsWith("//")
    property string textNetworkFixed:
        isNetworkDrive ? normalizedText.slice(2) : normalizedText
    // Keep URI schemes as one root button: splitting :// creates an empty
    // breadcrumb and loses the root's navigation address.
    function uriPrefix(path) {
        const match = /^[A-Za-z][A-Za-z0-9+.-]*:\/\//.exec(path)
        return match ? match[0] : ""
    }
    property var breadcrumbs: {
        const prefix = uriPrefix(normalizedText)
        if (prefix !== "") {
            const body = normalizedText.slice(prefix.length).replace(/\/+$/g, "")
            return [prefix].concat(body === "" ? [] : body.split("/").filter(part => part !== ""))
        }
        return (textNetworkFixed.endsWith("/") ? textNetworkFixed.slice(0, -1) : textNetworkFixed).split("/")
    }

    function updatePathField() {
        const editablePath = navigationPath !== "" ? navigationPath : pathRoot.text
        if (windowsPathSeparators) {
            pathField.text = editablePath.replace(/\\/g, "\\")
        }
        else {
            pathField.text = editablePath
        }
    }

    onTextChanged: {
        Qt.callLater(resetPathScroll)
        if (editMode) {
            updatePathField()
        }
    }

    property bool editMode: false
    onEditModeChanged: {
        if (editMode) {
            updatePathField()
            pathField.selectAll()
            pathField.forceActiveFocus()
        }
        else {
            pathField.focus = false
        }
    }

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
        }
        height: 32
        color: pathMouse.containsMouse
               ? pathRoot.pathHoveredColor
               : (backgroundOnHoverOnly ? "transparent"
                                        : pathRoot.pathBackgroundColor)
        radius: 4
    }

    function navigateTo(path) {
        if (navigationHandler) {
            navigationHandler(path)
            return
        }
        viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
        viewerController.cd(path)
        masonryLayout.view.loadSavedState()
    }

    function canonicalFolderPath(index, fallbackPath) {
        const source = navigationPath !== "" ? navigationPath : text
        const canonical = windowsPathSeparators
                ? source.replace(/\\/g, "/") : source
        const prefix = uriPrefix(canonical)
        if (prefix !== "") {
            const parts = canonical.slice(prefix.length).split("/").filter(part => part !== "")
            return prefix + parts.slice(0, Math.max(0, index + 1)).join("/")
        }
        if (navigationPath === "")
            return fallbackPath

        const networkPrefix = canonical.startsWith("//") ? "//" : ""
        const rootPrefix = networkPrefix !== ""
                ? networkPrefix
                : (canonical.startsWith("/") ? "/" : "")
        const body = canonical.replace(/^\/+|\/+$/g, "")
        const parts = body === "" ? [] : body.split("/")

        if (index < 0) {
            if (rootPrefix !== "")
                return rootPrefix
            if (parts.length === 0)
                return canonical
            return parts[0].endsWith(":") ? parts[0] + "/" : parts[0]
        }
        if (parts.length === 0 || index >= parts.length)
            return canonical
        return rootPrefix + parts.slice(0, index + 1).join("/")
    }

    function folderClicked(path) {
        const basePath = (isNetworkDrive ? "//" : "") + breadcrumbs[0]
        navigateTo(canonicalFolderPath(-1, basePath + "/" + path))
    }

    MouseArea {
        id: pathMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: editMode = !editMode
        onReleased: (event) => {
            if (editMode && event.button & Qt.RightButton) {
                pathField.showContextMenu()
            }
        }
    }

    readonly property real dpr:
        Math.max(0.5, Number(pathRoot.devicePixelRatio || 1.0))
    function snap(val) {
        return Math.round(Number(val || 0) * dpr) / dpr
    }
    property real alignmentRevision:
        pathRoot.Window.window
        ? pathRoot.Window.window.width + pathRoot.Window.window.height + dpr
        : width + height + dpr
    function visualPixelOffsetX(item, geometryRevision) {
        if (!item || !item.parent)
            return 0
        const revision = alignmentRevision + pathRoot.x + pathRoot.y
                       + Number(geometryRevision || 0)
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return snap(scenePoint.x) - scenePoint.x + revision * 0
    }
    function visualPixelOffsetY(item, geometryRevision) {
        if (!item || !item.parent)
            return 0
        const revision = alignmentRevision + pathRoot.x + pathRoot.y
                       + Number(geometryRevision || 0)
        const scenePoint = item.parent.mapToItem(null, item.x, item.y)
        return snap(scenePoint.y) - scenePoint.y + revision * 0
    }
    readonly property real breadcrumbSeparatorSize: snap(12)
    readonly property real breadcrumbSeparatorHorizontalPadding: snap(6)
    property bool compactBreadcrumbs: true
    FontMetrics { id: breadcrumbMetrics; font: rootFolder.font }
    // Water-fill the labels: short names retain their natural width, while
    // long names share the remaining space. Keep every ancestor addressable.
    readonly property var breadcrumbWidths: {
        const labels = breadcrumbs.slice(1)
        const overhead = labels.map((_, i) => breadcrumbSeparatorHorizontalPadding
            + (i < labels.length - 1
               ? breadcrumbSeparatorHorizontalPadding + breadcrumbSeparatorSize : 0))
        const natural = labels.map(label => Math.ceil(breadcrumbMetrics.advanceWidth(label)))
        if (!compactBreadcrumbs)
            return natural.map((width, i) => Math.ceil((width + overhead[i]) * dpr) / dpr)
        const minimum = labels.map((label, i) => i === labels.length - 1
            ? natural[i] : Math.min(natural[i],
                Math.ceil(breadcrumbMetrics.advanceWidth(Array.from(label).slice(0, 3).join(""))) + 14))
        const budget = Math.max(0, dynamicPart.width
            - overhead.reduce((a, b) => a + b, 0))
        let low = 0
        let high = Math.max(0, ...natural)
        for (let i = 0; i < 24; ++i) {
            const cap = (low + high) / 2
            if (natural.reduce((sum, value, i) => sum + Math.max(minimum[i], Math.min(value, cap)), 0) > budget)
                high = cap
            else
                low = cap
        }
        return natural.map((value, i) =>
            Math.ceil((Math.max(minimum[i], Math.min(value, low)) + overhead[i]) * dpr) / dpr)
    }
    readonly property real collapsedPathWidth: breadcrumbWidths.reduce((sum, value) => sum + value, 0)
    onCollapsedPathWidthChanged: Qt.callLater(resetPathScroll)
    function resetPathScroll() {
        dynamicPart.contentX = Math.max(0, dynamicPart.contentWidth - dynamicPart.width)
    }
    readonly property real driveIconLogicalSize: 18
    readonly property real driveIconSize: snap(driveIconLogicalSize)
    readonly property real breadcrumbGeometryRevision:
        alignmentRevision
        + fixedPart.x + fixedPart.y + fixedPart.width + fixedPart.height
        + dynamicPart.x + dynamicPart.y + dynamicPart.width + dynamicPart.height
        + collapsiblePart.x + collapsiblePart.y
        + collapsiblePart.width + collapsiblePart.height

    component FolderDelegate : Item {
        id: folderDelegate
        property alias text: folderText.text
        property alias font: folderText.font
        property bool needArrow: true
        property int splitIndex: -1
        property real allocatedWidth: implicitWidth
        readonly property real naturalWidth: pathRoot.snap(folderText.implicitWidth
            + horizontalLeadingInset + horizontalTrailingInset
            + (needArrow ? pathRoot.breadcrumbSeparatorHorizontalPadding
                         + pathRoot.breadcrumbSeparatorSize : 0))
        readonly property bool expanded: folderMouse.containsMouse
        property real expansionProgress: expanded ? 1 : 0
        readonly property real presentedWidth: allocatedWidth
            + (naturalWidth - allocatedWidth) * expansionProgress
        Behavior on expansionProgress {
            NumberAnimation { duration: 240; easing.type: Easing.InOutCubic }
        }
        readonly property real horizontalLeadingInset:
            Math.floor(pathRoot.breadcrumbSeparatorHorizontalPadding
                       * pathRoot.dpr / 2) / pathRoot.dpr
        readonly property real horizontalTrailingInset:
            pathRoot.breadcrumbSeparatorHorizontalPadding
            - horizontalLeadingInset
        readonly property real geometryRevision:
            pathRoot.breadcrumbGeometryRevision
            + x + y + width + height + folder.x + folder.y
            + folder.width + folder.height + pathRoot.breadcrumbs.length

        signal clicked(index: int)

        implicitWidth: naturalWidth
        implicitHeight: parent.height
        // RowLayout is allowed to distribute rounding residue among children.
        // A breadcrumb's width must instead remain its exact snapped width so
        // adding the next segment cannot move already-rendered labels.
        width: presentedWidth
        Layout.minimumWidth: presentedWidth
        Layout.preferredWidth: presentedWidth
        Layout.maximumWidth: presentedWidth

        Item {
            id: visual
            objectName: folderDelegate.objectName + "-visual"
            // The delegate itself grows, so the row moves every successor.
            width: folderDelegate.width
            height: parent.height

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: pathRoot.snap(24)
            color: folderDelegate.expanded
                   ? Qt.tint(pathRoot.pathSurfaceColor, (folderMouse.pressed
                      ? pathRoot.pathItemPressedColor
                      : pathRoot.pathItemHoveredColor))
                   : "transparent"
            radius: 4
        }

        Item {
            id: folder
            x: folderDelegate.horizontalLeadingInset
            width: Math.max(0, visual.width - folderDelegate.horizontalLeadingInset
                           - folderDelegate.horizontalTrailingInset)
            height: folderDelegate.height

            Item {
                id: labelClip
                width: Math.max(0, folder.width - (needArrow
                    ? pathRoot.breadcrumbSeparatorSize + pathRoot.breadcrumbSeparatorHorizontalPadding : 0))
                height: parent.height
                clip: true

            Text {
                id: folderText
                y: pathRoot.snap((folderDelegate.height - height) / 2)
                objectName: folderDelegate.objectName + "-text"
                color: pathRoot.pathTextColor
                font.pixelSize: pathRoot.breadcrumbFontPixelSize
                transform: Translate {
                    x: pathRoot.visualPixelOffsetX(
                           folderText, folderDelegate.geometryRevision)
                    y: pathRoot.visualPixelOffsetY(
                           folderText, folderDelegate.geometryRevision)
                }
            }

            Rectangle {
                id: labelFade
                objectName: folderDelegate.objectName + "-fade"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(pathRoot.snap(14), parent.width)
                height: pathRoot.snap(24)
                visible: folderText.implicitWidth > labelClip.width + 0.5
                readonly property color fadeColor: folderDelegate.expanded
                    ? Qt.tint(pathRoot.pathSurfaceColor,
                        folderMouse.pressed ? pathRoot.pathItemPressedColor : pathRoot.pathItemHoveredColor)
                    : pathRoot.pathSurfaceColor
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.rgba(labelFade.fadeColor.r, labelFade.fadeColor.g, labelFade.fadeColor.b, 0) }
                    GradientStop { position: 1; color: labelFade.fadeColor }
                }
            }
            }

            Image {
                id: separatorIcon
                objectName: folderDelegate.objectName + "-separator"
                // Snap both physical edges so the provider's physical raster
                // is neither clipped nor resampled at fractional DPR.
                x: labelClip.width + pathRoot.breadcrumbSeparatorHorizontalPadding
                y: pathRoot.snap((folderDelegate.height - height) / 2 + 1)
                width: pathRoot.breadcrumbSeparatorSize
                height: pathRoot.breadcrumbSeparatorSize
                smooth: false
                readonly property bool rootSlash: folderDelegate.splitIndex === -1
                        && pathRoot.normalizedText.startsWith("/")
                        && !pathRoot.isNetworkDrive
                visible: needArrow && !rootSlash
                source: pathRoot.breadcrumbSeparatorIconSource
                transform: Translate {
                    x: pathRoot.visualPixelOffsetX(
                           separatorIcon, folderDelegate.geometryRevision)
                    y: pathRoot.visualPixelOffsetY(
                           separatorIcon, folderDelegate.geometryRevision)
                }
            }
            Text {
                objectName: folderDelegate.objectName + "-slash"
                x: (folderDelegate.width - width) / 2 - folder.x
                y: pathRoot.snap((folderDelegate.height - height) / 2)
                width: separatorIcon.width
                height: folderText.height
                visible: needArrow && separatorIcon.rootSlash
                text: "/"
                font: folderText.font
                color: pathRoot.pathTextColor
                opacity: 0.5
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        MouseArea {
            id: folderMouse
            anchors.fill: parent
            hoverEnabled: true

            onClicked: folderDelegate.clicked(folderDelegate.splitIndex)
        }
        }
    }

    RowLayout {
        id: fixedPart
        objectName: "pathFixedPart"
        width: implicitWidth
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        spacing: 0

        Item {
            objectName: "pathDriveIconSlot"
            visible: pathRoot.showDriveIcon
            Layout.leftMargin: pathRoot.showDriveIcon
                               ? pathRoot.leadingInset : 0
            Layout.minimumWidth: pathRoot.showDriveIcon ? 24 : 0
            Layout.preferredWidth: pathRoot.showDriveIcon ? 24 : 0
            Layout.maximumWidth: pathRoot.showDriveIcon ? 24 : 0
            Layout.preferredHeight: parent.height

            Image {
                id: driveIcon
                objectName: "pathDriveIcon"
                visible: pathRoot.showDriveIcon
                width: pathRoot.driveIconSize
                height: pathRoot.driveIconSize
                smooth: false
                anchors.centerIn: parent

                source: pathRoot.currentDriveIconSource
                transform: Translate {
                    x: pathRoot.visualPixelOffsetX(driveIcon)
                    y: pathRoot.visualPixelOffsetY(driveIcon)
                }
            }
        }

        FolderDelegate {
            id: rootFolder
            objectName: "pathBreadcrumbRoot"
            visible: !editMode
            text: pathRoot.rootBreadcrumbLabel || breadcrumbs[0]
            onClicked: (index) => pathRoot.folderClicked("")
        }
    }

    Flickable {
        id: dynamicPart
        objectName: "pathDynamicPart"
        anchors.left: fixedPart.right
        width: Math.max(0, pathRoot.width - fixedPart.width)
        height: parent.height
        clip: true
        visible: !editMode
        contentWidth: collapsiblePart.width
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        onWidthChanged: Qt.callLater(pathRoot.resetPathScroll)

        function scrollWheel(event) {
                const delta = event.pixelDelta.x !== 0 ? event.pixelDelta.x
                    : event.pixelDelta.y !== 0 ? event.pixelDelta.y
                    : (event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y) / 3
                dynamicPart.contentX = Math.max(0, Math.min(
                    Math.max(0, dynamicPart.contentWidth - dynamicPart.width),
                    dynamicPart.contentX - delta))
                event.accepted = true
        }

        Row {
            id: collapsiblePart
            objectName: "pathCollapsiblePart"
            width: implicitWidth
            anchors {
                top: parent.top
                bottom: parent.bottom
                left: parent.left
            }
            spacing: 0

            Repeater {
                id: repeater
                model: breadcrumbs.slice(1)

                FolderDelegate {
                    required property int index
                    required property var modelData
                    objectName: "pathBreadcrumb-" + index
                    text: modelData
                    needArrow: index !== repeater.model.length - 1
                    splitIndex: index
                    allocatedWidth: needArrow ? (pathRoot.breadcrumbWidths[index] || 0) : naturalWidth

                    onClicked: (index) => pathRoot.navigateTo(
                        pathRoot.canonicalFolderPath(
                            index,
                            (pathRoot.isNetworkDrive ? "//" : "")
                            + pathRoot.breadcrumbs[0] + "/"
                            + repeater.model.slice(0, index + 1).join("/")))
                }
            }
        }
    }

    Rectangle {
        objectName: "pathLeadingFade"
        anchors.left: dynamicPart.left
        anchors.top: dynamicPart.top
        anchors.bottom: dynamicPart.bottom
        width: Math.min(16, dynamicPart.width)
        visible: !pathRoot.editMode && dynamicPart.contentX > 0.5
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: pathRoot.pathSurfaceColor }
            GradientStop { position: 1; color: Qt.rgba(pathRoot.pathSurfaceColor.r, pathRoot.pathSurfaceColor.g, pathRoot.pathSurfaceColor.b, 0) }
        }
    }
    // Receive wheel/trackpad gestures in viewport coordinates, above the
    // breadcrumb MouseAreas. No buttons are accepted, so clicks and hover
    // continue to reach the breadcrumb underneath.
    MouseArea {
        objectName: "pathWheelArea"
        anchors.fill: pathRoot
        visible: !pathRoot.editMode
        z: 10
        acceptedButtons: Qt.NoButton
        scrollGestureEnabled: true
        onWheel: wheel => dynamicPart.scrollWheel(wheel)
    }
    Rectangle {
        objectName: "pathTrailingFade"
        anchors.right: dynamicPart.right
        anchors.top: dynamicPart.top
        anchors.bottom: dynamicPart.bottom
        width: Math.min(16, dynamicPart.width)
        visible: !pathRoot.editMode && dynamicPart.contentX + dynamicPart.width < dynamicPart.contentWidth - 0.5
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.rgba(pathRoot.pathSurfaceColor.r, pathRoot.pathSurfaceColor.g, pathRoot.pathSurfaceColor.b, 0) }
            GradientStop { position: 1; color: pathRoot.pathSurfaceColor }
        }
    }

    ZGS.TextField {
        id: pathField
        objectName: "pathField"
        anchors {
            left: fixedPart.right
            top: parent.top
            bottom: parent.bottom
            right: parent.right
        }
        visible: editMode
        font: pathRoot.breadcrumbFont
        transform: Translate {
            x: pathRoot.visualPixelOffsetX(pathField, pathRoot.breadcrumbGeometryRevision)
            y: pathRoot.visualPixelOffsetY(pathField, pathRoot.breadcrumbGeometryRevision)
        }

        leftPadding: 7
        rightPadding: 10
        hasBackground: false
        color: pathRoot.pathTextColor

        onFocusChanged: {
            if (!focus && pathField.focusReason !== Qt.PopupFocusReason) {
                editMode = false
            }
        }

        Keys.onEscapePressed: editMode = false

        function accept() {
            if (windowsPathSeparators) {
                pathRoot.navigateTo(pathField.text.replace(/\\/g, "/"))
            }
            else {
                pathRoot.navigateTo(pathField.text)
            }
            editMode = false
        }

        Keys.onEnterPressed: accept()
        Keys.onReturnPressed: accept()
    }
}
