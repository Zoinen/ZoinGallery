pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as T

// Module-local copy of ZGStyle/ScrollBar.  Using the Basic template directly
// keeps the control identical when an embedding application selects another
// global Qt Quick Controls style.
T.ScrollBar {
    id: scroll

    required property GalleryThemePalette theme
    readonly property color handleColor: theme.scrollBarHandle
    readonly property color handleBackgroundHoveredColor:
        theme.scrollBarHandleBackgroundHovered
    readonly property color handleHoveredColor: theme.scrollBarHandleHovered
    readonly property color handlePressedColor: theme.scrollBarHandlePressed
    readonly property color trackHoveredColor: theme.scrollBarTrackHovered

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
