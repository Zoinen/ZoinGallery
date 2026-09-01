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

    signal sortRequested(string sortMode, bool contextMenu)

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
        return Math.round(width * before / totalColumnWidth)
    }

    function columnWidth(index) {
        const start = columnX(index)
        return index === columns.length - 1
                ? width - start : columnX(index + 1) - start
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
