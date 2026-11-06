#pragma once

#include <QtCore/qglobal.h>
#include <cmath>

QT_BEGIN_NAMESPACE
// Optional renderer capability; all access is on the render thread. The pointer
// and function table have library lifetime. A sample is valid only in a Qt
// animation callback; unsupported drivers retain the ordinary animation clock.
struct F4AnimationClockInterface {
    quint32 version;
    quint32 size;
    bool (*sample)(double *milliseconds);
};
QT_END_NAMESPACE

class GalleryAnimationClock {
public:
    qreal deltaSeconds(int qtElapsedMilliseconds, const F4AnimationClockInterface *api) {
        double milliseconds = -1;
        const bool valid = api && api->version == 1
            && api->size >= sizeof(F4AnimationClockInterface) && api->sample
            && api->sample(&milliseconds) && std::isfinite(milliseconds) && milliseconds >= 0;
        qreal elapsed = qMax(0, qtElapsedMilliseconds) / 1000.0;
        if (valid && api == _interface && _milliseconds >= 0 && milliseconds >= _milliseconds)
            elapsed = (milliseconds - _milliseconds) / 1000.0;
        _interface = valid ? api : nullptr;
        _milliseconds = valid ? milliseconds : -1;
        return elapsed;
    }
    void reset() { _interface = nullptr; _milliseconds = -1; }
private:
    const F4AnimationClockInterface *_interface = nullptr;
    double _milliseconds = -1;
};
