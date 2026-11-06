#include "HdrDisplay.h"

namespace ZoinGallery {
HdrDisplayState queryHdrDisplay(const QString &deviceName)
{
    HdrDisplayState state;
    state.deviceName = deviceName;
    return state;
}
int setHdrDisplayEnabled(const HdrDisplayState &, bool) { return -1; }
HdrDisplayState queryHdrDisplayForWindow(quintptr) { return {}; }
HdrDisplayState queryHdrDisplayState(const HdrDisplayState &) { return {}; }
} // namespace ZoinGallery
