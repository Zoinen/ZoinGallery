import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as T
import Qt.labs.settings
import ZoinGallery 1.0

Window {
    id: settingsDialog
    title: "Settings"
    visible: false
    width: 720
    height: 640
    minimumWidth: 560
    minimumHeight: 460
    color: Style.windowBackgroundNoQWK
    flags: Qt.Window

    property int currentPage: 0
    property bool positionedOnce: false

    property var thumbnailShortcuts: [
        { key: "Enter", desc: "Open image / folder" },
        { key: "Backspace", desc: "Go up one folder" },
        { key: "Alt + Left / Right", desc: "Go back / forward in history" },
        { key: "Ctrl + C / Insert", desc: "Copy file name" },
        { key: "Ctrl + D", desc: "Copy full path" },
        { key: "+ / -", desc: "Zoom in / out" },
        { key: "F8", desc: "Toggle List / Grid view" },
        { key: "F9", desc: "Toggle transparent grid" },
        { key: "F10", desc: "Toggle recursive view" },
        { key: "Tab", desc: "Toggle file names" },
        { key: "Ctrl + \\", desc: "Show selected items only" },
        { key: "F11", desc: "Toggle fullscreen" },
        { key: "Type any text", desc: "Quick search" }
    ]

    property var viewerShortcuts: [
        { key: "Left / Right", desc: "Previous / Next image" },
        { key: "Up / Down", desc: "Pan image if zoomed in" },
        { key: "Ctrl + Arrows", desc: "Pan image faster" },
        { key: "Ctrl + 1 / * / 9", desc: "Zoom to 100%" },
        { key: "Ctrl + 2", desc: "Zoom to 50%" },
        { key: "Ctrl + 3", desc: "Zoom to 25%" },
        { key: "Ctrl + 0", desc: "Zoom to fit" },
        { key: "Z / 0", desc: "Toggle zoom to fit" },
        { key: "Tab", desc: "Toggle UI panels" },
        { key: "` (Tilde)", desc: "Toggle previous/next image" },
        { key: "Esc / Enter", desc: "Close viewer" },
        { key: "S / P", desc: "Toggle spheric viewer mode" },
        { key: "[ / ]", desc: "Rotate image left / right" }
    ]

    function open() {
        if (!positionedOnce && typeof topLevelWindow !== "undefined") {
            x = topLevelWindow.x + Math.max(40, Math.round((topLevelWindow.width - width) / 2))
            y = topLevelWindow.y + Math.max(40, Math.round((topLevelWindow.height - height) / 2))
            positionedOnce = true
        }
        visible = true
        fileListModel.refreshCacheInfo()
        raise()
        requestActivate()
    }

    function formatCacheSize(bytes) {
        if (bytes < 1024)
            return bytes + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
    }

    Timer {
        interval: 2000
        repeat: true
        running: settingsDialog.visible
        onTriggered: fileListModel.refreshCacheInfo()
    }

    Settings {
        id: generalSettings
        category: "General"
        property bool singleInstanceByDefault: true
        property string dockIconMode: "windowVisible"
    }

    component PageButton: T.Button {
        id: pageButton
        required property int page

        focusPolicy: Qt.NoFocus
        implicitWidth: 128
        implicitHeight: 32
        padding: 0

        contentItem: Text {
            text: pageButton.text
            color: Style.text
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 4
            color: pageButton.down ? Style.darker
                   : pageButton.hovered ? Style.lighter2
                   : settingsDialog.currentPage === pageButton.page ? Style.lighter
                   : "transparent"

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                width: 18
                height: 3
                radius: 3
                visible: settingsDialog.currentPage === pageButton.page
                color: Style.accentColor
            }
        }

        onClicked: settingsDialog.currentPage = page
    }

    component SectionHeader: Text {
        color: Style.text
        font.bold: true
        font.pixelSize: 14
        Layout.fillWidth: true
        Layout.topMargin: 6
    }

    component SecondaryText: Text {
        color: Style.viewerSecondaryText
        font.pixelSize: 12
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Divider: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Style.lighter2
    }

    component StyledCheckBox: T.CheckBox {
        id: control
        focusPolicy: Qt.NoFocus
        spacing: 10
        implicitHeight: 28

        indicator: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 18
            height: 18
            radius: 3
            color: control.checked ? Style.accentColor
                   : control.down ? Style.darker
                   : control.hovered ? Style.lighter
                   : "transparent"
            border.width: 1
            border.color: control.checked ? Style.accentColor : Style.scrollBarHandle

            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 1
                visible: control.checked
                color: Style.windowBackgroundNoQWK
            }
        }

        contentItem: Text {
            text: control.text
            color: Style.text
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            leftPadding: control.indicator.width + control.spacing
            elide: Text.ElideRight
        }
    }

    component StyledButton: T.Button {
        id: control
        focusPolicy: Qt.NoFocus
        implicitHeight: 34
        padding: 0

        contentItem: Text {
            text: control.text
            color: Style.text
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 4
            color: !control.enabled ? "transparent"
                   : control.down ? Style.darker
                   : control.hovered ? Style.lighter2
                   : Style.lighter
            border.width: 1
            border.color: Style.lighter2
        }
    }

    component StyledComboBox: T.ComboBox {
        id: control
        focusPolicy: Qt.NoFocus
        implicitHeight: 34
        textRole: "text"
        valueRole: "value"

        contentItem: Text {
            text: control.displayText
            color: Style.text
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
            rightPadding: 30
            elide: Text.ElideRight
        }

        indicator: Text {
            x: control.width - width - 10
            y: control.topPadding + control.availableHeight / 2 - height / 2
            text: "v"
            color: Style.viewerSecondaryText
            font.pixelSize: 13
        }

        background: Rectangle {
            radius: 4
            color: control.down ? Style.darker : control.hovered ? Style.lighter2 : Style.lighter
            border.width: 1
            border.color: Style.lighter2
        }

        popup: T.Popup {
            y: control.height + 4
            width: control.width
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 220)
            padding: 4

            background: Rectangle {
                radius: 4
                color: Style.menuBackground
                border.width: 1
                border.color: Style.menuBorder
            }

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
            }
        }

        delegate: T.ItemDelegate {
            id: itemDelegate
            width: control.width - 8
            height: 30
            text: model.text
            highlighted: control.highlightedIndex === index

            contentItem: Text {
                text: itemDelegate.text
                color: Style.text
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: 3
                color: itemDelegate.highlighted ? Style.lighter2 : "transparent"
            }
        }
    }

    component SettingRow: RowLayout {
        id: row
        required property string label
        property string detail: ""
        property alias control: controlSlot.data

        Layout.fillWidth: true
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: row.label
                color: Style.text
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: row.detail
                visible: text !== ""
                color: Style.viewerSecondaryText
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }

        Item {
            id: controlSlot
            Layout.preferredWidth: 260
            Layout.minimumWidth: 180
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: children.length ? children[0].implicitHeight : 0
        }
    }

    component ShortcutSection: ColumnLayout {
        id: section
        required property string title
        required property var shortcuts

        Layout.fillWidth: true
        spacing: 8

        SectionHeader { text: section.title }

        Repeater {
            model: section.shortcuts

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Text {
                    Layout.preferredWidth: 150
                    Layout.alignment: Qt.AlignTop
                    text: modelData.key
                    color: Style.accentColor
                    font.bold: true
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: modelData.desc
                    color: Style.text
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "Settings"
                color: Style.text
                font.bold: true
                font.pixelSize: 20
                elide: Text.ElideRight
            }

            StyledButton {
                text: "Close"
                Layout.preferredWidth: 86
                onClicked: settingsDialog.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            radius: 5
            color: Style.tabBarBackground
            border.width: 1
            border.color: Style.tabBarBorder

            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2

                PageButton {
                    text: "General"
                    page: 0
                }

                PageButton {
                    text: "Shortcuts"
                    page: 1
                }

                Item { Layout.fillWidth: true }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: settingsDialog.currentPage

            T.ScrollView {
                clip: true

                ColumnLayout {
                    width: Math.max(parent.width - 20, 480)
                    spacing: 14

                    SectionHeader { text: "Behavior" }

                    StyledCheckBox {
                        text: "Animate resizing items in layout"
                        checked: masonryLayout.view.animateResizing
                        onToggled: masonryLayout.view.animateResizing = checked
                    }

                    StyledCheckBox {
                        text: "Use single instance by default"
                        checked: generalSettings.singleInstanceByDefault
                        onToggled: generalSettings.singleInstanceByDefault = checked
                    }

                    Divider {}

                    SectionHeader { text: "Cache" }

                    SettingRow {
                        label: "Image cache"
                        detail: "Controls cached image metadata and previews."
                        control: StyledComboBox {
                            id: imageCacheModeComboBox
                            width: parent.width
                            model: ListModel {
                                ListElement { text: "Off"; value: 0 }
                                ListElement { text: "On"; value: 1 }
                                ListElement { text: "Only cache"; value: 2 }
                            }

                            function syncFromModel() {
                                let index = indexOfValue(fileListModel.imageCacheMode)
                                currentIndex = index >= 0 ? index : indexOfValue(1)
                            }

                            Component.onCompleted: syncFromModel()
                            onActivated: fileListModel.imageCacheMode = currentValue

                            Connections {
                                target: fileListModel
                                function onImageCacheModeChanged() {
                                    imageCacheModeComboBox.syncFromModel()
                                }
                            }
                        }
                    }

                    SettingRow {
                        label: "Image cache data"
                        detail: fileListModel.imageCacheLocation
                        control: RowLayout {
                            width: parent.width
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: settingsDialog.formatCacheSize(fileListModel.imageCacheSize)
                                color: Style.accentColor
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            StyledButton {
                                text: "Clear"
                                enabled: fileListModel.imageCacheSize > 0
                                Layout.preferredWidth: 82
                                onClicked: fileListModel.clearImageCache()
                            }
                        }
                    }

                    SettingRow {
                        label: "File list cache"
                        detail: "Controls cached directory contents and folder previews."
                        control: StyledComboBox {
                            id: fileListCacheModeComboBox
                            width: parent.width
                            model: ListModel {
                                ListElement { text: "Off"; value: 0 }
                                ListElement { text: "On"; value: 1 }
                                ListElement { text: "Only cache"; value: 2 }
                            }

                            function syncFromModel() {
                                let index = indexOfValue(fileListModel.fileListCacheMode)
                                currentIndex = index >= 0 ? index : indexOfValue(1)
                            }

                            Component.onCompleted: syncFromModel()
                            onActivated: fileListModel.fileListCacheMode = currentValue

                            Connections {
                                target: fileListModel
                                function onFileListCacheModeChanged() {
                                    fileListCacheModeComboBox.syncFromModel()
                                }
                            }
                        }
                    }

                    SettingRow {
                        label: "File list cache data"
                        detail: fileListModel.fileListCacheLocation
                        control: RowLayout {
                            width: parent.width
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: settingsDialog.formatCacheSize(fileListModel.fileListCacheSize)
                                color: Style.accentColor
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            StyledButton {
                                text: "Clear"
                                enabled: fileListModel.fileListCacheSize > 0
                                Layout.preferredWidth: 82
                                onClicked: fileListModel.clearFileListCache()
                            }
                        }
                    }

                    Divider {}

                    SectionHeader { text: "Appearance" }

                    SettingRow {
                        label: "Dock icon"
                        detail: "Controls when ZoinGallery appears in the macOS Dock."
                        visible: Qt.platform.os === "osx"
                        control: StyledComboBox {
                            id: dockIconModeComboBox
                            width: parent.width
                            model: ListModel {
                                ListElement { text: "When window is open"; value: "windowVisible" }
                                ListElement { text: "Always"; value: "always" }
                                ListElement { text: "Never"; value: "never" }
                            }

                            function syncFromSettings() {
                                let index = indexOfValue(generalSettings.dockIconMode)
                                currentIndex = index >= 0 ? index : indexOfValue("windowVisible")
                            }

                            Component.onCompleted: syncFromSettings()

                            onActivated: {
                                generalSettings.dockIconMode = currentValue
                                viewerController.applyDockIconPreference(true)
                            }

                            Connections {
                                target: generalSettings
                                function onDockIconModeChanged() {
                                    dockIconModeComboBox.syncFromSettings()
                                }
                            }
                        }
                    }

                    SettingRow {
                        label: "Target color space"
                        detail: "Detected from the screen that owns the main window."
                        control: Text {
                            width: parent.width
                            text: topLevelWindow.targetColorSpaceDescription
                            color: Style.accentColor
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }

                    StyledCheckBox {
                        text: "Convert images to target color space"
                        checked: topLevelWindow.convertToDisplayColorSpace
                        onToggled: topLevelWindow.convertToDisplayColorSpace = checked
                    }

                    Divider {}

                    SectionHeader { text: "System" }

                    SettingRow {
                        label: "Version"
                        detail: "Installed application build."
                        control: Text {
                            width: parent.width
                            text: zoinVersion
                            color: Style.accentColor
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        StyledButton {
                            text: "Make Default Image Viewer"
                            Layout.preferredWidth: 230
                            onClicked: defaultImageViewerStatus.text = viewerController.makeDefaultImageViewer()
                        }

                        Text {
                            id: defaultImageViewerStatus
                            Layout.fillWidth: true
                            text: ""
                            visible: text !== ""
                            color: Style.viewerSecondaryText
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            T.ScrollView {
                clip: true

                ColumnLayout {
                    width: Math.max(parent.width - 20, 480)
                    spacing: 16

                    ShortcutSection {
                        title: "Thumbnails Viewer"
                        shortcuts: settingsDialog.thumbnailShortcuts
                    }

                    Divider {}

                    ShortcutSection {
                        title: "Viewer"
                        shortcuts: settingsDialog.viewerShortcuts
                    }
                }
            }
        }
    }
}
