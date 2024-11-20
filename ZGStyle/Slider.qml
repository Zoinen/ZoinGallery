import QtQuick
import QtQuick.Controls.Basic as T

T.Slider {
    id: control

    focusPolicy: Qt.NoFocus
    property bool handleVisible: true
    property real visualHeight

    property real handleDiameter: handleVisible ? 22 : 0

    background: Item {
        x: control.leftPadding
        y: control.orientation === Qt.Horizontal ? (control.topPadding + control.availableHeight / 2 - height / 2) : control.topPadding
        implicitWidth: control.orientation === Qt.Horizontal ? 200 : control.width
        implicitHeight: control.orientation === Qt.Horizontal ? control.height : 200
        width: control.orientation === Qt.Horizontal ? control.availableWidth : control.width
        height: control.orientation === Qt.Horizontal ? (!handleVisible ? (visualHeight ? visualHeight : control.height) : 4) : control.availableHeight

        Rectangle {
            anchors {
                left: filledArea.right
                leftMargin: handleDiameter - (handleVisible ? 2 : 0)
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }

            color: !handleVisible ? Style.sliderNoHandleBackgroundColor : Style.sliderBackgroundColor
            topRightRadius: 4
            bottomRightRadius: 4

            topLeftRadius: !handleVisible && !control.visualPosition ? 4 : 0
            bottomLeftRadius: !handleVisible && !control.visualPosition ? 4 : 0
        }

        Rectangle {
            id: filledArea
            width: control.orientation === Qt.Horizontal ? control.visualPosition * (control.availableWidth - handleDiameter) + (handleVisible ? 1 : 0) : parent.width
            height: control.orientation === Qt.Horizontal ? parent.height : (control.visualPosition * parent.height)
            color: !handleVisible ? Style.sliderNoHandleFilledColor : handleDot.color
            topLeftRadius: 4
            bottomLeftRadius: 4

            topRightRadius: !handleVisible && control.visualPosition == 1 ? 4 : 0
            bottomRightRadius: !handleVisible && control.visualPosition == 1 ? 4 : 0
        }
    }

    handle: Rectangle {
        id: handleRect
        visible: handleVisible
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: handleVisible ? handleDiameter : 0
        implicitHeight: handleDiameter
        radius: handleDiameter
        border.width: 1
        border.color: Style.sliderHandleBorder
        color: Style.sliderHandleBackground

        Rectangle {
            id: handleDot
            // anchors.centerIn: parent
            x: parent.width / 2 - diameter / 2
            y: parent.height / 2 - diameter / 2
            property real diameter: control.hovered ? (control.pressed ? 9 : 14) : 11
            Behavior on diameter {
                NumberAnimation {
                    duration: 150
                    easing.type: Easing.InOutQuad
                }
            }

            width: diameter
            height: diameter
            radius: diameter

            color: control.hovered ? (control.pressed ? Style.sliderHandlePressed : Style.sliderHandleHovered) : (control.pressed ? Style.sliderHandlePressed : Style.sliderHandle)
        }

        ToolTip {
            id: tooltip
            x: handleRect.width / 2 - width / 2
            y: control.height

            visible: control.hovered || control.pressed
            text: control.value
        }
    }
}
