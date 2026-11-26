#pragma once

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <QtCore/private/qcore_mac_p.h>
#include <QVideoFrame>
#include <optional>

struct AVFrame;

namespace ZoinGallery {

class MacVideoSample {
public:
    struct Upcoming {
        QVideoFrame frame;
        CMTime hostTime;
    };
    QCFType<CMSampleBufferRef> make(const QVideoFrame &frame, CMTime hostTime = kCMTimeInvalid);
    std::optional<Upcoming> upcoming(const QVideoFrame &current);
    bool sameDecoderFrame(const QVideoFrame &a, const QVideoFrame &b);

private:
    const AVFrame *decodedFrame(const QVideoFrame &frame);
    QCFType<CVPixelBufferRef> copyPixels(QVideoFrame frame, OSType type);
    std::optional<const AVFrame *(*)(const QVideoFrame *)> m_frameAccessor;
    std::optional<bool (*)(const QVideoFrame *, QVideoFrame *, qint64 *)> m_upcomingAccessor;
    QCFType<CVPixelBufferPoolRef> m_pool;
    QSize m_size;
    OSType m_type = 0;
};

} // namespace ZoinGallery
