#ifndef IMAGEPROBE_H
#define IMAGEPROBE_H

#include <QImage>
#include <QSharedPointer>
#include <QSize>
#include <QString>

#include <ZoinGallery/ImageSourceProvider.h>

namespace ZoinGallery {

// A source probe is deliberately independent from masonry/viewer geometry.
// Its result is reusable for every presentation target of one immutable
// source revision, including tiny embedded previews on slow filesystems.
struct ImageProbeRequest {
    ImageSourceDescriptor source;
    QString requestNamespace;
    bool highPriority = false;
};

struct ImageProbeResult {
    ImageProbeRequest request;
    ImageSourceProbeStatus status = ImageSourceProbeStatus::Unsupported;
    QImage preview;
    QSize sourceSize;
    int orientation = 1;
    qint64 sourceBytesRead = 0;
    int rangeRequests = 0;
    QString diagnostic;

    bool found() const {
        return status == ImageSourceProbeStatus::Found && !preview.isNull();
    }
};

} // namespace ZoinGallery

#endif // IMAGEPROBE_H
