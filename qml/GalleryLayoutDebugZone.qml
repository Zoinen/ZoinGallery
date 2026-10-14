pragma ComponentBehavior: Bound

import QtQuick

// Temporary non-interactive padding visualization used by all panel modes.
Rectangle {
    objectName: "galleryLayoutDebugZone"
    property string zone: "text"
    anchors.fill: parent
    z: 20
    color: zone === "cell" ? "#1466dd88"
           : zone === "preview" ? "#3833ccee" : "#40ffaa55"
    border.color: zone === "cell" ? "#8866dd88"
                  : zone === "preview" ? "#aa33ccee" : "#bbffaa55"
}
