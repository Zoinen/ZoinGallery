import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: menu
    property color menuBackground: "#222c38"
    property color menuBorder: "#3a495b"
    property color menuHover: "#2a3745"
    popupType: Popup.Item
    dim: false
    padding: 6
    background: Rectangle {
        color: menu.menuBackground
        border.color: menu.menuBorder
        radius: 8
    }
}
