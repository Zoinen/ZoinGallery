#include "ViewerWheelAreaNative.h"

#include <QtGlobal>

#ifdef Q_OS_MACOS
#import <AppKit/NSApplication.h>
#import <AppKit/NSEvent.h>

ViewerWheelNativeInfo currentViewerWheelNativeInfo()
{
    NSEvent *event = [NSApp currentEvent];
    if (!event || [event type] != NSEventTypeScrollWheel) {
        return {};
    }

    ViewerWheelNativeInfo info;
    info.valid = true;
    info.phase = int([event phase]);
    info.momentumPhase = int([event momentumPhase]);
    info.momentum = [event momentumPhase] != NSEventPhaseNone;
    return info;
}
#endif
