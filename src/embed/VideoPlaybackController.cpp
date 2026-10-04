#include "VideoPlaybackController.h"

#include "ImageSourceRangeDevice.h"

#include <ZoinGallery/ImageSourceProvider.h>
#include <ZoinGallery/MediaTimingTrace.h>

#include <QAudioOutput>
#include <QAudioDevice>
#include <QFile>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMetaObject>
#include <QMutexLocker>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <utility>

namespace ZoinGallery {

class VideoPlaybackWorker final : public QObject {
    Q_OBJECT

public:
    VideoPlaybackWorker(QSharedPointer<ImageSourceProvider> provider,
                        QSharedPointer<VideoPlaybackController::FrameMailbox> mailbox)
        : m_provider(std::move(provider)), m_mailbox(std::move(mailbox)) {}

public slots:
    void initialize() {
        createPlayer();
    }

    void createPlayer() {
        if (m_player || m_shutdown) {
            return;
        }
        // Give each media backend a fresh output object. Some platform output
        // backends keep player-specific stream state inside QAudioOutput;
        // reattaching one object to a replacement QMediaPlayer can leave the
        // new player decoding audio buffers without sending them to the device.
        m_audio = new QAudioOutput(this);
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setAudioOutput(m_audio);
        m_player->setVideoSink(m_sink);
        emit availabilityReady(m_player->isAvailable());
        connect(m_player, &QMediaPlayer::positionChanged, this,
                [this](qint64) { publishSnapshot(); });
        connect(m_player, &QMediaPlayer::durationChanged, this,
                [this](qint64) { publishSnapshot(); });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this,
                [this](QMediaPlayer::PlaybackState state) {
            m_playing = state == QMediaPlayer::PlayingState;
            if (m_audio) {
                MediaTimingTrace::event(
                    QStringLiteral("qt.gallery.video.audio_playback_state"),
                    {{QStringLiteral("generation"), m_generation},
                     {QStringLiteral("playing"), m_playing},
                     {QStringLiteral("muted"), m_audio->isMuted()},
                     {QStringLiteral("volume"), m_audio->volume()},
                     {QStringLiteral("device"),
                      m_audio->device().description()},
                     {QStringLiteral("hasAudio"), m_player->hasAudio()},
                     {QStringLiteral("activeAudioTrack"),
                      m_player->activeAudioTrack()}});
            }
            publishSnapshot();
        });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
                [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                m_state = QStringLiteral("ended");
                m_resumeWhenVisible = false;
                publishSnapshot();
            } else if (status == QMediaPlayer::LoadedMedia
                       && m_state == QStringLiteral("loading")) {
                m_state = QStringLiteral("ready");
                publishSnapshot();
            } else if (status == QMediaPlayer::InvalidMedia
                       && !m_error.isEmpty()) {
                m_state = QStringLiteral("failed");
                publishSnapshot();
            }
        });
        connect(m_player, &QMediaPlayer::tracksChanged, this, [this]() {
            MediaTimingTrace::event(
                QStringLiteral("qt.gallery.video.audio_tracks"),
                {{QStringLiteral("generation"), m_generation},
                 {QStringLiteral("hasAudio"), m_player->hasAudio()},
                 {QStringLiteral("audioTrackCount"),
                  m_player->audioTracks().size()},
                 {QStringLiteral("activeAudioTrack"),
                  m_player->activeAudioTrack()}});
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error, const QString &message) {
            fail(message.isEmpty() ? tr("The video could not be decoded.")
                                   : message);
        });
        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) { publishFrame(frame); });
    }

    void openSource(qulonglong generation, const QVariantMap &source,
                    const QString &startMode, bool presentationVisible,
                    const QSharedPointer<ImageSourceCancellation> &cancellation) {
        if (m_shutdown) {
            return;
        }
        releaseCurrentSource();
        createPlayer();
        if (!m_player) {
            return;
        }
        m_generation = generation;
        m_cancellation = cancellation;
        m_state = QStringLiteral("loading");
        m_error.clear();
        m_position = 0;
        m_duration = 0;
        m_playing = false;
        m_firstFrame = false;
        m_visible = presentationVisible;
        m_autoStart = startMode == QStringLiteral("autoplay-muted")
            || startMode == QStringLiteral("autoplay-sound");
        if (!m_hasOpenedSource) {
            m_muted = startMode != QStringLiteral("autoplay-sound");
        }
        m_hasOpenedSource = true;
        m_volume = std::clamp(m_volume, qreal(0), qreal(1));
        m_audio->setMuted(m_muted);
        m_audio->setVolume(m_volume);
        publishSnapshot();

        const ImageSourceDescriptor descriptor = descriptorFromMap(source);
        const bool external = source.value(QStringLiteral("sourceKind")).toString()
            == QStringLiteral("external");
        QString sourcePath = source.value(QStringLiteral("path")).toString();
        if (external && descriptor.isValid()) {
            if (imageSourceSupportsRangeReads(descriptor, m_provider)) {
                m_rangeDevice = std::make_unique<ImageSourceRangeDevice>(
                    m_provider, descriptor, m_cancellation);
                m_sourceDevice = m_rangeDevice.get();
                m_sourceUrl = imageSourceUrlHint(descriptor);
                traceSourceMode(QStringLiteral("range"));
            } else {
                m_lease = m_provider
                    ? m_provider->materialize(descriptor, m_cancellation)
                    : QSharedPointer<ImageSourceLease>();
                if (!m_lease) {
                    fail(m_cancellation && m_cancellation->isCanceled()
                        ? tr("Video loading was cancelled.")
                        : tr("Could not access the video source."));
                    return;
                }
                sourcePath = m_lease->localPath();
                traceSourceMode(QStringLiteral("materialized"));
            }
        } else {
            traceSourceMode(QStringLiteral("local"));
        }

        if (m_cancellation && m_cancellation->isCanceled()) {
            fail(tr("Video loading was cancelled."));
            return;
        }
        if (!m_sourceDevice) {
            auto file = std::make_unique<QFile>(sourcePath);
            if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)
                || !file->open(QIODevice::ReadOnly)) {
                fail(file->errorString().isEmpty()
                    ? tr("The video file is unavailable.")
                    : file->errorString());
                return;
            }
            m_fileDevice = std::move(file);
            m_sourceDevice = m_fileDevice.get();
            m_sourceUrl = QUrl::fromLocalFile(
                QFileInfo(sourcePath).absoluteFilePath());
        }

        MediaTimingTrace::event(QStringLiteral("qt.gallery.video.open"),
            sourceTraceFields(source, {{QStringLiteral("generation"), generation},
                                       {QStringLiteral("mode"), startMode}}));
        m_player->setSourceDevice(m_sourceDevice, m_sourceUrl);
        if (m_cancellation && m_cancellation->isCanceled()) {
            releaseCurrentSource();
            m_state = QStringLiteral("idle");
            publishSnapshot();
            return;
        }
        if (m_autoStart) {
            if (m_visible) {
                m_player->play();
            } else {
                m_resumeWhenVisible = true;
            }
        }
    }

    void stopSource(qulonglong generation) {
        m_generation = generation;
        releaseCurrentSource();
        m_state = QStringLiteral("idle");
        m_error.clear();
        m_position = 0;
        m_duration = 0;
        m_playing = false;
        m_resumeWhenVisible = false;
        m_hasOpenedSource = false;
        publishSnapshot();
    }

    void playPause() {
        if (!m_player || m_state == QStringLiteral("loading")
            || m_state == QStringLiteral("failed")) {
            return;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_resumeWhenVisible = false;
            m_player->pause();
        } else {
            if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) {
                m_player->setPosition(0);
            }
            if (m_visible) {
                m_player->play();
            } else {
                m_resumeWhenVisible = true;
            }
        }
    }

    void seekBy(qint64 deltaMs) {
        if (!m_player || m_duration <= 0) {
            return;
        }
        m_player->setPosition(std::clamp(
            m_player->position() + deltaMs, qint64(0), m_duration));
    }

    void seekTo(qint64 positionMs) {
        if (!m_player || m_duration <= 0) {
            return;
        }
        m_player->setPosition(std::clamp(positionMs, qint64(0), m_duration));
    }

    void setMuted(bool muted) {
        m_muted = muted;
        if (m_audio) {
            m_audio->setMuted(muted);
        }
        publishSnapshot();
    }

    void setVolume(qreal volume) {
        m_volume = std::clamp(volume, qreal(0), qreal(1));
        if (m_audio) {
            m_audio->setVolume(m_volume);
        }
        publishSnapshot();
    }

    void setPresentationVisible(bool visible) {
        if (m_visible == visible) {
            return;
        }
        m_visible = visible;
        if (!visible) {
            m_resumeWhenVisible = m_player
                && m_player->playbackState() == QMediaPlayer::PlayingState;
            if (m_resumeWhenVisible) {
                m_player->pause();
            }
        } else if (m_resumeWhenVisible && m_player) {
            m_resumeWhenVisible = false;
            if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) {
                m_player->setPosition(0);
            }
            m_player->play();
        }
    }

    void shutdown() {
        m_shutdown = true;
        if (m_cancellation) {
            m_cancellation->cancel();
        }
        releaseCurrentSource();
        delete m_player;
        m_player = nullptr;
        delete m_audio;
        m_audio = nullptr;
        delete m_sink;
        m_sink = nullptr;
        emit shutdownFinished();
    }

