#pragma once

#include <QString>

namespace ZoinGallery {

struct HdrDisplayState {
    QString deviceName;
    QString displayName;
    quint64 adapterId = 0;
    quint32 targetId = 0;
    bool supported = false;
    bool enabled = false;
    bool active = false;
    bool modernApi = false;
    int error = 0;
};

HdrDisplayState queryHdrDisplay(const QString &deviceName);
HdrDisplayState queryHdrDisplayState(const HdrDisplayState &display);
HdrDisplayState queryHdrDisplayForWindow(quintptr nativeWindow);
int setHdrDisplayEnabled(const HdrDisplayState &display, bool enabled);

} // namespace ZoinGallery
