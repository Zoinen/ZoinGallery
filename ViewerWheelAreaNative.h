#ifndef VIEWERWHEELAREANATIVE_H
#define VIEWERWHEELAREANATIVE_H

struct ViewerWheelNativeInfo
{
    bool valid = false;
    bool momentum = false;
    int phase = 0;
    int momentumPhase = 0;
};

ViewerWheelNativeInfo currentViewerWheelNativeInfo();

#endif // VIEWERWHEELAREANATIVE_H