signals:
    void availabilityReady(bool available);
    void snapshotReady(qulonglong generation, const QString &state,
                       const QString &error, qint64 position,
                       qint64 duration, bool playing, bool muted,
                       qreal volume);
    void frameAvailable();
    void shutdownFinished();

private:
    static ImageSourceDescriptor descriptorFromMap(const QVariantMap &map) {
        ImageSourceDescriptor descriptor;
        descriptor.resourceId = map.value(QStringLiteral("resourceId")).toString();
        descriptor.sourceKey = map.value(QStringLiteral("sourceKey")).toString();
        descriptor.contentVersion = map.value(QStringLiteral("contentVersion")).toString();
        descriptor.versionStrength = map.value(QStringLiteral("versionStrength")).toString();
        descriptor.storageClass = map.value(QStringLiteral("storageClass")).toString();
        descriptor.accessProfile = map.value(QStringLiteral("accessProfile")).toString();
        descriptor.displayName = map.value(QStringLiteral("displayName")).toString();
        descriptor.mimeType = map.value(QStringLiteral("mimeType")).toString();
        descriptor.size = map.value(QStringLiteral("size")).toLongLong();
        descriptor.catalogGeneration = map.value(QStringLiteral("catalogGeneration")).toULongLong();
        return descriptor;
    }

    static QVariantMap sourceTraceFields(const QVariantMap &source,
                                         QVariantMap extra = {}) {
        ImageSourceDescriptor descriptor = descriptorFromMap(source);
        if (descriptor.isValid()) {
            extra = MediaTimingTrace::mergedFields(
                MediaTimingTrace::sourceFields(descriptor), extra);
        }
        extra.insert(QStringLiteral("entryId"),
                     source.value(QStringLiteral("entryId")));
        extra.insert(QStringLiteral("catalogRevision"),
                     source.value(QStringLiteral("catalogRevision")));
        return extra;
    }

    void releaseCurrentSource() {
        if (m_player) {
            m_player->stop();
            // setSource() is asynchronous. Keep every source device and lease
            // alive until the player/backend has been destroyed on this
            // worker thread; otherwise a decoder can still read freed memory
            // after a rapid source switch or viewer close.
            delete m_player;
            m_player = nullptr;
        }
        if (m_sink) {
            delete m_sink;
            m_sink = nullptr;
        }
        if (m_audio) {
            delete m_audio;
            m_audio = nullptr;
        }
        m_sourceDevice = nullptr;
        m_rangeDevice.reset();
        m_fileDevice.reset();
        m_lease.clear();
        m_resumeWhenVisible = false;
        m_sourceUrl = QUrl();
    }

    void traceSourceMode(const QString &mode) {
        MediaTimingTrace::event(QStringLiteral("qt.gallery.video.source"),
            {{QStringLiteral("sourceMode"), mode},
             {QStringLiteral("generation"), m_generation}});
    }

    void fail(const QString &message) {
        if (m_cancellation && m_cancellation->isCanceled()) {
            return;
        }
        m_error = message;
        m_state = QStringLiteral("failed");
        m_playing = false;
        m_resumeWhenVisible = false;
        if (m_player) {
            m_player->stop();
        }
        MediaTimingTrace::event(QStringLiteral("qt.gallery.video.error"),
            {{QStringLiteral("generation"), m_generation},
             {QStringLiteral("error"), message}});
        publishSnapshot();
    }

    void publishSnapshot() {
        if (!m_player) {
            return;
        }
        m_position = m_player->position();
        m_duration = m_player->duration();
        m_playing = m_player->playbackState() == QMediaPlayer::PlayingState;
        emit snapshotReady(m_generation, m_state, m_error, m_position,
                           m_duration, m_playing, m_muted, m_volume);
    }

    void publishFrame(const QVideoFrame &frame) {
        if (!frame.isValid() || !m_mailbox
            || m_mailbox->generation.load(std::memory_order_acquire)
                != m_generation) {
            return;
        }
        bool needsDelivery = false;
        {
            QMutexLocker lock(&m_mailbox->mutex);
            if (m_mailbox->generation.load(std::memory_order_relaxed)
                != m_generation) {
                return;
            }
            m_mailbox->frame = frame;
            if (!m_mailbox->deliveryQueued) {
                m_mailbox->deliveryQueued = true;
                needsDelivery = true;
            }
        }
        if (needsDelivery) {
            if (!m_firstFrame) {
                MediaTimingTrace::event(
                    QStringLiteral("qt.gallery.video.first_frame"),
                    {{QStringLiteral("generation"), m_generation},
                     {QStringLiteral("width"), frame.size().width()},
                     {QStringLiteral("height"), frame.size().height()},
                     {QStringLiteral("rotation"), int(frame.rotation())}});
            }
            m_firstFrame = true;
            emit frameAvailable();
        }
    }

    QSharedPointer<ImageSourceProvider> m_provider;
    QSharedPointer<VideoPlaybackController::FrameMailbox> m_mailbox;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoSink *m_sink = nullptr;
    std::unique_ptr<ImageSourceRangeDevice> m_rangeDevice;
    std::unique_ptr<QFile> m_fileDevice;
    QIODevice *m_sourceDevice = nullptr;
    QSharedPointer<ImageSourceLease> m_lease;
    QSharedPointer<ImageSourceCancellation> m_cancellation;
    QUrl m_sourceUrl;
    qulonglong m_generation = 0;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    qreal m_volume = 1.0;
    bool m_playing = false;
    bool m_muted = true;
    bool m_visible = true;
    bool m_autoStart = false;
    bool m_resumeWhenVisible = false;
    bool m_firstFrame = false;
    bool m_hasOpenedSource = false;
    bool m_shutdown = false;
};

