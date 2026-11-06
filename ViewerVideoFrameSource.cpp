#include "ViewerVideoFrameSource.h"
#include "VideoFrameGeometry.h"
#include "VideoColorSpace.h"

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK

#include <QMutexLocker>

namespace ZoinGallery {

ViewerVideoFrameSource::ViewerVideoFrameSource(QObject *parent)
    : QObject(parent)
{
}

QObject *ViewerVideoFrameSource::sink()
{
    if (!m_sink) {
        m_sink = new QVideoSink(this);
        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                &ViewerVideoFrameSource::acceptFrame);
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
    return {m_frame, m_revision};
}

bool ViewerVideoFrameSource::hdr() const
{
    QMutexLocker lock(&m_mutex);
    return m_frame.isValid() && VideoColorSpace::isHdr(m_frame.surfaceFormat());
}

void ViewerVideoFrameSource::acceptFrame(const QVideoFrame &frame)
{
    bool changedHdr = false;
    {
        QMutexLocker lock(&m_mutex);
        changedHdr = (frame.isValid() && VideoColorSpace::isHdr(frame.surfaceFormat()))
            != (m_frame.isValid() && VideoColorSpace::isHdr(m_frame.surfaceFormat()));
        m_frame = frame;
        ++m_revision;
    }
    emit frameChanged();
    if (changedHdr) emit hdrChanged();
}

} // namespace ZoinGallery

#endif // ZOIN_ENABLE_VIDEO_PLAYBACK
