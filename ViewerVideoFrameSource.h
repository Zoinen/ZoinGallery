#pragma once

#ifdef ZOIN_ENABLE_VIDEO_PLAYBACK

#include <QObject>
#include <QMutex>
#include <QSize>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtQmlIntegration/qqmlintegration.h>

namespace ZoinGallery {

// Delivers the newest decoded frame to the scene graph without converting it
// to QImage. The renderer owns the RHI texture pool; this object only protects
// the short-lived QVideoFrame snapshot shared with the render thread.
class ViewerVideoFrameSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *sink READ sink CONSTANT)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY frameChanged)

public:
    struct Snapshot {
        QVideoFrame frame;
        quint64 revision = 0;
    };

    explicit ViewerVideoFrameSource(QObject *parent = nullptr);

    QObject *sink() const { return m_sink; }
    bool hasFrame() const;
    QSize frameSize() const;
    quint64 revision() const;
    Snapshot snapshot() const;

signals:
    void frameChanged();

private:
    void acceptFrame(const QVideoFrame &frame);

    QVideoSink *m_sink = nullptr;
    mutable QMutex m_mutex;
    QVideoFrame m_frame;
    quint64 m_revision = 0;
};

} // namespace ZoinGallery

#endif // ZOIN_ENABLE_VIDEO_PLAYBACK
