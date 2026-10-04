#ifndef ZOINGALLERY_VIDEOPLAYBACKCONTROLLER_H
#define ZOINGALLERY_VIDEOPLAYBACKCONTROLLER_H

#include <QObject>
#include <QMutex>
#include <QMetaType>
#include <QPointer>
#include <QSharedPointer>
#include <QThread>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVariantMap>

#include <atomic>

namespace ZoinGallery {

class ImageSourceProvider;
class ImageSourceCancellation;
class VideoPlaybackWorker;

class VideoPlaybackController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(qint64 position READ position NOTIFY changed)
    Q_PROPERTY(qint64 duration READ duration NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(qreal volume READ volume NOTIFY changed)

public:
    explicit VideoPlaybackController(
        QSharedPointer<ImageSourceProvider> provider,
        QObject *parent = nullptr);
    ~VideoPlaybackController() override;

    bool available() const { return m_available; }
    QString state() const { return m_state; }
    QString error() const { return m_error; }
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }
    bool playing() const { return m_playing; }
    bool muted() const { return m_muted; }
    qreal volume() const { return m_volume; }

    Q_INVOKABLE void openSource(const QVariantMap &source,
                                const QString &startMode);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void seekBy(qint64 deltaMs);
    Q_INVOKABLE void seekTo(qint64 positionMs);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void adjustVolume(qreal delta);
    Q_INVOKABLE void setPresentationVisible(bool visible);
    Q_INVOKABLE void setOutputSink(QObject *sink);

signals:
    void availableChanged();
    void changed();

private slots:
    void drainLatestFrame();
    void acceptAvailability(bool available);
    void acceptSnapshot(qulonglong generation, const QString &state,
                        const QString &error, qint64 position,
                        qint64 duration, bool playing, bool muted,
                        qreal volume);

private:
    friend class VideoPlaybackWorker;

    void invalidateSource(bool clearFrame);
    void postWorkerStop();

    struct FrameMailbox {
        QMutex mutex;
        QVideoFrame frame;
        std::atomic<qulonglong> generation{0};
        bool deliveryQueued = false;
    };

    bool m_available = false;
    bool m_backendReady = false;
    bool m_presentationVisible = true;
    QPointer<QVideoSink> m_videoSink;
    QPointer<QThread> m_workerThread;
    QPointer<VideoPlaybackWorker> m_worker;
    QSharedPointer<ImageSourceProvider> m_provider;
    QSharedPointer<ImageSourceCancellation> m_cancellation;
    QSharedPointer<FrameMailbox> m_frameMailbox;
    std::atomic<qulonglong> m_generation{0};
    QString m_sourceIdentity;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_playing = false;
    bool m_muted = true;
    bool m_muteOverridden = false;
    qreal m_volume = 1.0;
};

} // namespace ZoinGallery

Q_DECLARE_METATYPE(QSharedPointer<ZoinGallery::ImageSourceCancellation>)

#endif // ZOINGALLERY_VIDEOPLAYBACKCONTROLLER_H
