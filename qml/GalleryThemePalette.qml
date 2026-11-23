pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery as ZG

QtObject {
    function entryNameColor(directory, selected, foreground, nameForeground) {
        if (nameForeground)
            return nameForeground
        if (selected)
            return markedText
        if (neutralFileTextColors)
            return directory ? folderText : fileText
        return foreground || text
    }

    function entryIconColor(directory, selected, normalForeground) {
        return selected ? markedText : (normalForeground || (directory ? folderIcon : mutedText))
    }

    function entrySurfaceColor(details, masonry, directory, folderPreview, selected, current, normalBg, cursorBg) {
        if (selected)
            return markedBackground
        if (details)
            return current ? (cursorBg || cursorBackground) : (normalBg || "transparent")
        if (current)
            return cursor
        if (masonry && !folderPreview)
            return directory ? directoryBackground : itemBackground
        return "transparent"
    }

    function entryCursorBorder(details, selected) {
        return selected && showSelectionBorders ? selection : (details ? cursorBorder : cardCursorBorder)
    }

    function entryMasonryLabelColor(selected, current, normalBg) {
        if (selected)
            return Qt.alpha(markedBackground, ZG.Style.brickInfoPanelSelected.a)
        return current ? ZG.Style.brickInfoPanelSelected : (normalBg || labelBackground)
    }

    function entryIconTinted(tint, systemSource, monochrome) {
        return !systemSource && (tint === true || (tint === undefined && monochrome))
    }

    function entryIconSource(provider, resolver, iconKey, source, logicalSize, dpr, tint, compact, alignBottom) {
        const value = source ? source.toString() : ""
        if (!value)
            return ""
        if (compact && value.startsWith("qrc:/F4QtHost/icons/zoin/")
                && provider && typeof provider.compactFileArtworkSource === "function")
            return provider.compactFileArtworkSource(value, Math.round(logicalSize), dpr)
        const monochrome = resolver.isMonochrome(iconKey, value)
        const name = iconKey || resolver.keyFromSource(value)
        const size = Math.max(1, Math.round(Number(logicalSize) || 1))
        const scale = Math.max(0.5, Number(dpr) || 1)
        const color = tint === undefined || tint === null ? "" : String(tint)
        const sized = monochrome && name && provider
                && typeof provider.rasterizedLucideSource === "function"
                && typeof provider.lucideStrokeWidth === "function"
            ? provider.rasterizedLucideSource(name, size, scale, color,
                provider.lucideStrokeWidth(Math.max(1, Number(logicalSize) || 1))).toString()
            : resolver.retargetProviderSource(value, size, scale, color, monochrome)
        return sized.startsWith("image://") && sized.indexOf("/lucide/") >= 0
            ? sized + (sized.indexOf("?") >= 0 ? "&" : "?") + "fileArt=1"
                + (alignBottom ? "&alignBottom=1" : "")
            : sized
    }

    function entryFallbackMarker(directory, parentEntry, marker) {
        return marker || (directory ? (parentEntry ? "↰" : "▸") : " ")
    }

    property color panelBackground: "#17191d"
    property color viewerBackground: "#090a0c"
    property color text: "#e8eaed"
    property color mutedText: "#aeb4bc"
    property color cursor: "#4c8bf5"
    property color cursorBackground: cursor
    property color cursorBorder: Qt.lighter(cursor, 1.35)
    property color cardCursorBorder: Qt.lighter(cursor, 1.35)
    property color selection: "#d8a31a"
    property bool showSelectionBorders: true
    property color markedBackground: Qt.darker(selection, 2.6)
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
    property color progressFill: scrollBarHandle
    property color progressTrack: Qt.darker(progressFill, 1.6)
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
