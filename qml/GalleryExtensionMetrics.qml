pragma ComponentBehavior: Bound

import QtQuick

FontMetrics {
    required property real textWidth
    required property real maximumWidth
    property real minimumWidth: -1
    property real devicePixelRatio: 1

    readonly property real reservedWidth: {
        // FontMetrics methods do not establish a font binding dependency.
        const currentFont = font
        return snap(Math.min(maximumWidth,
                    minimumWidth >= 0 ? minimumWidth : advanceWidth("MMM") * 57 / 67))
    }
    readonly property real columnWidth:
        snap(Math.min(maximumWidth, Math.max(reservedWidth, textWidth)))
    readonly property int alignment:
        textWidth <= reservedWidth ? Text.AlignLeft : Text.AlignRight

    function snap(value) {
        const dpr = Math.max(0.01, devicePixelRatio)
        return Math.round(Math.max(0, value) * dpr) / dpr
    }
}
