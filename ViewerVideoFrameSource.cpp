#include "ViewerVideoFrameSource.h"
#include "VideoFrameGeometry.h"
#include "VideoColorSpace.h"

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK

#include <QMutexLocker>

namespace ZoinGallery {

#ifdef Q_OS_MACOS
class NativeVideoSink final : public QVideoSink {
    Q_OBJECT
    Q_CLASSINFO("F4TimedVideoPresentation", "1")
public:
    using QVideoSink::QVideoSink;
};
#endif

QVideoSink *ViewerVideoFrameSource::createPlaybackSink(QObject *parent)
{
#ifdef Q_OS_MACOS
    return new NativeVideoSink(parent);
#else
    return new QVideoSink(parent);
#endif
}

ViewerVideoFrameSource::ViewerVideoFrameSource(QObject *parent)
    : QObject(parent)
{
}

QObject *ViewerVideoFrameSource::sink()
{
    if (!m_sink) {
        m_sink = new QVideoSink(this);
        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) { acceptFrame(frame); });
    }
    return m_sink;
}

bool ViewerVideoFrameSource::hasFrame() const
{
    QMutexLocker lock(&m_mutex);
    return m_frame.isValid();
}

QSize ViewerVideoFrameSource::frameSize() const
{
    QMutexLocker lock(&m_mutex);
    if (!m_frame.isValid())
        return {};
    return VideoFrameGeometry::displaySize(m_frame);
}

quint64 ViewerVideoFrameSource::revision() const
{
    QMutexLocker lock(&m_mutex);
    return m_revision;
}

ViewerVideoFrameSource::Snapshot ViewerVideoFrameSource::snapshot() const
{
    QMutexLocker lock(&m_mutex);
    return {m_frame, m_revision, m_allowUpcoming};
}

bool ViewerVideoFrameSource::edrEligible() const
{
    const auto frame = snapshot().frame;
    const auto format = frame.surfaceFormat();
    if (!frame.isValid() || !VideoColorSpace::isHdr(format))
        return false;
    using Format = QVideoFrameFormat;
    return format.pixelFormat() == Format::Format_P010
        || format.pixelFormat() == Format::Format_P016
        || format.pixelFormat() == Format::Format_YUV420P10;
}

bool ViewerVideoFrameSource::hdr() const
{
    QMutexLocker lock(&m_mutex);
    return m_frame.isValid() && VideoColorSpace::isHdr(m_frame.surfaceFormat());
}

void ViewerVideoFrameSource::cancelUpcoming()
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_allowUpcoming) return;
        m_allowUpcoming = false;
        ++m_revision;
    }
    emit frameChanged();
}

void ViewerVideoFrameSource::acceptFrame(const QVideoFrame &frame, bool allowUpcoming)
{
    bool changedHdr = false;
    {
        QMutexLocker lock(&m_mutex);
        const bool hdr = frame.isValid() && VideoColorSpace::isHdr(frame.surfaceFormat());
        changedHdr = hdr != (m_frame.isValid() && VideoColorSpace::isHdr(m_frame.surfaceFormat()));
        m_frame = frame;
        m_allowUpcoming = allowUpcoming && hdr;
        ++m_revision;
    }
    emit frameChanged();
    if (changedHdr) emit hdrChanged();
}

} // namespace ZoinGallery

#ifdef Q_OS_MACOS
#include "ViewerVideoFrameSource.moc"
#endif

#endif // ZOIN_ENABLE_VIDEO_PLAYBACK
