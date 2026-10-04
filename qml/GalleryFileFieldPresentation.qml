pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    id: presentation
    required property Item panelRoot
    required property var fileFieldDescriptors
    required property var columnSchema
    required property real devicePixelRatio
    readonly property FontMetrics columnMetrics: FontMetrics {
        font: Qt.font({
            family: presentation.panelRoot.metrics.panelFontFamily,
            pixelSize: presentation.panelRoot.metrics.detailsSecondaryFontPixelSize
        })
    }
    // Evaluate once per schema/font/viewport change, not once per visible row.
    readonly property var pixelWidths: {
        const fontDependency = columnMetrics.font
        const columns = columnSchema || []
        const available = detailsContentWidth()
        const totalWeight = columns.reduce((sum, column) =>
            sum + Math.max(1, Number(column.width || 1)), 0)
        const padding = panelRoot.metrics.columnPadding * 2
        let nameIndex = -1
        let widestDigit = "0"
        for (let digit = 1; digit < 10; ++digit) {
            if (columnMetrics.advanceWidth(String(digit))
                    > columnMetrics.advanceWidth(widestDigit))
                widestDigit = String(digit)
        }
        const sizeSample = Array(3).fill(widestDigit.repeat(3)).join(" ") + " B"
        const widths = columns.map((column, index) => {
            if (!column.autoWidth)
                return available * Math.max(1, Number(column.width || 1)) / Math.max(1, totalWeight)
            const role = String(column.role || column.id || "")
            if (role === "name") {
                nameIndex = index
                return 0
            }
            const content = role === "size" ? columnMetrics.advanceWidth(sizeSample)
                : columnMetrics.advanceWidth("0") * Math.max(1, Number(column.width || 1))
            return Math.max(content, columnMetrics.advanceWidth(String(column.title || ""))) + padding
        })
        const used = widths.reduce((sum, width) => sum + width, 0)
        if (nameIndex >= 0) {
            const minimumName = Math.min(available / Math.max(1, columns.length),
                                         columnMetrics.advanceWidth("MMMM") + padding)
            if (used > available - minimumName && used > 0) {
                const scale = Math.max(0, available - minimumName) / used
                for (let index = 0; index < widths.length; ++index)
                    widths[index] *= scale
            }
            widths[nameIndex] = Math.max(0, available - widths.reduce((sum, width) => sum + width, 0))
        } else if (used > 0) {
            for (let index = 0; index < widths.length; ++index)
                widths[index] *= available / used
        }
        return widths
    }

    function detailsFieldColumns() {
        const columns = columnSchema || []
        const fields = []
        for (let index = 0; index < columns.length; ++index) {
            const role = String(columns[index].role || columns[index].id || "")
            if (role === "name" || role === "size")
                continue
            fields.push({ schemaIndex: index, column: columns[index] })
        }
        return fields
    }

    function fileFieldDescriptor(fieldId) {
        const descriptors = fileFieldDescriptors || []
        for (let index = 0; index < descriptors.length; ++index) {
            if (String(descriptors[index].id || "") === fieldId)
                return descriptors[index]
        }
        return null
    }

    function formattedFieldNumber(value, precision, trimZeros) {
        const number = Number(value)
        if (!Number.isFinite(number) || number <= 0)
            return ""
        const digits = Math.max(0, Math.min(12, Number(precision) || 0))
        let result = number.toFixed(digits)
        if (trimZeros && result.indexOf(".") >= 0)
            result = result.replace(/0+$/, "").replace(/\.$/, "")
        return result
    }

    function formattedFieldUnit(unit) {
        if (unit === "s")
            return "с"
        if (unit === "mm")
            return "мм"
        return unit
    }

    function formatDetailsField(fieldId, rawValue) {
        if (rawValue === undefined || rawValue === null)
            return ""
        const descriptor = fileFieldDescriptor(String(fieldId))
        if (!descriptor)
            return typeof rawValue === "object" ? "" : String(rawValue)

        const kind = String(descriptor.kind || "text")
        const format = String(descriptor.format || "")
        const unit = String(descriptor.unit || "")
        const displayUnit = formattedFieldUnit(unit)
        const precision = Number(descriptor.precision || 0)
        if (kind === "text")
            return String(rawValue).trim()
        if (kind === "range") {
            if (typeof rawValue !== "object")
                return ""
            const minimum = Number(rawValue.min)
            const maximum = Number(rawValue.max)
            if (!Number.isFinite(minimum) || !Number.isFinite(maximum)
                    || minimum <= 0 || maximum < minimum)
                return ""
            const minimumText = formattedFieldNumber(minimum, precision, true)
            const maximumText = formattedFieldNumber(maximum, precision, true)
            const rangeText = minimum === maximum
                    ? minimumText : minimumText + "–" + maximumText
            return displayUnit === "" ? rangeText
                                       : rangeText + " " + displayUnit
        }

        const number = Number(rawValue)
        if (!Number.isFinite(number) || number <= 0
                || (kind === "integer" && Math.trunc(number) !== number))
            return ""
        if (format === "exposure") {
            if (number < 1) {
                const reciprocal = 1 / number
                const denominator = Math.round(reciprocal)
                if (denominator > 0 && denominator <= 1000000
                        && Math.abs(reciprocal - denominator) / denominator < 0.02)
                    return "1/" + denominator
                            + (displayUnit ? " " + displayUnit : "")
            }
            const seconds = formattedFieldNumber(number, precision, true)
            return displayUnit === "" ? seconds
                                      : seconds + " " + displayUnit
        }
        if (format === "aperture")
            return "f/" + formattedFieldNumber(number, precision, true)
        const numberText = formattedFieldNumber(number, precision, true)
        return displayUnit === "" ? numberText
                                  : numberText + " " + displayUnit
    }

    function detailsPixelExtent(value) {
        const dpr = Math.max(0.01, Number(devicePixelRatio) || 1)
        return Math.round(Number(value || 0) * dpr) / dpr
    }

    function detailsRowContentInset() {
        // Column boundaries cover the row; padding belongs inside each cell.
        return 0
    }

    function detailsContentWidth() {
        const layout = panelRoot.galleryLayout
        const availableWidth = layout
                ? Math.max(0, Number(layout.width || 0)
                           - Number(layout.paddingLeft || 0)
                           - Number(layout.paddingRight || 0))
                : Math.max(0, Number(panelRoot.width || 0))
        return detailsPixelExtent(Math.max(
            0, availableWidth - detailsRowContentInset() * 2))
    }

    function detailsColumnX(index) {
        let before = 0
        for (let candidate = 0; candidate < index; ++candidate)
            before += pixelWidths[candidate] || 0
        const local = Math.round(before)
        return detailsPixelExtent(detailsRowContentInset() + local)
    }

    function detailsColumnWidth(index) {
        const columns = columnSchema || []
        const start = detailsColumnX(index)
        return index === columns.length - 1
                ? detailsRowContentInset() + detailsContentWidth() - start
                : detailsColumnX(index + 1) - start
    }

}