VideoPlaybackController::VideoPlaybackController(
    QSharedPointer<ImageSourceProvider> provider, QObject *parent)
    : QObject(parent), m_provider(std::move(provider)),
      m_frameMailbox(QSharedPointer<FrameMailbox>::create()) {
    qRegisterMetaType<QSharedPointer<ImageSourceCancellation>>();
    auto *thread = new QThread;
    auto *worker = new VideoPlaybackWorker(m_provider, m_frameMailbox);
    worker->moveToThread(thread);
    m_workerThread = thread;
    m_worker = worker;
    connect(thread, &QThread::started, worker,
            &VideoPlaybackWorker::initialize);
    connect(worker, &VideoPlaybackWorker::snapshotReady, this,
            &VideoPlaybackController::acceptSnapshot, Qt::QueuedConnection);
    connect(worker, &VideoPlaybackWorker::availabilityReady, this,
            &VideoPlaybackController::acceptAvailability,
            Qt::QueuedConnection);
    connect(worker, &VideoPlaybackWorker::frameAvailable, this,
            &VideoPlaybackController::drainLatestFrame, Qt::QueuedConnection);
    connect(worker, &VideoPlaybackWorker::shutdownFinished,
            thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

VideoPlaybackController::~VideoPlaybackController() {
    invalidateSource(true);
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::QueuedConnection);
    } else if (m_workerThread) {
        m_workerThread->quit();
    }
}

