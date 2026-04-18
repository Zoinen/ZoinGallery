pragma Singleton
import QtQuick
import Qt.labs.settings

QtObject {
    id: root

    property alias animateResizing: settings.animateResizing

    Settings {
        id: settings
        category: "General"
        property bool animateResizing: true
    }
}
