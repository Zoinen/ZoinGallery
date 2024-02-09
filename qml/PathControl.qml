import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: pathRoot
    property string text
    property string pathSeparator: Qt.platform.os === "windows" ? "\\" : "/"

    property bool editMode: false
    onEditModeChanged: {
        if (editMode) {
            pathField.text = pathRoot.text
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
        color: pathMouse.containsMouse ? Style.lighter2 : Style.lighter
        radius: 4
    }

    function folderClicked(path) {
        viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
        viewerController.cd(rootFolder.text + pathSeparator + path)
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

        implicitWidth: folder.width + 12
        implicitHeight: parent.height

        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: 22
            color: folderMouse.containsMouse ? (folderMouse.pressed ? Style.darker : Style.lighter) : "transparent"
            radius: 4
        }

        Row {
            id: folder
            anchors.centerIn: parent
            spacing: 12

            Text {
                id: folderText
                color: Style.text
            }

            Image {
                anchors {
                    verticalCenter: parent.verticalCenter
                    verticalCenterOffset: 1
                }
                visible: needArrow

                source: "qrc:/resources/PathSeparator.svg"
                sourceSize.height: 6
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

                source: "qrc:/resources/DriveIcon.svg"
                sourceSize.width: 18
                sourceSize.height: 18
            }
        }

        FolderDelegate {
            id: rootFolder
            visible: !editMode
            text: (pathRoot.text.endsWith(pathSeparator) ? pathRoot.text.slice(0, -1) : pathRoot.text).split(pathSeparator)[0]
            onClicked: (index) => pathRoot.folderClicked("")
        }
    }

    Item {
        anchors.left: fixedPart.right
        width: Math.min(pathRoot.width - fixedPart.width, collapsiblePart.width)
        height: parent.height
        clip: true
        visible: !editMode

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
                model: (text.endsWith(pathSeparator) ? text.slice(0, -1) : text).split(pathSeparator).slice(1)

                FolderDelegate {
                    text: modelData
                    needArrow: index !== repeater.model.length - 1
                    splitIndex: index

                    onClicked: (index) => pathRoot.folderClicked(repeater.model.slice(0, index + 1).join(pathSeparator))
                }
            }
        }
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

        leftPadding: 4
        rightPadding: 10
        hasBackground: false
        color: Style.text

        text: pathRoot.text

        onFocusChanged: {
            if (!focus && pathField.focusReason !== Qt.PopupFocusReason) {
                editMode = false
            }
        }

        Keys.onEscapePressed: editMode = false

        function accept() {
            viewerController.saveCurrentState(masonryLayout.view.contentY, masonryLayout.view.currentIndex)
            viewerController.cd(pathField.text)
            masonryLayout.view.loadSavedState()
            editMode = false
        }

        Keys.onEnterPressed: accept()
        Keys.onReturnPressed: accept()
    }

    Rectangle {
        visible: !editMode && (pathRoot.width - fixedPart.width < collapsiblePart.width)
        anchors {
            left: fixedPart.right
            top: parent.top
            bottom: parent.bottom
        }
        width: 20
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Style.pathFadeGradient }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
