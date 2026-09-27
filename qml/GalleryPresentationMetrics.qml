pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    property string panelFontFamily: ""
    property real panelFontPixelSize: 13
    property real columnPadding: 8
    property real detailsRowInset: columnPadding
    property real detailsRowSpacing: columnPadding
    property real detailsIconSlotSize: 18
    property real detailsIconSize: 16
    // Per-edge inset in compact rows; independent of the current zoom.
    property real detailsIconVerticalPadding: 3
    property real detailsNameFontPixelSize: 13
    property real detailsSecondaryFontPixelSize: 12
    // Negative uses a three-character lane measured in the extension font.
    property real detailsExtensionMinimumWidth: -1
    property real detailsExtensionMaximumWidth: 80
    property real detailsSizeColumnWidth: 96
    // A non-positive value asks the panel to derive this from the active row
    // density. Embedders with a separate header supply an exact snapped value.
    property real detailsHeaderHeight: -1
    property real detailsHeaderCellInset: columnPadding
    property real detailsHeaderFontPixelSize: 12
    property real detailsSeparatorVerticalMargin: 6
    property real detailsSeparatorWidth: 1
    property real detailsScrollBarWidth: 16
}
