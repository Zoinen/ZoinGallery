import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import ZoinGallery 1.0

BrickItem {
    id: brickDelegate
    property string text
    property string imageId
    property int index
    property bool isImage
    property bool isDecodedImage
    property bool isFolder
    property string iconPath
    property bool folderView: false

    property real sizeBase: Math.min(width, height - 26) - masonryLayout.spacing

    Component {
        id: fileDelegate

        Item {
            Rectangle {
                id: delegateOutline
                anchors {
                    fill: icon
                    leftMargin: -2
                    topMargin: -2
                    rightMargin: -2
                    bottomMargin: -2 + (quickSearchMode ? (fileInfoPanel.height - fileName.contentHeight - icon.y) :
                                                          Math.max(0, fileName.height - fileName.contentHeight + sizeBase / 40 - icon.y))
                }
                color: Style.focus
                visible: masonryLayout.currentIndex === index
            }

            Rectangle {
                anchors {
                    fill: parent
                    leftMargin: 2
                    topMargin: 2
                    rightMargin: 2
                    bottomMargin: 2 + delegateOutline.anchors.bottomMargin + 2
                }
                color: "#1a4662"
                visible: masonryLayout.currentIndex === index
            }

            Image {
                id: icon
                x: parent.width / 2 - width / 2
                y: sizeBase / 10
                width: sizeBase - sizeBase / 10
                height: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                source: iconPath
            }

            Item {
                id: fileInfoPanel
                anchors {
                    left: parent.left
                    leftMargin: masonryLayout.spacing / 2
                    right: parent.right
                    rightMargin: masonryLayout.spacing / 2
                    top: icon.bottom
                    bottom: parent.bottom
                    bottomMargin: masonryLayout.spacing / 2
                }
                z: 1
                //                color: "#80FA8080"

                Text {
                    id: fileName
                    anchors{
                        fill: parent
                        margins: sizeBase / 40
                    }
                    width: parent.width

                    text: brickDelegate.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    color: brickMouseArea.containsMouse ? "#fff" : "#d1d1d1"
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: fileListDelegate

        Item {
            Rectangle {
                id: outline
                anchors {
                    fill: parent
                    leftMargin: 2
                    topMargin: 2
                    rightMargin: 2
                    bottomMargin: 2
                }
                color: "#1a4662"
                visible: masonryLayout.currentIndex === index
            }

            Image {
                id: icon
                anchors {
                    verticalCenter: parent.verticalCenter
                    left: outline.left
                    leftMargin: 10
                }
                height: parent.height - 10
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                source: iconPath
            }

            Text {
                id: fileName
                anchors {
                    left: icon.right
                    leftMargin: 5
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                }
                text: brickDelegate.text
                textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                elide: Text.ElideRight
                color: brickMouseArea.containsMouse ? "#fff" : "#d1d1d1"
                maximumLineCount: 1
            }
        }
    }

    Component {
        id: imageDelegate

        Item {
            Rectangle {
                id: delegateOutline
                anchors {
                    fill: image
                    leftMargin: -2
                    topMargin: -2
                    rightMargin: -2
                    bottomMargin: -2
                }
                color: Style.focus
                visible: masonryLayout.currentIndex === index
            }

            Image {
                id: image

                // Dirty hack to workaround blurry output. Remove someday
                property bool needScaling: image.sourceSize.width !== Math.round(image.width * dpr) ||
                                           image.sourceSize.height !== Math.round(image.height * dpr)
                width: Math.round((parent.width - masonryLayout.spacing) * dpr) / dpr
                height: Math.round((parent.height - masonryLayout.spacing) * dpr) / dpr
                smooth: needScaling

                x: masonryLayout.spacing / 2
                y: masonryLayout.spacing / 2
                fillMode: Image.PreserveAspectCrop
                cache: false
                source: imageId
                //                    opacity: 0.1
                //                    asynchronous: true
            }

            Rectangle {
                id: imageInfoPanel
                color: masonryLayout.currentIndex === index ? "#B3002943" : Qt.rgba(0, 0, 0, 0.5)
                anchors {
                    left: parent.left
                    leftMargin: masonryLayout.spacing / 2
                    right: parent.right
                    rightMargin: masonryLayout.spacing / 2
                    bottom: parent.bottom
                    bottomMargin: masonryLayout.spacing / 2
                }
                height: imageText.height + 10
                visible: brickMouseArea.containsMouse && !hideHovered ||
                         masonryLayout.currentIndex === index ||
                         masonryView.alwaysShowFileNames ||
                         masonryView.quickSearchMode
                z: 1

                Text {
                    id: imageText
                    anchors{
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 5
                    }

                    text: brickDelegate.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: brickMouseArea.containsMouse ? "#fff" : "#d1d1d1"
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: folderViewDelegate

        Item {
            Rectangle {
                anchors {
                    fill: parent
                    leftMargin: 2
                    topMargin: 2
                    rightMargin: 2
                    bottomMargin: 2
                }
                color: "#1a4662"
                visible: masonryLayout.currentIndex === index
            }

            Item {
                anchors {
                    fill: parent //icon
                    leftMargin: masonryLayout.spacing / 2
                    rightMargin: masonryLayout.spacing / 2
                    topMargin: masonryLayout.spacing / 2
                    bottomMargin: masonryLayout.spacing / 2 + imageInfoPanel.height
                }

                Rectangle {
                    id: folderBackground
                    anchors.fill: parent
                    anchors.topMargin: sizeBase / 27
                    color: Style.focus
                    radius: 10
                }

                Rectangle {
                    width: Math.min(110, parent.width * 0.44)
                    height: sizeBase / 27 + 10*2
                    radius: 10

                    color: Style.focus
                }

                MasonryLayout {
                    id: masonryLayout2

                    Connections {
                        target: masonryLayout
                        function onLayoutReset() {
                            masonryLayout2.reReadAndDecodeThumbnails()
                        }
                    }

                    anchors {
                        fill: folderBackground
                        margins: sizeBase / 20
                    }
                    clip: true
                    model: fileListModel.folderModel(index)

                    targetHeight: 100
                    spacing: 2

                    delegate: BrickItem {
                        id: brck
                        property string text
                        property string imageId
                        property int index
                        property bool isImage
                        property bool isFolder
                        property string iconPath
                        property bool folderView: false

                        Image {
                            id: image2

                            // Dirty hack to workaround blurry output. Remove someday
                            property bool needScaling: false
                            width: Math.round((parent.width - masonryLayout2.spacing) * dpr) / dpr
                            height: Math.round((parent.height - masonryLayout2.spacing) * dpr) / dpr
                            smooth: needScaling
                            cache: false

                            x: masonryLayout2.spacing / 2
                            y: masonryLayout2.spacing / 2
                            fillMode: Image.PreserveAspectCrop
                            source: imageId
                            //                    opacity: 0.1
                            //                    asynchronous: true
                        }
                    }
                }
            }

            Item {
                id: imageInfoPanel
                anchors {
                    left: parent.left
                    leftMargin: masonryLayout.spacing / 2
                    right: parent.right
                    rightMargin: masonryLayout.spacing / 2
                    bottom: parent.bottom
                    bottomMargin: masonryLayout.spacing / 2
                }
                height: imageText.height + 10
                z: 1

                Text {
                    id: imageText
                    anchors{
                        left: parent.left
                        leftMargin: /*masonryLayout.listView ? 45 :*/ 5
                        right: parent.right
                        rightMargin: 5
                        bottom: parent.bottom
                        bottomMargin: 5
                    }

                    text: brickDelegate.text
                    textFormat: quickSearchMode ? Text.RichText : Text.PlainText

                    horizontalAlignment: masonryLayout.listView ? Text.AlignLeft : Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: brickMouseArea.containsMouse ? "#fff" : "#d1d1d1"
                    maximumLineCount: 4
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    MouseArea {
        id: brickMouseArea
        anchors.fill: parent
        hoverEnabled: true

        onPressed: {
            focusProxy.forceActiveFocus()

            if (scrollingMode) {
                endScrolling()
            }
            setCurrentIndex(index)
        }

        onDoubleClicked: {
            if (isFolder) {
                viewerController.cd(text)
            }
            else {
                if (masonryLayout.currentItem.isImage) {
                    masonryView.toggleViewer()
                }
            }
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        sourceComponent: fileDelegate
    }

    states: [
        State {
            when: folderView
            PropertyChanges { loader.sourceComponent: folderViewDelegate }
        },
        State {
            when: imageId !== "" || isDecodedImage
            PropertyChanges { loader.sourceComponent: imageDelegate }
        },
        State {
            when: masonryLayout.listView && isFolder
            PropertyChanges { loader.sourceComponent: fileListDelegate }
        }
    ]
}
