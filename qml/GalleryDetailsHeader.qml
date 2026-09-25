pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: header

    property var columnSchema: []
    required property color hoverColor
    required property color textColor
    required property color mutedTextColor
    required property color separatorColor
    required property real cellInset
    required property real separatorWidth
    required property real separatorVerticalMargin
    required property real textPixelSize
    property real devicePixelRatio: 1
    property point pixelGridOffset: Qt.point(0, 0)
    transform: Translate {
        x: header.pixelGridOffset.x
        y: header.pixelGridOffset.y
    }

    signal sortRequested(string sortMode, bool contextMenu)
    signal columnResizePreviewed(var columns)
    signal columnResizeRequested(var columns)

    objectName: "galleryDetailsHeader"
    color: "transparent"
    border.width: 0

    readonly property var columns:
        columnSchema && columnSchema.length > 0
        ? columnSchema : [
            { id: "name", role: "name", title: qsTr("Name"),
              width: 50, alignment: "left", sortMode: "name",
              sortable: true },
            { id: "size", role: "size", title: qsTr("Size"),
              width: 14, alignment: "right", sortMode: "size",
              sortable: true }
        ]
    readonly property real totalColumnWidth: {
        let total = 0
        for (let index = 0; index < columns.length; ++index)
            total += Math.max(1, Number(columns[index].width || 1))
        return Math.max(1, total)
    }

    function columnX(index) {
        let before = 0
        for (let candidate = 0; candidate < index; ++candidate)
            before += Math.max(1, Number(columns[candidate].width || 1))
        const local = Math.round(width * before / totalColumnWidth)
        const dpr = Math.max(0.01, Number(devicePixelRatio) || 1)
        return Math.round(local * dpr) / dpr
    }

    function columnWidth(index) {
        const start = columnX(index)
        return index === columns.length - 1
                ? width - start : columnX(index + 1) - start
    }

    function pointerXInHeader(item, itemX, itemY) {
        return item.mapToItem(header, itemX, itemY).x
    }

    function minimumColumnPixels(column) {
        const desired = String(column.id || column.role || "") === "name"
                ? 96 : 44
        const perColumn = width / Math.max(1, columns.length)
        return Math.min(desired, Math.max(1, perColumn * 0.7))
    }

    function resizedColumnsForDrag(startWidths, boundaryIndex, deltaPixels) {
        const totalWeight = startWidths.reduce(
                    (total, value) => total + value, 0)
        if (width <= 0 || totalWeight <= 0)
            return []
        const leftIndex = boundaryIndex
        const rightIndex = boundaryIndex + 1
        const pairWeight = startWidths[leftIndex] + startWidths[rightIndex]
        let minimumLeft = minimumColumnPixels(columns[leftIndex])
                * totalWeight / width
        let minimumRight = minimumColumnPixels(columns[rightIndex])
                * totalWeight / width
        if (minimumLeft + minimumRight > pairWeight) {
            const ratio = pairWeight / (minimumLeft + minimumRight)
            minimumLeft *= ratio
            minimumRight *= ratio
        }
        const desiredLeft = startWidths[leftIndex]
                + deltaPixels * totalWeight / width
        const nextLeft = Math.round(Math.max(minimumLeft,
            Math.min(pairWeight - minimumRight, desiredLeft)))
        const nextRight = Math.round(pairWeight - nextLeft)
        const result = []
        for (let index = 0; index < columns.length; ++index) {
            let weight = startWidths[index]
            if (index === leftIndex)
                weight = nextLeft
            else if (index === rightIndex)
                weight = nextRight
            result.push(Object.assign({}, columns[index], {
                width: Math.max(1, weight)
            }))
        }
        return result
    }

    Repeater {
        model: header.columns

        delegate: Rectangle {
            id: headerCell
            required property int index
            required property var modelData
            objectName: "galleryDetailsHeaderCell-" + index
            x: header.columnX(index)
            width: header.columnWidth(index)
            height: header.height
            color: headerPointer.containsMouse
                   && modelData.sortable === true
                   ? header.hoverColor : "transparent"

            Behavior on color { ColorAnimation { duration: 70 } }

            Text {
                id: headerCellText
                objectName: "galleryDetailsHeaderText-" + headerCell.index
                anchors.fill: parent
                anchors.leftMargin: header.cellInset
                anchors.rightMargin: header.cellInset
                text: headerCell.modelData.title || ""
                color: headerCell.modelData.sortable === true
                       ? header.textColor : header.mutedTextColor
                font.pixelSize: header.textPixelSize
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: headerCell.index > 0
                                     || headerCell.modelData.alignment === "right"
                                     ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideRight
            }

            Rectangle {
                objectName: "galleryDetailsHeaderSeparator-" + headerCell.index
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: header.separatorWidth
                height: Math.max(
                    1, parent.height - header.separatorVerticalMargin * 2)
                color: header.separatorColor
                opacity: headerCell.index < header.columns.length - 1
                         ? 0.65 : 0
            }

            MouseArea {
                id: headerPointer
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                enabled: headerCell.modelData.sortable === true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: mouse => {
                    header.sortRequested(
                        headerCell.modelData.sortMode
                        || headerCell.modelData.role
                        || headerCell.modelData.id || "name",
                        mouse.button === Qt.RightButton)
                    mouse.accepted = true
                }
            }
        }
    }

    Repeater {
        model: Math.max(0, header.columns.length - 1)

        delegate: MouseArea {
            id: resizeHandle
            required property int index
            objectName: "galleryDetailsResizeHandle-" + index
            readonly property real physicalWidth:
                Math.max(1, Math.round(8 * header.devicePixelRatio))
                / Math.max(0.01, header.devicePixelRatio)
            property real pointerStartX: 0
            property real boundaryStartX: 0
            property var widthsAtPress: []
            property var previewColumns: []
            property bool moved: false
            x: header.columnX(index + 1)
               - Math.round(physicalWidth * header.devicePixelRatio / 2)
                 / header.devicePixelRatio
            width: physicalWidth
            height: header.height
            z: 10
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SplitHCursor

            onPressed: mouse => {
                const pointX = header.pointerXInHeader(
                            resizeHandle, mouse.x, mouse.y)
                pointerStartX = pointX
                boundaryStartX = header.columnX(index + 1)
                widthsAtPress = header.columns.map(column =>
                    Math.max(1, Number(column.width || 1)))
                moved = false
                previewColumns = []
                mouse.accepted = true
            }

            onPositionChanged: mouse => {
                if (!(mouse.buttons & Qt.LeftButton)
                        || widthsAtPress.length !== header.columns.length)
                    return
                const pointerX = header.pointerXInHeader(
                            resizeHandle, mouse.x, mouse.y)
                const dpr = Math.max(0.01,
                                     Number(header.devicePixelRatio) || 1)
                const delta = Math.round((pointerX - pointerStartX) * dpr)
                        / dpr
                if (Math.abs(delta) < 0.01)
                    return
                const columns = header.resizedColumnsForDrag(
                            widthsAtPress, index, delta)
                if (columns.length !== widthsAtPress.length)
                    return
                moved = true
                previewColumns = columns
                header.columnResizePreviewed(columns)
            }

            onReleased: mouse => {
                if (moved && previewColumns.length > 0)
                    header.columnResizeRequested(previewColumns)
                widthsAtPress = []
                previewColumns = []
                moved = false
                mouse.accepted = true
            }

            onCanceled: {
                widthsAtPress = []
                previewColumns = []
                moved = false
            }
        }
    }

    Rectangle {
        objectName: "galleryDetailsHeaderBottomSeparator"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: header.separatorWidth
        color: header.separatorColor
        opacity: 0.7
    }
}
