import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ZoinGallery 1.0

Dialog {
    id: settingsDialog
    title: "Settings"
    modal: true
    anchors.centerIn: parent
    standardButtons: Dialog.Close

    background: Rectangle {
        color: Style.popupBackground
        border.color: Style.popupBorder
        radius: 4
    }

    contentItem: ColumnLayout {
        spacing: 10

        CheckBox {
            id: animateResizingCheckbox
            text: "Animate resizing items in layout"
            checked: masonryLayout.view.animateResizing
            onToggled: {
                masonryLayout.view.animateResizing = checked
            }
            contentItem: Text {
                text: animateResizingCheckbox.text
                font: animateResizingCheckbox.font
                color: Style.text
                verticalAlignment: Text.AlignVCenter
                leftPadding: animateResizingCheckbox.indicator.width + animateResizingCheckbox.spacing
            }
        }
    }
}
