pragma ComponentBehavior: Bound

import QtQuick

import ZoinGallery.Native 1.0

QtObject {
    id: formatter

    required property GalleryPanelController controller
    required property bool controllerReady
    required property bool localQuickSearchEnabled
    required property color matchColor
    property var externalMatches: ({})

    function escapeStyledText(value) {
        return String(value === undefined || value === null ? "" : value)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/\"/g, "&quot;")
    }

    // Go publishes matcher offsets in runes while JavaScript indexes UTF-16
    // code units. Split surrogate pairs explicitly so non-BMP filenames keep
    // the same highlighted glyphs in both renderers.
    function codePoints(value) {
        const text = String(value === undefined || value === null ? "" : value)
        const result = []
        for (let index = 0; index < text.length;) {
            const first = text.charCodeAt(index)
            let width = 1
            if (first >= 0xd800 && first <= 0xdbff
                    && index + 1 < text.length) {
                const second = text.charCodeAt(index + 1)
                if (second >= 0xdc00 && second <= 0xdfff)
                    width = 2
            }
            result.push(text.substr(index, width))
            index += width
        }
        return result
    }

    function codePointLength(value) {
        return codePoints(value).length
    }

    function matchForEntry(entryId) {
        if (!entryId)
            return null
        let match = externalMatches
                ? externalMatches[String(entryId)] : null
        if (!match && localQuickSearchEnabled && controllerReady
                && controller.quickSearchActive) {
            // Reading the revision makes bindings which call this function
            // depend on the controller's typed quick-search state.
            const revision = controller.quickSearchRevision
            match = controller.quickSearchMatchForEntry(String(entryId))
            if (revision < 0)
                return null
        }
        if (!match)
            return null
        const start = Number(match.start)
        const length = Number(match.length)
        if (!Number.isInteger(start) || start < 0
                || !Number.isInteger(length) || length <= 0)
            return null
        return { "start": start, "length": length }
    }

    // sourceRuneOffset locates a displayed fragment (for example a separately
    // aligned extension) within the complete filename matched by the host.
    function styledTextForMatch(value, match, sourceRuneOffset) {
        if (!match)
            return String(value === undefined || value === null ? "" : value)

        const characters = codePoints(value)
        const offset = Math.max(0, Number(sourceRuneOffset) || 0)
        const localStart = Math.max(0, match.start - offset)
        const localEnd = Math.min(characters.length,
                                  match.start + match.length - offset)
        if (localStart >= localEnd)
            return escapeStyledText(characters.join(""))

        const prefix = escapeStyledText(
            characters.slice(0, localStart).join(""))
        const highlighted = escapeStyledText(
            characters.slice(localStart, localEnd).join(""))
        const suffix = escapeStyledText(
            characters.slice(localEnd).join(""))
        return prefix + "<font color=\"" + String(matchColor)
            + "\">" + highlighted + "</font>" + suffix
    }

    function styledText(value, entryId, sourceRuneOffset) {
        return styledTextForMatch(value, matchForEntry(entryId),
                                  sourceRuneOffset)
    }

    // Avoid walking the complete base name merely to calculate an extension
    // offset when fast find is inactive. Long WinSxS directory names made this
    // otherwise dominate an ordinary Details catalog replacement.
    function styledSuffix(value, prefix, entryId, prefixSeparatorLength) {
        if (!matchForEntry(entryId))
            return String(value === undefined || value === null ? "" : value)
        return styledText(value, entryId, codePointLength(prefix)
                          + Math.max(0,
                                     Number(prefixSeparatorLength) || 0))
    }

    // Icons mode can middle-elide very long names before QML paints them.
    // Preserve match offsets for the retained prefix and suffix around that
    // synthetic ellipsis.
    function styledElidedText(value, sourceValue, entryId) {
        if (!matchForEntry(entryId))
            return String(value === undefined || value === null ? "" : value)

        const shown = codePoints(value)
        const source = codePoints(sourceValue)
        if (shown.join("") === source.join(""))
            return styledText(value, entryId, 0)

        let ellipsis = -1
        for (let index = 0; index < shown.length; ++index) {
            if (shown[index] === "…") {
                ellipsis = index
                break
            }
        }
        if (ellipsis < 0)
            return styledText(value, entryId, 0)

        const prefix = shown.slice(0, ellipsis)
        const suffix = shown.slice(ellipsis + 1)
        if (prefix.join("") !== source.slice(0, prefix.length).join("")
                || suffix.join("")
                   !== source.slice(source.length - suffix.length).join(""))
            return styledText(value, entryId, 0)
        return styledText(prefix.join(""), entryId, 0)
            + escapeStyledText("…")
            + styledText(suffix.join(""), entryId,
                         source.length - suffix.length)
    }
}
