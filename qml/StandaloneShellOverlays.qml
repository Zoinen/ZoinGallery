import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import ZoinGallery 1.0

pragma ComponentBehavior: Bound

Item {
    id: overlays

    required property Item stateController

    readonly property alias createDialog: createFolderPopup
    readonly property alias nameField: createFolderName
    readonly property alias errorLabel: createFolderError
    readonly property alias dropDialog: fileDropErrorPopup

    Popup {
        id: createFolderPopup
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 40)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 18

        background: Rectangle {
            radius: 8
            color: Style.popupBackground
            border.width: 1
            border.color: Style.popupBorder
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: overlays.stateController.createFolderForDrop
                      ? "Create folder and move items"
                      : "Create folder"
                color: Style.text
                font.pixelSize: 17
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: overlays.stateController.createFolderForDrop
                      ? "The dropped items will be placed in the new folder."
                      : "The folder will be created in the current path."
                color: Style.viewerSecondaryText
                wrapMode: Text.Wrap
            }

            TextField {
                id: createFolderName
                Layout.fillWidth: true
                placeholderText: "Folder name"
                selectByMouse: true
                onAccepted: overlays.stateController.confirmFolderCreation()
            }

            Text {
                id: createFolderError
                Layout.fillWidth: true
                visible: text.length > 0
                color: "#e76565"
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: "Cancel"
                    onClicked: createFolderPopup.close()
                }

                Button {
                    text: "Create"
                    inactive: createFolderName.text.trim().length === 0
                    onClicked: {
                        if (!inactive) {
                            overlays.stateController.confirmFolderCreation()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: fileDropErrorPopup
        property string titleText: ""
        property string messageText: ""

        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 18

        background: Rectangle {
            radius: 8
            color: Style.popupBackground
            border.width: 1
            border.color: Style.popupBorder
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: fileDropErrorPopup.titleText
                color: Style.text
                font.pixelSize: 17
                font.bold: true
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                text: fileDropErrorPopup.messageText
                color: Style.text
                wrapMode: Text.Wrap
            }

            Button {
                Layout.alignment: Qt.AlignRight
                text: "OK"
                onClicked: fileDropErrorPopup.close()
            }
        }
    }

}
