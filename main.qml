import QtQuick 2.14
import QtQuick.Window 2.14
import QtQuick.Layouts 1.14
import QtQuick.Controls 2.14

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")

    ColumnLayout {
        anchors.fill: parent

        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 20
            Button {
                Layout.leftMargin: 10
                Layout.topMargin: 10
                width: 150
                height: 30

                text: "Do CD"
                onClicked: {
                    viewerController.doCd()
                }
            }

            Rectangle {
                color: "orange"
                Layout.preferredWidth: 20
                Layout.preferredHeight: 40
                antialiasing: true

                RotationAnimator on rotation {
                    loops: Animation.Infinite
                    duration: 2000
                    from: 0 ; to: 360
                }
            }
        }


        GridView {
            id: gridView

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            cellWidth: 200
            cellHeight: 200
            model: fileListModel

            ScrollBar.vertical: ScrollBar {
                id: scroll
                policy: ScrollBar.AlwaysOn
                visible: parent.contentHeight > parent.height

                hoverEnabled: true

                leftPadding: 0
                topPadding: 0
                rightPadding: 0
                bottomPadding: 0

                contentItem: Rectangle {
                    implicitWidth: 8
                    color: scroll.pressed ? "#8d8d8d" : scroll.hovered ? "#b9b9b9" : "#c1c1c1"
                }

                background: Rectangle {
                    color: "#f1f1f1"
                }
            }

            delegate: Rectangle {
                width: gridView.cellWidth
                height: gridView.cellHeight
                border.color: "black"
                border.width: 1
                color: "white"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 5

                    Image {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        fillMode: Image.PreserveAspectFit
//                        mipmap: true
                        source: "image://thumbnails/" + imageIdRole
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: displayRole + " [" + imageIdRole + "]"
                    }
                }
            }
        }
    }
}
