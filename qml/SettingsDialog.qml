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

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Style.popupBorder
            Layout.topMargin: 10
            Layout.bottomMargin: 10
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 400
            Layout.preferredWidth: 450
            clip: true

            ColumnLayout {
                width: parent.width - 20
                spacing: 15

                component ShortcutHeader : Text {
                    color: Style.text
                    font.bold: true
                    font.pixelSize: 14
                    Layout.topMargin: 5
                }

                component ShortcutKey : Text {
                    color: Style.accentColor
                    font.bold: true
                    Layout.alignment: Qt.AlignRight | Qt.AlignTop
                }

                component ShortcutDesc : Text {
                    color: Style.text
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                ShortcutHeader { text: "Thumbnails Viewer" }

                GridLayout {
                    columns: 2
                    columnSpacing: 15
                    rowSpacing: 5
                    Layout.fillWidth: true

                    ShortcutKey { text: "Enter" }
                    ShortcutDesc { text: "Open image / folder" }

                    ShortcutKey { text: "Backspace" }
                    ShortcutDesc { text: "Go up one folder" }

                    ShortcutKey { text: "Alt + Left / Right" }
                    ShortcutDesc { text: "Go back / forward in history" }

                    ShortcutKey { text: "Ctrl + C / Insert" }
                    ShortcutDesc { text: "Copy file name" }

                    ShortcutKey { text: "Ctrl + D" }
                    ShortcutDesc { text: "Copy full path" }

                    ShortcutKey { text: "+ / -" }
                    ShortcutDesc { text: "Zoom in / out" }

                    ShortcutKey { text: "F8" }
                    ShortcutDesc { text: "Toggle List / Grid view" }

                    ShortcutKey { text: "F9" }
                    ShortcutDesc { text: "Toggle transparent grid" }

                    ShortcutKey { text: "F10" }
                    ShortcutDesc { text: "Toggle recursive view" }

                    ShortcutKey { text: "F11" }
                    ShortcutDesc { text: "Toggle fullscreen" }

                    ShortcutKey { text: "Type any text" }
                    ShortcutDesc { text: "Quick search" }
                }

                ShortcutHeader { text: "Viewer" }

                GridLayout {
                    columns: 2
                    columnSpacing: 15
                    rowSpacing: 5
                    Layout.fillWidth: true

                    ShortcutKey { text: "Left / Right" }
                    ShortcutDesc { text: "Previous / Next image" }

                    ShortcutKey { text: "Up / Down" }
                    ShortcutDesc { text: "Pan image (if zoomed in)" }

                    ShortcutKey { text: "Ctrl + Arrows" }
                    ShortcutDesc { text: "Pan image faster" }

                    ShortcutKey { text: "Ctrl + 1 / * / 9" }
                    ShortcutDesc { text: "Zoom to 100%" }

                    ShortcutKey { text: "Ctrl + 2" }
                    ShortcutDesc { text: "Zoom to 50%" }

                    ShortcutKey { text: "Ctrl + 3" }
                    ShortcutDesc { text: "Zoom to 25%" }

                    ShortcutKey { text: "Ctrl + 0" }
                    ShortcutDesc { text: "Zoom to fit" }

                    ShortcutKey { text: "Z / 0" }
                    ShortcutDesc { text: "Toggle zoom to fit" }

                    ShortcutKey { text: "Tab" }
                    ShortcutDesc { text: "Toggle UI panels" }

                    ShortcutKey { text: "` (Tilde)" }
                    ShortcutDesc { text: "Toggle previous/next image" }

                    ShortcutKey { text: "Esc / Enter" }
                    ShortcutDesc { text: "Close viewer" }

                    ShortcutKey { text: "S / P" }
                    ShortcutDesc { text: "Toggle spheric viewer mode" }

                    ShortcutKey { text: "[ / ]" }
                    ShortcutDesc { text: "Rotate image left / right" }
                }
            }
        }
    }
}
