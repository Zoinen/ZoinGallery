pragma ComponentBehavior: Bound

import QtQuick

// Temporary non-interactive padding visualization used by all panel modes.
Loader {
    objectName: "galleryLayoutDebugZone"
    property string zone: "text"
    anchors.fill: parent
    z: 20
    property color color: zone === "cell" ? "#1466dd88"
           : zone === "preview" ? "#3833ccee" : "#40ffaa55"
    property color borderColor: zone === "cell" ? "#8866dd88"
                                : zone === "preview" ? "#aa33ccee" : "#bbffaa55"
    active: visible
    source: active ? Qt.resolvedUrl("GalleryLayoutDebugPaint.qml") : ""
}
