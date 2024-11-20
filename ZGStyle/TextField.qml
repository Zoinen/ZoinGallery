import QtQuick
import QtQuick.Controls.Basic as T

import "."

T.TextField {
    id: control

    property bool hasBackground: true

    color: acceptableInput ? Style.text : Style.textError

    background: Rectangle {
        id: bg
        visible: hasBackground
        anchors.fill: parent

        color: Style.darker
        border.color: parent.activeFocus ? Style.textSelectedBackground : bg.color
        border.width: 1
    }

    hoverEnabled: true
    selectByMouse: true
    palette.highlight: Style.textSelectedBackground
    palette.highlightedText: Style.textSelected
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
