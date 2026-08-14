import QtQuick
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
	// Hosts may align breadcrumb labels with their ordinary UI typography
	// without changing the editable path field or standalone defaults.
	property real breadcrumbFontPixelSize: 14
    property bool isNetworkDrive: text.startsWith("//")
    property string textNetworkFixed: isNetworkDrive ? text.slice(2) : text
    property var breadcrumbs: (textNetworkFixed.endsWith("/") ? textNetworkFixed.slice(0, -1) : textNetworkFixed).split("/")

    function replaceAll(str, find, replace) {
        return str.replace(new RegExp(find, 'g'), replace);
    }

    function updatePathField() {
        if (Qt.platform.os === "windows") {
            pathField.text = replaceAll(pathRoot.text, "/", "\\")
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
               ? Style.pathBackgroundHovered
               : (backgroundOnHoverOnly ? "transparent"
                                        : Style.pathBackground)
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
        navigateTo(rootFolder.text + "/" + path)
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

    component FolderDelegate : Item {
        id: folderDelegate
        property alias text: folderText.text
        property bool needArrow: true
        property int splitIndex: -1

        signal clicked(index: int)

        implicitWidth: folder.width + 11
        implicitHeight: parent.height

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: 24
            color: folderMouse.containsMouse ? (folderMouse.pressed ? Style.pathItemPressed : Style.pathItemHovered) : "transparent"
            radius: 4
        }

        Row {
            id: folder
            anchors.centerIn: parent
            spacing: 12

            Text {
                id: folderText
                color: Style.text
				font.pixelSize: pathRoot.breadcrumbFontPixelSize
                renderType: Text.NativeRendering
            }

            IconLabel {
                anchors {
                    verticalCenter: parent.verticalCenter
                    verticalCenterOffset: 1
                }
                visible: needArrow
                opacity: 0.5

                icon.source: "qrc:/ZoinGallery/resources/PathSeparator.svg"
                icon.color: Style.text
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
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        spacing: 0

        Item {
            Layout.leftMargin: pathRoot.leadingInset
            Layout.preferredWidth: 24
            Layout.preferredHeight: parent.height

            Image {
                anchors {
                    verticalCenter: parent.verticalCenter
                    right: parent.right
                    rightMargin: 7
                }

                source: isNetworkDrive ? "qrc:/ZoinGallery/resources/NetworkDriveIcon.svg" : "qrc:/ZoinGallery/resources/DriveIcon.svg"
                sourceSize.width: 18
                sourceSize.height: 18
            }
        }

        FolderDelegate {
            id: rootFolder
            visible: !editMode
            text: breadcrumbs[0]
            onClicked: (index) => pathRoot.folderClicked("")
        }
    }

    Item {
        id: dynamicPart
        anchors.left: fixedPart.right
        width: rectMaskSource.overflowIndicatorVisible ? pathRoot.width - fixedPart.width - 10 : collapsiblePart.width
        height: parent.height
        clip: true

        RowLayout {
            id: collapsiblePart
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
            hideSource: true
        }

        maskEnabled: true
        maskSource: rectMaskSource

        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0

        visible: !editMode
    }

    ZGS.TextField {
        id: pathField
        anchors {
            left: fixedPart.right
            top: parent.top
            bottom: parent.bottom
            right: parent.right
        }
        visible: editMode
        font.pixelSize: 14
        renderType: Text.NativeRendering

        leftPadding: 7
        rightPadding: 10
        hasBackground: false
        color: Style.text

        onFocusChanged: {
            if (!focus && pathField.focusReason !== Qt.PopupFocusReason) {
                editMode = false
            }
        }

        Keys.onEscapePressed: editMode = false

        function accept() {
            if (Qt.platform.os === "windows") {
                pathRoot.navigateTo(replaceAll(pathField.text, "\\\\", "/"))
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
