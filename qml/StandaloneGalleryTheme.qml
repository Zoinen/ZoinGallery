pragma ComponentBehavior: Bound

import QtQuick

GalleryThemePalette {
    panelBackground: Style.masonryViewBackground
    text: Style.text
    mutedText: Qt.rgba(Style.text.r, Style.text.g, Style.text.b, 0.68)
    cursor: Style.brickSelected
    cursorBackground: Style.brickSelected
    cursorBorder: Style.brickSelectedBorder
    cardCursorBorder: Style.brickSelectedBorder
    selection: Style.persistentSelectionBorder
    markedBackground: Style.brickSelected
    markedText: Style.textSelected
    directoryText: Style.text
    fileText: mutedText
    folderText: Style.text
    folderIcon: Style.folderIcon
    itemBackground: Style.darker
    directoryBackground: Style.darker
    itemHover: Style.brickHovered
    controlHover: Style.brickHovered
    labelBackground: Style.opaqueMasonryViewBackgroundWithOpacity
    previewBackdrop: Style.darker
    separator: Style.lighter2
    headerText: Style.text
    quickSearchMatch: Style.accentColor
    dialogBackground: Style.popupBackground
    neutralFileTextColors: false
}
