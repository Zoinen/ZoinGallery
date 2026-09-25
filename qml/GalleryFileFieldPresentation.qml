pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property Item panelRoot
    required property var fileFieldDescriptors
    required property var columnSchema
    required property real devicePixelRatio

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

    function detailsColumnX(index) {
        const columns = columnSchema || []
        let total = 0
        let before = 0
        for (let candidate = 0; candidate < columns.length; ++candidate) {
            const extent = Math.max(1, Number(columns[candidate].width || 1))
            total += extent
            if (candidate < index)
                before += extent
        }
        const local = Math.round(detailsPixelExtent(panelRoot.width)
                                 * before / Math.max(1, total))
        return detailsPixelExtent(local)
    }

    function detailsColumnWidth(index) {
        const columns = columnSchema || []
        const start = detailsColumnX(index)
        return index === columns.length - 1
                ? detailsPixelExtent(panelRoot.width) - start
                : detailsColumnX(index + 1) - start
    }

}