void VideoPlaybackController::openSource(const QVariantMap &source,
                                         const QString &startMode) {
    if (!m_available || !m_worker || source.isEmpty()) {
        stop();
        return;
    }
    const QString identity = source.value(QStringLiteral("identity")).toString();
    if (!identity.isEmpty() && identity == m_sourceIdentity) {
        return;
    }
    invalidateSource(true);
    m_sourceIdentity = identity;
    m_cancellation = QSharedPointer<ImageSourceCancellation>::create();
    const qulonglong generation = m_generation.load(std::memory_order_acquire);
    m_frameMailbox->generation.store(generation, std::memory_order_release);
    m_state = QStringLiteral("loading");
    m_error.clear();
    m_position = 0;
    m_duration = 0;
    m_playing = false;
    if (!m_muteOverridden) {
        m_muted = startMode != QStringLiteral("autoplay-sound");
    }
    emit changed();
    QMetaObject::invokeMethod(m_worker, "openSource", Qt::QueuedConnection,
        Q_ARG(qulonglong, generation), Q_ARG(QVariantMap, source),
        Q_ARG(QString, startMode), Q_ARG(bool, m_presentationVisible),
        Q_ARG(QSharedPointer<ZoinGallery::ImageSourceCancellation>, m_cancellation));
    MediaTimingTrace::event(QStringLiteral("qt.gallery.video.request"),
        {{QStringLiteral("generation"), generation},
         {QStringLiteral("identity"), identity},
         {QStringLiteral("mode"), startMode}});
}

