pragma ComponentBehavior: Bound

import QtQuick

Text {
    id: label
    property bool motionActive: false
    property int idleRenderType: Text.QtRendering
    property bool rendererInitialized: false

    Component.onCompleted: {
        idleRenderType = renderType
        rendererInitialized = true
    }
    onRenderTypeChanged: {
        if (!motionActive) idleRenderType = renderType
    }

    // Native glyphs snap in Qt's vertex shader. Distance fields can follow a
    // fractional translation; restore the original renderer when motion ends.
    Binding {
        target: label
        property: "renderType"
        value: Text.QtRendering
        when: label.rendererInitialized && label.motionActive
              && label.idleRenderType === Text.NativeRendering
        restoreMode: Binding.RestoreBindingOrValue
    }
}
