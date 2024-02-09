import QtQuick
import QtQuick.Controls.Basic as T

T.Slider {
    id: control

    focusPolicy: Qt.NoFocus
    property bool handleVisible: true

    background: Item {
        x: control.leftPadding
        y: control.orientation === Qt.Horizontal ? (control.topPadding + control.availableHeight / 2 - height / 2) : control.topPadding
        implicitWidth: control.orientation === Qt.Horizontal ? 200 : control.width
        implicitHeight: control.orientation === Qt.Horizontal ? control.height : 200
        width: control.orientation === Qt.Horizontal ? control.availableWidth : control.width
        height: control.orientation === Qt.Horizontal ? (!handleVisible ? control.height : 6) : control.availableHeight

        Rectangle {
            anchors {
                left: lightArea.right
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }

            color: Style.isDarkTheme ? (!handleVisible ? Style.lighter2 : Style.darker) : Style.lighter2
            radius: handleVisible ? 2 : 0
        }

        Rectangle {
            id: lightArea
            width: control.orientation === Qt.Horizontal ? control.visualPosition * parent.width : parent.width
            height: control.orientation === Qt.Horizontal ? parent.height : (control.visualPosition * parent.height)
            color: Style.isDarkTheme ? (!handleVisible ? Style.handle : Style.lighter2) : Style.darker
            radius: handleVisible ? 2 : 0
        }
    }

    handle: Rectangle {
        visible: handleVisible
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: handleVisible ? 18 : 0
        implicitHeight: 18
        radius: 18
        color: control.hovered ? (control.pressed ? Style.handlePressed : Style.handleHovered) : (control.pressed ? Style.handlePressed : Style.handle)


        ToolTip {
            id: tooltip

            visible: control.hovered || control.pressed
            text: control.value
        }
    }
}