void VideoPlaybackController::stop() {
    invalidateSource(true);
    m_sourceIdentity.clear();
    m_state = QStringLiteral("idle");
    m_error.clear();
    m_position = 0;
    m_duration = 0;
    m_playing = false;
    m_muteOverridden = false;
    emit changed();
    postWorkerStop();
}

void VideoPlaybackController::playPause() {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "playPause", Qt::QueuedConnection);
    }
}

void VideoPlaybackController::seekBy(qint64 deltaMs) {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "seekBy", Qt::QueuedConnection,
                                  Q_ARG(qint64, deltaMs));
    }
}

void VideoPlaybackController::seekTo(qint64 positionMs) {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "seekTo", Qt::QueuedConnection,
                                  Q_ARG(qint64, positionMs));
    }
}

void VideoPlaybackController::toggleMute() {
    m_muted = !m_muted;
    m_muteOverridden = true;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setMuted", Qt::QueuedConnection,
                                  Q_ARG(bool, m_muted));
    }
    emit changed();
}

void VideoPlaybackController::adjustVolume(qreal delta) {
    m_volume = std::clamp(m_volume + delta, qreal(0), qreal(1));
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setVolume", Qt::QueuedConnection,
                                  Q_ARG(qreal, m_volume));
    }
    emit changed();
}

void VideoPlaybackController::setPresentationVisible(bool visible) {
    if (m_presentationVisible == visible) {
        return;
    }
    m_presentationVisible = visible;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setPresentationVisible",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, visible));
    }
}

void VideoPlaybackController::setOutputSink(QObject *sinkObject) {
    auto *sink = qobject_cast<QVideoSink *>(sinkObject);
    if (m_videoSink == sink) {
        return;
    }
    if (m_videoSink) {
        m_videoSink->setVideoFrame(QVideoFrame());
    }
    m_videoSink = sink;
    drainLatestFrame();
}

void VideoPlaybackController::drainLatestFrame() {
    if (!m_frameMailbox) {
        return;
    }
    QVideoFrame frame;
    qulonglong generation = 0;
    {
        QMutexLocker lock(&m_frameMailbox->mutex);
        frame = m_frameMailbox->frame;
        generation = m_frameMailbox->generation.load(std::memory_order_relaxed);
        m_frameMailbox->deliveryQueued = false;
    }
    if (m_videoSink
        && generation == m_generation.load(std::memory_order_acquire)) {
        m_videoSink->setVideoFrame(frame);
    }
}

void VideoPlaybackController::acceptAvailability(bool available) {
    const bool valueChanged = m_available != available;
    const bool firstResult = !m_backendReady;
    if (!valueChanged && !firstResult) {
        return;
    }
    m_backendReady = true;
    m_available = available;
    if (!m_available) {
        m_state = QStringLiteral("unavailable");
    } else if (m_state == QStringLiteral("unavailable")) {
        m_state = QStringLiteral("idle");
    }
    if (valueChanged) {
        emit availableChanged();
    }
    emit changed();
}

void VideoPlaybackController::acceptSnapshot(
    qulonglong generation, const QString &state, const QString &error,
    qint64 position, qint64 duration, bool playing, bool muted,
    qreal volume) {
    if (generation != m_generation.load(std::memory_order_acquire)) {
        return;
    }
    m_state = state;
    m_error = error;
    m_position = position;
    m_duration = duration;
    m_playing = playing;
    m_muted = muted;
    m_volume = volume;
    emit changed();
}

void VideoPlaybackController::invalidateSource(bool clearFrame) {
    const qulonglong previousGeneration =
        m_generation.load(std::memory_order_acquire);
    const qulonglong generation =
        m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (m_cancellation) {
        MediaTimingTrace::event(QStringLiteral("qt.gallery.video.cancel"),
            {{QStringLiteral("generation"), previousGeneration},
             {QStringLiteral("identity"), m_sourceIdentity}});
        m_cancellation->cancel();
        m_cancellation.clear();
    }
    if (m_frameMailbox) {
        m_frameMailbox->generation.store(generation, std::memory_order_release);
        QMutexLocker lock(&m_frameMailbox->mutex);
        m_frameMailbox->frame = QVideoFrame();
    }
    if (clearFrame && m_videoSink) {
        m_videoSink->setVideoFrame(QVideoFrame());
    }
}

void VideoPlaybackController::postWorkerStop() {
    if (m_worker) {
        const qulonglong generation = m_generation.load(std::memory_order_acquire);
        QMetaObject::invokeMethod(m_worker, "stopSource", Qt::QueuedConnection,
                                  Q_ARG(qulonglong, generation));
    }
}

} // namespace ZoinGallery

#include "VideoPlaybackController.moc"
