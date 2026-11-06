pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.PixelGrid 1.0
import QtQuick.Controls.Basic as T

// Module-local copy of ZGStyle/ScrollBar.  Using the Basic template directly
// keeps the control identical when an embedding application selects another
// global Qt Quick Controls style.
T.ScrollBar {
    id: scroll

    required property GalleryThemePalette theme
    property bool pixelAlignmentSuspended: false
    readonly property bool motionActive: pixelAlignmentSuspended || pressed
    readonly property color handleColor: theme.scrollBarHandle
    readonly property color handleBackgroundHoveredColor:
        theme.scrollBarHandleBackgroundHovered
    readonly property color handleHoveredColor: theme.scrollBarHandleHovered
    readonly property color handlePressedColor: theme.scrollBarHandlePressed
    readonly property color trackHoveredColor: theme.scrollBarTrackHovered

    policy: T.ScrollBar.AlwaysOn
    // Keep a usable thumb when the proportional size is only a few pixels.
    // minimumSize affects visualSize/visualPosition, not the logical range.
    minimumSize: Math.min(1, 24 / Math.max(1, orientation === Qt.Vertical
                                          ? availableHeight : availableWidth))
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
            id: handleSurface
            objectName: "galleryScrollBarHandle"
            x: 4
            y: 4
            readonly property var pixelGrid: PixelGridAlignment.grid
            readonly property real rawWidth: Math.max(0, parent.width - 8)
            readonly property real rawHeight: Math.max(0, parent.height - 8)
            width: !scroll.motionActive && pixelGrid ? pixelGrid.snap(rawWidth) : rawWidth
            height: !scroll.motionActive && pixelGrid ? pixelGrid.snap(rawHeight) : rawHeight
            PixelGridAlignment.enabled: scroll.visible && !scroll.motionActive
            transform: Translate {
                x: handleSurface.PixelGridAlignment.offset.x
                y: handleSurface.PixelGridAlignment.offset.y
            }
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
        objectName: "galleryScrollBarTrack"
        color: (scroll.hovered || scroll.pressed)
               ? scroll.trackHoveredColor : "transparent"
        radius: 15
    }
}
