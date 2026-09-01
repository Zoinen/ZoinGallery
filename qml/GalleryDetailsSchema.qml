pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    required property GalleryPanel panel

    function column(role, fallbackTitle) {
        const columns = panel.columnSchema || []
        for (let index = 0; index < columns.length; ++index) {
            if ((columns[index].role || columns[index].id) === role)
                return columns[index]
        }
        return { role: role, title: fallbackTitle, sortMode: role }
    }

    function columns() {
        if (panel.columnSchema && panel.columnSchema.length > 0)
            return panel.columnSchema
        return [
            { id: "name", role: "name", title: qsTr("Name"),
              width: 50, alignment: "left", sortMode: "name",
              sortable: true },
            { id: "size", role: "size", title: qsTr("Size"),
              width: 14, alignment: "right", sortMode: "size",
              sortable: true }
        ]
    }
}
