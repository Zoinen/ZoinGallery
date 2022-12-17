import QtQuick
import QtQuick.Controls.Basic as T

T.ToolTip {
    id: control
    text: qsTr("A descriptive tool tip of what the button does")

    contentItem: Text {
        text: control.text
        font: control.font
        color: "#fff"
    }

    background: Rectangle {
        color: "#505050"
        border.color: "#8a8a8a"
    }
}
