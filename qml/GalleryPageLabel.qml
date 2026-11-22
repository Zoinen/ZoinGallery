pragma ComponentBehavior: Bound

import QtQuick
import ZoinGallery.PixelGrid 1.0

Text {
    id: label
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter

    // Align the text rectangle to the scene before Qt lays out its glyphs.
    transform: Translate {
        x: label.PixelGridAlignment.offsetX
        y: label.PixelGridAlignment.offsetY
    }
}
