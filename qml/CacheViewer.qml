import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

import ZoinGallery.Native 1.0

Window {
    id: topLevelWindow
    color: Style.windowBackgroundNoQWK
    title: "Cache Viewer"

    x: 0
    y: 0
    width: 800
    height: 600

    visible: true

    ImageInfoModel {
        id: imageModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 20

            TextField {
                id: filterField
                Layout.fillWidth: true
                placeholderText: qsTr("Filter by path")
                onTextChanged: imageModel.setFilter(filterField.text)
            }

            Button {
                Layout.preferredWidth: 100
                text: qsTr("Refresh")
                onClicked: imageModel.refresh()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            ListView {
                id: listView

                Layout.fillWidth: true
                Layout.fillHeight: true

                highlightMoveDuration: 100
                activeFocusOnTab: true
                focus: true
                clip: true

                Keys.onUpPressed: decrementCurrentIndex()
                Keys.onDownPressed: incrementCurrentIndex()

                Keys.onPressed: function(event) {
                    switch (event.key) {
                        case Qt.Key_PageUp:
                            var newIndex = Math.max(0, currentIndex - visibleArea.heightRatio * count)
                            currentIndex = Math.floor(newIndex)
                            event.accepted = true
                            break
                        case Qt.Key_PageDown:
                            var newIndex = Math.min(count - 1, currentIndex + visibleArea.heightRatio * count)
                            currentIndex = Math.floor(newIndex)
                            event.accepted = true
                            break
                        case Qt.Key_Home:
                            currentIndex = 0
                            event.accepted = true
                            break
                        case Qt.Key_End:
                            currentIndex = count - 1
                            event.accepted = true
                            break
                    }
                }


                ScrollBar.vertical: ScrollBar {}
                model: imageModel
                delegate: Rectangle {
                    width: listView.width
                    height: 40
                    color: listView.currentIndex === index ? (listView.activeFocus ? Style.brickSelected : Style.brickImageHovered) : "transparent"
                    ColumnLayout {
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: 0

                        Label { Layout.alignment: Qt.AlignLeft; Layout.fillWidth: true; text: model.path; elide: Text.ElideMiddle }

                        RowLayout {
                            spacing: 10
                            Label { Layout.alignment: Qt.AlignLeft; text: model.lastModified.toString() }
                            Label { Layout.alignment: Qt.AlignLeft; text: model.thumbnailSize.toString() }
                            Label { Layout.alignment: Qt.AlignLeft; text: model.imageSize.toString() }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            // Logic to display image
                            listView.currentIndex = index
                            listView.forceActiveFocus()
                        }
                    }
                }

                onCurrentIndexChanged: {
                    imageViewer.source = imageModel.retrieveImage(currentIndex);
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Image {
                    id: imageViewer

                    anchors.fill: parent
                    fillMode: Image.PreserveAspectFit
                }
            }
        }
    }

}
