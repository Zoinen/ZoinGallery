#include "ViewerWheelAreaNative.h"

#include <QtGlobal>

#ifndef Q_OS_MACOS
ViewerWheelNativeInfo currentViewerWheelNativeInfo()
{
    return {};
}
#endif
