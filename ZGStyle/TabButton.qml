import QtQuick
import QtQuick.Controls.Basic as T
import QtQuick.Layouts

import "."

T.TabButton {
    id: control

    focusPolicy: Qt.NoFocus

    background: Rectangle {
        color: control.down ? Style.controlBackgroundPressed : (control.hovered ? Style.controlBackground : "transparent")
    }
}
