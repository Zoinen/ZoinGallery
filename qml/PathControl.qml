import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Effects
import "../ZGStyle" as ZGS

Item {
    id: pathRoot
    property string text
    // Embedded hosts can own navigation while reusing the complete original
    // path control. The standalone application keeps its historical
    // viewerController/masonryLayout transaction when no callback is set.
    property var navigationHandler: null
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
    // ShaderEffectSource currently drops the dynamic breadcrumb subtree when
    // it contains images served by a QQuickImageProvider. Keep the original
    // fade mask for resource icons, and use the clipped source directly for
    // provider-backed icons until those temporary images are removed.
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
    property var breadcrumbs: (textNetworkFixed.endsWith("/") ? textNetworkFixed.slice(0, -1) : textNetworkFixed).split("/")

    function updatePathField() {
        if (windowsPathSeparators) {
            pathField.text = normalizedText.replace(/\//g, "\\")
        }
        else {
            pathField.text = pathRoot.text
        }
    }

    onTextChanged: {
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

    function folderClicked(path) {
        const basePath = (isNetworkDrive ? "//" : "") + rootFolder.text
        navigateTo(basePath + "/" + path)
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

        implicitWidth: pathRoot.snap(folder.implicitWidth
                                     + horizontalLeadingInset
                                     + horizontalTrailingInset)
        implicitHeight: parent.height
        // RowLayout is allowed to distribute rounding residue among children.
        // A breadcrumb's width must instead remain its exact snapped width so
        // adding the next segment cannot move already-rendered labels.
        Layout.minimumWidth: implicitWidth
        Layout.preferredWidth: implicitWidth
        Layout.maximumWidth: implicitWidth

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: pathRoot.snap(24)
            color: folderMouse.containsMouse
                   ? (folderMouse.pressed
                      ? pathRoot.pathItemPressedColor
                      : pathRoot.pathItemHoveredColor)
                   : "transparent"
            radius: 4
        }

        Row {
            id: folder
            x: folderDelegate.horizontalLeadingInset
            y: pathRoot.snap((folderDelegate.height - height) / 2)
            spacing: pathRoot.breadcrumbSeparatorHorizontalPadding

            Text {
                id: folderText
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

            Image {
                id: separatorIcon
                objectName: folderDelegate.objectName + "-separator"
                // Snap both physical edges so the provider's physical raster
                // is neither clipped nor resampled at fractional DPR.
                y: pathRoot.snap((folderDelegate.height - height) / 2 + 1) - folder.y
                width: pathRoot.breadcrumbSeparatorSize
                height: pathRoot.breadcrumbSeparatorSize
                smooth: false
                visible: needArrow
                source: pathRoot.breadcrumbSeparatorIconSource
                transform: Translate {
                    x: pathRoot.visualPixelOffsetX(
                           separatorIcon, folderDelegate.geometryRevision)
                    y: pathRoot.visualPixelOffsetY(
                           separatorIcon, folderDelegate.geometryRevision)
                }
            }
        }

        MouseArea {
            id: folderMouse
            anchors.fill: parent
            hoverEnabled: true

            onClicked: folderDelegate.clicked(folderDelegate.splitIndex)
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
            text: breadcrumbs[0]
            onClicked: (index) => pathRoot.folderClicked("")
        }
    }

    Item {
        id: dynamicPart
        objectName: "pathDynamicPart"
        anchors.left: fixedPart.right
        width: rectMaskSource.overflowIndicatorVisible ? pathRoot.width - fixedPart.width - 10 : collapsiblePart.implicitWidth
        height: parent.height
        clip: true
        visible: !editMode

        RowLayout {
            id: collapsiblePart
            objectName: "pathCollapsiblePart"
            width: implicitWidth
            anchors {
                top: parent.top
                bottom: parent.bottom
                right: parent.right
            }
            spacing: 0

            Repeater {
                id: repeater
                model: breadcrumbs.slice(1)

                FolderDelegate {
                    objectName: "pathBreadcrumb-" + index
                    text: modelData
                    needArrow: index !== repeater.model.length - 1
                    splitIndex: index

                    onClicked: (index) => pathRoot.folderClicked(repeater.model.slice(0, index + 1).join("/"))
                }
            }
        }
    }

    Item {
        id: rectMaskSource
        anchors.fill: dynamicPart

        layer.enabled: true
        visible: false
        property bool overflowIndicatorVisible: !editMode && (pathRoot.width - fixedPart.width - 10 < collapsiblePart.width)

        Rectangle {
            id: overflowIndicator
            anchors {
                top: parent.top
                bottom: parent.bottom
            }
            width: 20
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: rectMaskSource.overflowIndicatorVisible ? "transparent" : Qt.white }
                GradientStop { position: 1.0; color: Qt.white }
            }
        }

        Rectangle {
            anchors {
                left: overflowIndicator.right
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }
        }
    }

    MultiEffect {
        anchors.fill: dynamicPart
        source: ShaderEffectSource {
            sourceItem: dynamicPart
            hideSource: pathRoot.breadcrumbMaskEnabled
        }

        maskEnabled: true
        maskSource: rectMaskSource

        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0

        visible: !editMode && pathRoot.breadcrumbMaskEnabled
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
