pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    property color panelBackground: "#17191d"
    property color viewerBackground: "#090a0c"
    property color text: "#e8eaed"
    property color mutedText: "#aeb4bc"
    property color cursor: "#4c8bf5"
    property color cursorBackground: cursor
    property color cursorBorder: Qt.lighter(cursor, 1.35)
    property color cardCursorBorder: Qt.lighter(cursor, 1.35)
    property color selection: "#d8a31a"
    property color markedBackground: selection
    property color markedText: "#ffd43b"
    property color directoryText: text
    property bool neutralFileTextColors: true
    property color fileText: "#c4cbd3"
    property color folderText: "#ffffff"
    property color folderIcon: mutedText
    property color itemBackground: Qt.lighter(panelBackground, 1.09)
    property color directoryBackground: Qt.lighter(panelBackground, 1.20)
    property color itemHover: Qt.lighter(panelBackground, 1.25)
    property color labelBackground: "#aa101216"
    property color previewBackdrop:
        Qt.styleHints.colorScheme === Qt.Dark
            ? Qt.rgba(0, 0, 0, 0.3) : Qt.rgba(0, 0, 0, 0.2)
    property color dialogBackground: panelBackground
    property color separator: Qt.rgba(1, 1, 1, 0.12)
    property color headerText: text
    property color controlHover: Qt.lighter(panelBackground, 1.25)
    property color quickSearchMatch: text
    property color scrollBarHandle: "#4a4a4a"
    property color scrollBarHandleBackgroundHovered: "#676767"
    property color scrollBarHandleHovered: "#878787"
    property color scrollBarHandlePressed: "#505050"
    property color scrollBarTrackHovered: Qt.rgba(1, 1, 1, 0.06)
}
