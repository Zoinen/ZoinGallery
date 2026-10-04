#include "ViewerVideoFrameSource.h"
#include "VideoFrameGeometry.h"

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK

#include <QMutexLocker>

namespace ZoinGallery {

ViewerVideoFrameSource::ViewerVideoFrameSource(QObject *parent)
    : QObject(parent), m_sink(new QVideoSink(this))
{
    connect(m_sink, &QVideoSink::videoFrameChanged, this,
            &ViewerVideoFrameSource::acceptFrame);
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

void ViewerVideoFrameSource::acceptFrame(const QVideoFrame &frame)
{
    {
        QMutexLocker lock(&m_mutex);
        m_frame = frame;
        ++m_revision;
    }
    emit frameChanged();
}

} // namespace ZoinGallery

#endif // ZOIN_ENABLE_VIDEO_PLAYBACK
