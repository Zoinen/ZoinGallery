pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    property real detailsRowInset: 8
    property real detailsRowSpacing: 8
    property real detailsIconSlotSize: 18
    property real detailsIconSize: 16
    property real detailsNameFontPixelSize: 13
    property real detailsSecondaryFontPixelSize: 12
    property real detailsExtensionMinimumWidth: 40
    property real detailsExtensionMaximumWidth: 80
    property real detailsSizeColumnWidth: 96
    // A non-positive value asks the panel to derive this from the active row
    // density. Embedders with a separate header supply an exact snapped value.
    property real detailsHeaderHeight: -1
    property real detailsHeaderCellInset: 8
    property real detailsHeaderFontPixelSize: 12
    property real detailsSeparatorVerticalMargin: 6
    property real detailsSeparatorWidth: 1
    property real detailsScrollBarWidth: 16
}
