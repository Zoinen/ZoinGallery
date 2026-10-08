pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Host-independent chrome backed by the same catalog, metadata and thumbnail
// provider as the panel. The filmstrip virtualizes delegates rather than
// building a second list of image data.
Item {
    id: osd
    objectName: "galleryViewerOsd"
    required property Item viewer
    readonly property real rightChromeInset: width - filmstripPanel.x
    readonly property var session: viewer.session
    readonly property var images: session ? session.imageModel : null
    property var exif: []
    readonly property color background: Qt.hsla(viewer.theme.dialogBackground.hslHue,
            viewer.theme.dialogBackground.hslSaturation * 0.3,
            Math.min(1, viewer.theme.dialogBackground.hslLightness + 0.075), 0.92)

    function refresh() {
        exif = session ? session.imageExifAt(viewer.presentedIndex) : []
    }
    function syncCursor() {
        refresh()
        if (images) {
            strip.currentIndex = images.mapFromSourceRow(viewer.presentedIndex)
            strip.positionViewAtIndex(strip.currentIndex, ListView.Contain)
        }
    }
    Component.onCompleted: syncCursor()
    onVisibleChanged: {
        if (visible) syncCursor()
        if (Qt.application.arguments.indexOf("--debug-viewer-osd") >= 0)
            console.debug("[FIX:viewer-osd] visible=", visible,
                          "row=", viewer.presentedIndex)
    }
    Connections {
        target: osd.viewer
        function onPresentedIndexChanged() { osd.syncCursor() }
    }
    Connections {
        target: osd.session ? osd.session.model : null
        function onDataChanged() {
            if (osd.visible) Qt.callLater(osd.refresh)
        }
        function onModelReset() { Qt.callLater(osd.syncCursor) }
        function onRowsInserted() { Qt.callLater(osd.syncCursor) }
    }

    Rectangle {
        id: filenamePanel
        objectName: "galleryViewerOsdFilenamePanel"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.topMargin: 12
        width: Math.max(0, Math.min(osd.width - filmstripPanel.width - 48,
                                   filename.implicitWidth + 24))
        height: filename.implicitHeight + 16
        radius: 8
        color: osd.background
        Text {
            id: filename
            objectName: "galleryViewerOsdFilename"
            anchors.fill: parent
            anchors.margins: 8
            text: osd.session ? osd.session.entryNameAt(osd.viewer.presentedIndex) : ""
            color: osd.viewer.theme.text
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideMiddle
        }
    }

    Rectangle {
        objectName: "galleryViewerExif"
        x: 12
        y: filenamePanel.y + filenamePanel.height + 12
        width: Math.min(220, osd.width * 0.35)
        height: Math.min(metadata.contentHeight + 24, osd.height - y - 12)
        color: osd.background
        radius: 8
        Flickable {
            id: metadata
            anchors.fill: parent
            anchors.margins: 12
            clip: true
            contentHeight: fields.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            ColumnLayout {
                id: fields
                width: metadata.width
                spacing: 0
                Repeater {
                    model: osd.exif
                    delegate: RowLayout {
                        id: field
                        objectName: "galleryViewerExifField-" + index
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.topMargin: index > 0 && !!modelData.title ? 15 : 0
                        Layout.bottomMargin: 2
                        spacing: 7
                        Image {
                            visible: !!field.modelData.icon
                            source: field.modelData.icon || ""
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                brightness: 1
                                colorization: 1
                                colorizationColor: osd.viewer.theme.mutedText
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: field.modelData.text || ""
                            color: field.modelData.title ? osd.viewer.theme.mutedText
                                                         : osd.viewer.theme.text
                            font.bold: !!field.modelData.title
                            wrapMode: Text.Wrap
                            MouseArea {
                                anchors.fill: parent
                                enabled: !!field.modelData.url
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally(field.modelData.url)
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: filmstripPanel
        objectName: "galleryViewerFilmstripPanel"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: 116
        radius: 8
        color: osd.background
        ListView {
            id: strip
            objectName: "galleryViewerFilmstrip"
            anchors.fill: parent
            anchors.margins: 8
            anchors.rightMargin: 24
            model: osd.images
            spacing: 8
            cacheBuffer: 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: thumbnail
                objectName: "galleryViewerOsdThumbnail-" + index
                required property int index
                required property string imageIdUrlRole
                required property bool selectedRole
                readonly property int sourceRow: osd.images.mapToSourceRow(index)
                width: strip.width
                height: 64
                radius: 4
                color: "transparent"
                border.width: strip.currentIndex === index || selectedRole ? 2 : 0
                border.color: selectedRole ? osd.viewer.theme.selection
                                          : osd.viewer.theme.cursorBorder
                function request() {
                    if (osd.visible && sourceRow >= 0)
                        osd.session.requestOsdThumbnailAt(sourceRow,
                            Math.ceil(width * osd.viewer.devicePixelRatio),
                            Math.ceil(height * osd.viewer.devicePixelRatio))
                }
                Component.onCompleted: Qt.callLater(request)
                onSourceRowChanged: Qt.callLater(request)
                Connections {
                    target: osd
                    function onVisibleChanged() { thumbnail.request() }
                }
                Image {
                    objectName: "galleryViewerOsdThumbnailImage-" + thumbnail.index
                    anchors.fill: parent
                    anchors.margins: 3
                    source: thumbnail.imageIdUrlRole
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        osd.viewer.emitNavigation(thumbnail.sourceRow)
                        osd.viewer.forceActiveFocus()
                    }
                }
            }
        }
        // ZoinGallery's filmstrip uses an image-index slider, not a
        // proportional viewport scrollbar: dragging it navigates images.
        Slider {
            id: imagePosition
            objectName: "galleryViewerFilmstripPosition"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 8
            width: 10
            orientation: Qt.Vertical
            rotation: 180
            focusPolicy: Qt.NoFocus
            padding: 0
            from: 0
            to: Math.max(0, strip.count - 1)
            value: Math.max(0, strip.currentIndex)
            stepSize: 1
            snapMode: Slider.SnapAlways
            enabled: strip.count > 1
            onMoved: {
                const row = osd.images.mapToSourceRow(Math.round(value))
                if (row >= 0) osd.viewer.emitNavigation(row)
                osd.viewer.forceActiveFocus()
            }
            handle: Item { }
            background: Rectangle {
                objectName: "galleryViewerFilmstripPositionTrack"
                color: osd.viewer.theme.progressTrack
                radius: 4
                Rectangle {
                    objectName: "galleryViewerFilmstripPositionFill"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: imagePosition.position * parent.height
                    radius: 4
                    color: osd.viewer.theme.progressFill
                }
            }
        }
    }
}
