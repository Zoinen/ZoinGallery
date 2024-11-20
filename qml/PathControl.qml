import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: pathRoot
    property string text
    property bool isNetworkDrive: text.startsWith("//")
    property string textNetworkFixed: isNetworkDrive ? text.slice(2) : text
    property var breadcrumbs: (textNetworkFixed.endsWith("/") ? textNetworkFixed.slice(0, -1) : textNetworkFixed).split("/")

    function replaceAll(str, find, replace) {
        return str.replace(new RegExp(find, 'g'), replace);
    }


    property bool editMode: false
    onEditModeChanged: {
        if (editMode) {
            if (Qt.platform.os === "windows") {
                pathField.text = replaceAll(pathRoot.text, "/", "\\")
            }
            else {
                pathField.text = pathRoot.text
            }

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
        color: pathMouse.containsMouse ? Style.pathBackgroundHovered : Style.pathBackground
        radius: 4
    }

    function folderClicked(path) {
        viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
        viewerController.cd(rootFolder.text + "/" + path)
        masonryLayout.view.loadSavedState()
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
                font.pixelSize: 14
                renderType: Text.NativeRendering
            }

            IconLabel {
                anchors {
                    verticalCenter: parent.verticalCenter
                    verticalCenterOffset: 1
                }
                visible: needArrow
                opacity: 0.5

                icon.source: "qrc:/resources/PathSeparator.svg"
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
            Layout.leftMargin: 15
            Layout.preferredWidth: 24
            Layout.preferredHeight: parent.height

            Image {
                anchors {
                    verticalCenter: parent.verticalCenter
                    right: parent.right
                    rightMargin: 7
                }

                source: isNetworkDrive ? "qrc:/resources/NetworkDriveIcon.svg" : "qrc:/resources/DriveIcon.svg"
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

    TextField {
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
            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
            if (Qt.platform.os === "windows") {
                viewerController.cd(replaceAll(pathField.text, "\\\\", "/"))
            }
            else {
                viewerController.cd(pathField.text)
            }
            masonryLayout.view.loadSavedState()
            editMode = false
        }

        Keys.onEnterPressed: accept()
        Keys.onReturnPressed: accept()
    }
}
