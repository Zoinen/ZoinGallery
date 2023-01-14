import QtQuick
import QtQuick.Controls.Basic as T

import "."

T.TextField {
    id: control

    property bool hasBackground: true

    color: acceptableInput ? (hovered ? Style.hovered : Style.text) : Style.textError

    background: Rectangle {
        id: bg
        visible: hasBackground
        anchors.fill: parent

        color: Style.controlBackground
        border.color: parent.activeFocus ? Style.focus : bg.color
        border.width: 1
    }

    hoverEnabled: true
    selectByMouse: true
    palette.highlight: Style.focus
    palette.highlightedText: Style.hovered
    palette.brightText: "green"

    leftPadding: 10
    rightPadding: 10

    function showContextMenu() {
        textEditMenu.popup()
    }

    onReleased: (event) => {
        if (event.button === Qt.RightButton) {
            showContextMenu()
        }
    }

    Menu {
        id: textEditMenu

        MenuItem {
            text: qsTr("Cut")
            enabled: control.selectedText.length > 0
            onTriggered: {
                control.cut()
                control.forceActiveFocus()
            }
        }

        MenuItem {
            text: qsTr("Copy")
            enabled: control.selectedText.length > 0
            onTriggered: {
                control.copy()
                control.forceActiveFocus()
            }
        }

        MenuItem {
            text: qsTr("Paste")
            enabled: control.canPaste
            onTriggered: {
                control.paste()
                control.forceActiveFocus()
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Undo")
            enabled: control.canUndo
            onTriggered: {
                control.undo()
                control.forceActiveFocus()
            }
        }

        MenuItem {
            text: qsTr("Redo")
            enabled: control.canRedo
            onTriggered: {
                control.redo()
                control.forceActiveFocus()
            }
        }
    }
}
