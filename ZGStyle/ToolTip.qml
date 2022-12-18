import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as T

T.ToolTip {
    id: control
    text: qsTr("A descriptive tool tip of what the button does")

    contentItem: RowLayout {
        spacing: 13
        Text {
            id: mainText
            Layout.leftMargin: 4
            Layout.rightMargin: hotkeyText.visible ? 0 : 4
            text: {
                let split = control.text.split("\t")
                if (split.length > 1) {
                    return split[0]
                }
                return control.text
            }
            font: control.font
            color: "#fff"
        }
        Text {
            id: hotkeyText
            Layout.rightMargin: 4
            Layout.preferredHeight: mainText.height
            verticalAlignment: Text.AlignBottom
            text: {
                let split = control.text.split("\t")
                if (split.length > 1) {
                    visible = true
                    return split[1]
                }
                visible = false
                return ""
            }
            font.family: control.font.family
            font.pixelSize: 10
            color: "#fff"
            opacity: 0.5
        }
    }

    background: Rectangle {
        color: "#505050"
        border.color: "#8a8a8a"
    }
}
