import QtQuick
import QtQuick.Controls.Basic as T

// Module-local copy of ZGStyle/ScrollBar.  Using the Basic template directly
// keeps the control identical when an embedding application selects another
// global Qt Quick Controls style.
T.ScrollBar {
    id: scroll

    property var theme: ({})
    readonly property color handleColor:
        theme && theme.scrollBarHandle !== undefined
        ? theme.scrollBarHandle : "#4a4a4a"
    readonly property color handleBackgroundHoveredColor:
        theme && theme.scrollBarHandleBackgroundHovered !== undefined
        ? theme.scrollBarHandleBackgroundHovered : "#676767"
    readonly property color handleHoveredColor:
        theme && theme.scrollBarHandleHovered !== undefined
        ? theme.scrollBarHandleHovered : "#878787"
    readonly property color handlePressedColor:
        theme && theme.scrollBarHandlePressed !== undefined
        ? theme.scrollBarHandlePressed : "#505050"
    readonly property color trackHoveredColor:
        theme && theme.scrollBarTrackHovered !== undefined
        ? theme.scrollBarTrackHovered : Qt.rgba(1, 1, 1, 0.06)

    policy: T.ScrollBar.AlwaysOn
    implicitWidth: 15
    implicitHeight: 15
    hoverEnabled: true
    leftPadding: 0
    topPadding: 0
    rightPadding: 0
    bottomPadding: 0

    contentItem: MouseArea {
        id: contentItem
        implicitWidth: 15
        implicitHeight: 15
        hoverEnabled: true
        acceptedButtons: Qt.NoButton

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: 4
            color: scroll.hovered
                   ? (scroll.pressed ? scroll.handlePressedColor
                      : (contentItem.containsMouse
                         ? scroll.handleHoveredColor
                         : scroll.handleBackgroundHoveredColor))
                   : scroll.handleColor
        }
    }

    background: Rectangle {
        color: (scroll.hovered || scroll.pressed)
               ? scroll.trackHoveredColor : "transparent"
        radius: 15
    }
}
