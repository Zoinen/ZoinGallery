import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls

import ZoinGallery 1.0

pragma ComponentBehavior: Bound

Window {
    id: historyWindow

    required property QtObject hostWindow
    required property QtObject stateController
    required property Item galleryLayout
    required property QtObject navigationController
    required property QtObject catalogModel

    Shortcut {
        sequence: "Ctrl+Shift+H"
        onActivated: {
            historyWindow.visible = !historyWindow.visible
            if (historyWindow.visible) {
                historyWindow.refresh(true)
            }
        }
    }

    width: 520
    height: 620
    x: historyWindow.hostWindow.x + Math.max(0, historyWindow.hostWindow.width - width - 40)
    y: historyWindow.hostWindow.y + 80
    title: "Selection history"
    visible: false
    color: Style.windowBackgroundNoQWK

    property var historyModel: []
    property int historyIndex: -1

    function refresh(scrollToNewest = false) {
        historyModel = historyWindow.catalogModel.selectionHistoryForIndex(historyWindow.stateController.currentSourceIndex())
        historyIndex = historyWindow.catalogModel.selectionHistoryIndexForIndex(historyWindow.stateController.currentSourceIndex())
        if (scrollToNewest) {
            scrollHistoryToBottom()
        }
    }

    function scrollHistoryToBottom() {
        Qt.callLater(function() {
            if (selectionHistoryList.count > 0) {
                selectionHistoryList.positionViewAtEnd()
            }
        })
    }

    Connections {
        target: historyWindow.catalogModel
        function onSelectionHistoryChanged() {
            if (historyWindow.visible) {
                historyWindow.refresh(true)
            }
        }
    }

    Connections {
        target: historyWindow.galleryLayout.view
        function onCurrentIndexChanged() {
            if (historyWindow.visible) {
                historyWindow.refresh()
            }
        }
    }

    Connections {
        target: historyWindow.navigationController
        function onCurrentPathChanged() {
            if (historyWindow.visible) {
                historyWindow.refresh()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: historyWindow.catalogModel.selectionContainerForIndex(historyWindow.stateController.currentSourceIndex())
                    color: Style.text
                    elide: Text.ElideMiddle
                    maximumLineCount: 1
                }

                Text {
                    text: historyWindow.catalogModel.selectedCount + " selected"
                    color: Style.viewerSecondaryText
                    font.pixelSize: 12
                }
            }

            Button {
                implicitWidth: 36
                implicitHeight: 32
                icon.source: "qrc:/ZoinGallery/resources/Back.svg"
                inactive: historyWindow.historyIndex <= 0
                onClicked: {
                    if (!inactive) {
                        historyWindow.catalogModel.selectionHistoryBack(historyWindow.stateController.currentSourceIndex())
                    }
                }
            }

            Button {
                implicitWidth: 36
                implicitHeight: 32
                icon.source: "qrc:/ZoinGallery/resources/Forward.svg"
                inactive: historyWindow.historyIndex < 0 ||
                          historyWindow.historyIndex >= historyWindow.historyModel.length - 1
                onClicked: {
                    if (!inactive) {
                        historyWindow.catalogModel.selectionHistoryForward(historyWindow.stateController.currentSourceIndex())
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Style.lighter2
        }

        ListView {
            id: selectionHistoryList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: historyWindow.historyModel
            rightMargin: selectionHistoryScrollBar.width

            ScrollBar.vertical: ScrollBar {
                id: selectionHistoryScrollBar
                policy: ScrollBar.AlwaysOn
            }

            delegate: Rectangle {
                width: selectionHistoryList.width - selectionHistoryScrollBar.width
                height: 48
                radius: 4
                color: modelData.current ? Style.brickSelected : historyMouse.containsMouse ? Style.lighter : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Text {
                        Layout.preferredWidth: 34
                        text: modelData.index
                        color: Style.viewerSecondaryText
                        horizontalAlignment: Text.AlignRight
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: modelData.description
                            color: Style.text
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.timestamp
                            color: Style.viewerSecondaryText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        Layout.preferredWidth: 86
                        text: modelData.selectedCount + " selected"
                        color: Style.viewerSecondaryText
                        horizontalAlignment: Text.AlignRight
                    }
                }

                MouseArea {
                    id: historyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: historyWindow.catalogModel.jumpSelectionHistory(historyWindow.stateController.currentSourceIndex(), modelData.index)
                }
            }
        }
    }
}
