pragma ComponentBehavior: Bound

import QtQuick

// Motion does not change the selected text renderer. Native glyphs retain
// their own physical-pixel stepping; the shared viewport remains fractional.
Text {
    property bool motionActive: false
}
