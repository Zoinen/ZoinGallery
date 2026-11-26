#include "VideoPlaybackController.h"

#include "ImageSourceRangeDevice.h"
#include "ViewerVideoFrameSource.h"

#include <ZoinGallery/ImageSourceProvider.h>
#include <ZoinGallery/MediaTimingTrace.h>

#include <QAudioOutput>
#include <QAudioDevice>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <utility>

namespace ZoinGallery {

// Catalog IDs and materialized temporary paths can change on every open.
static QString fileSettingsGroup(const QVariantMap &source) {
    QString identity;
    if (source.value(QStringLiteral("sourceKind")) == QStringLiteral("external")) {
        const auto key = source.value(QStringLiteral("sourceKey")).toString();
        if (!key.isEmpty())
            identity = QStringLiteral("provider:") + key;
    } else {
        const auto path = source.value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            identity = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
    }
    if (identity.isEmpty())
        return {};
    const auto digest = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("Video/Files/") + QString::fromLatin1(digest.toHex());
}

class VideoPlaybackWorker final : public QObject {
    Q_OBJECT

public:
    VideoPlaybackWorker(QSharedPointer<ImageSourceProvider> provider,
                        QSharedPointer<VideoPlaybackController::FrameMailbox> mailbox)
        : m_provider(std::move(provider)), m_mailbox(std::move(mailbox)) {}

    void acknowledgeClock(quint64 revision) { m_clockRevision = revision; }

public slots:
    void initialize() {
        createPlayer();
    }

    void createPlayer() {
        if (m_player || m_shutdown) {
            return;
        }
        const qint64 started = MediaTimingTrace::enabled()
            ? MediaTimingTrace::monotonicNanoseconds() : 0;
        // Give each media backend a fresh output object. Some platform output
        // backends keep player-specific stream state inside QAudioOutput;
        // reattaching one object to a replacement QMediaPlayer can leave the
        // new player decoding audio buffers without sending them to the device.
        m_audio = new QAudioOutput(this);
        m_player = new QMediaPlayer(this);
        m_player->setLoops(m_looping ? QMediaPlayer::Infinite : QMediaPlayer::Once);
        m_sink = ViewerVideoFrameSource::createPlaybackSink(this);
        m_player->setAudioOutput(m_audio);
        m_player->setVideoSink(m_sink);
        if (started) {
            MediaTimingTrace::event(QStringLiteral("qt.gallery.video.backend_created"),
                {{QStringLiteral("durationNs"), MediaTimingTrace::monotonicNanoseconds() - started}});
        }
        emit availabilityReady(m_player->isAvailable());
        connect(m_player, &QMediaPlayer::positionChanged, this,
                [this](qint64) { publishSnapshot(); });
        connect(m_player, &QMediaPlayer::durationChanged, this,
                [this](qint64) { publishSnapshot(); });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this,
                [this](QMediaPlayer::PlaybackState state) {
            m_playing = state == QMediaPlayer::PlayingState;
            if (m_playing && m_state == QStringLiteral("ended"))
                m_state = QStringLiteral("ready");
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
                restoreTrackSelections();
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
            publishTracks();
        });
        connect(m_player, &QMediaPlayer::activeTracksChanged, this,
                &VideoPlaybackWorker::publishTracks);
        connect(m_player, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error, const QString &message) {
            fail(message.isEmpty() ? tr("The video could not be decoded.")
                                   : message);
        });
        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) { publishFrame(frame); });
        connect(m_sink, &QVideoSink::subtitleTextChanged, this, [this](const QString &text) {
            if (m_player && m_player->activeSubtitleTrack() >= 0)
                emit subtitleReady(m_generation, text);
        });
    }

    void openSource(qulonglong generation, const QVariantMap &source,
                    const QString &startMode, bool presentationVisible,
                    bool muted, qreal volume,
                    const QSharedPointer<ImageSourceCancellation> &cancellation) {
        if (m_shutdown || (cancellation && cancellation->isCanceled())) {
            return;
        }
        if (m_sourceDevice || m_lease) {
            releaseCurrentSource();
        }
        createPlayer();
        if (!m_player) {
            return;
        }
        m_generation = generation;
        m_fileSettingsGroup = fileSettingsGroup(source);
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
        m_audio->setMuted(muted);
        m_audio->setVolume(volume);
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
                        ? tr("Video loading was canceled.")
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
            fail(tr("Video loading was canceled."));
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
                                       {QStringLiteral("mode"), startMode},
                                       {QStringLiteral("muted"), muted}}));
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
        if (m_sourceDevice || m_lease) {
            releaseCurrentSource();
        }
        m_generation = generation;
        m_state = QStringLiteral("idle");
        m_error.clear();
        m_position = 0;
        m_duration = 0;
        m_playing = false;
        m_resumeWhenVisible = false;
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

    void resume(qulonglong generation, qint64 position) {
        if (!m_player || generation != m_generation)
            return;
        seekTo(position);
        m_player->play();
    }

    void seekBy(qint64 deltaMs) {
        if (!m_player || m_duration <= 0) {
            return;
        }
        seekTo(m_player->position() + deltaMs);
    }

    void seekTo(qint64 positionMs) {
        if (!m_player || m_duration <= 0) {
            return;
        }
        const qint64 position = std::clamp(positionMs, qint64(0), m_duration);
        if (position != m_player->position())
            m_sink->setSubtitleText({});
        m_player->setPosition(position);
    }

    void selectAudioTrack(qulonglong generation, int index) {
        if (generation == m_generation && m_player
            && index >= 0 && index < m_player->audioTracks().size()) {
            m_player->setActiveAudioTrack(index);
            // The FFmpeg backend updates the active index synchronously
            // without forwarding activeTracksChanged for a manual selection.
            publishTracks();
        }
    }

    void selectSubtitleTrack(qulonglong generation, int index) {
        if (generation != m_generation || !m_player || index < -1
            || index >= m_player->subtitleTracks().size()
            || index == m_player->activeSubtitleTrack())
            return;
        m_sink->setSubtitleText({});
        m_player->setActiveSubtitleTrack(index);
        publishTracks();
        if (m_player->activeSubtitleTrack() >= 0)
            emit subtitleReady(m_generation, m_sink->subtitleText());
    }

    void setLooping(bool looping) {
        m_looping = looping;
        if (!m_player) {
            return;
        }
        const bool ended = m_player->mediaStatus() == QMediaPlayer::EndOfMedia;
        m_player->setLoops(looping ? QMediaPlayer::Infinite : QMediaPlayer::Once);
        if (m_duration > 0) {
            // A short clip can already be fully demuxed while it is still
            // playing. Re-arm the decoder at the current position when the
            // setting changes; subsequent boundaries use the native loop queue.
            // This also drops prefetched repeats when looping is disabled.
            m_player->setPosition(ended && looping ? 0 : m_player->position());
        }
        if (looping && ended) {
            m_state = QStringLiteral("ready");
            if (m_visible) {
                m_player->play();
            } else {
                m_resumeWhenVisible = true;
            }
        }
        publishSnapshot();
    }

    void setMuted(bool muted) {
        if (m_audio) {
            m_audio->setMuted(muted);
        }
    }

    void setVolume(qreal volume) {
        if (m_audio) {
            m_audio->setVolume(volume);
        }
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
                       qint64 duration, bool playing);
    void frameAvailable();
    void tracksReady(qulonglong generation, const QVariantList &audio,
                     const QVariantList &subtitles, int activeAudio, int activeSubtitle,
                     const QString &audioLanguage, const QString &subtitleLanguage);
    void subtitleReady(qulonglong generation, const QString &text);
    void shutdownFinished();

private:
    void restoreTrackSelections() {
        if (m_fileSettingsGroup.isEmpty())
            return;
        QSettings settings;
        settings.beginGroup(m_fileSettingsGroup);
        bool valid = false;
        const int audio = settings.value(QStringLiteral("audio")).toInt(&valid);
        if (valid && audio >= 0 && audio < m_player->audioTracks().size())
            m_player->setActiveAudioTrack(audio);
        const int subtitle = settings.value(QStringLiteral("subtitle")).toInt(&valid);
        if (valid && subtitle >= -1 && subtitle < m_player->subtitleTracks().size())
            m_player->setActiveSubtitleTrack(subtitle);
        publishTracks();
    }

    QVariantList trackLabels(const QList<QMediaMetaData> &tracks) const {
        QVariantList labels;
        labels.reserve(tracks.size());
        for (const auto &track : tracks) {
            const QString title = track.value(QMediaMetaData::Title).toString().trimmed();
            const auto languageCode = track.value(QMediaMetaData::Language).value<QLocale::Language>();
            const QString language = languageCode > QLocale::C
                ? QLocale::languageToString(languageCode) : QString();
            if (!title.isEmpty() && !language.isEmpty() && title.compare(language, Qt::CaseInsensitive) != 0)
                labels.append(tr("%1 (%2)").arg(title, language));
            else if (!title.isEmpty() || !language.isEmpty())
                labels.append(title.isEmpty() ? language : title);
            else
                labels.append(tr("Track %1").arg(labels.size() + 1));
        }
        // Some files have several completely unlabelled or identically named
        // tracks. Keep their individual stream indices visible in the menu.
        const QVariantList names = labels;
        for (qsizetype i = 0; i < names.size(); ++i) {
            if (names.count(names[i]) > 1)
                labels[i] = tr("%1 · Track %2").arg(names[i].toString()).arg(i + 1);
        }
        return labels;
    }

    void publishTracks() {
        if (!m_player)
            return;
        const auto audio = m_player->audioTracks();
        const auto subtitles = m_player->subtitleTracks();
        const int activeAudio = m_player->activeAudioTrack();
        const int activeSubtitle = m_player->activeSubtitleTrack();
        const auto languageCode = [](const QList<QMediaMetaData> &tracks, int index) -> QString {
            if (index < 0 || index >= tracks.size())
                return {};
            const auto lang = tracks[index].value(QMediaMetaData::Language).value<QLocale::Language>();
            return lang > QLocale::C ? QLocale::languageToCode(lang, QLocale::ISO639Part1).toUpper() : QString();
        };
        emit tracksReady(m_generation, trackLabels(audio), trackLabels(subtitles), activeAudio,
                         activeSubtitle, languageCode(audio, activeAudio), languageCode(subtitles, activeSubtitle));
    }

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
                           m_duration, m_playing);
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
#ifdef Q_OS_MACOS
            m_mailbox->clockRevision = m_clockRevision;
            m_mailbox->clockRunning = m_playing && m_visible;
#endif
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
    QString m_fileSettingsGroup;
    qulonglong m_generation = 0;
    quint64 m_clockRevision = 0;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_playing = false;
    bool m_looping = false;
    bool m_visible = true;
    bool m_autoStart = false;
    bool m_resumeWhenVisible = false;
    bool m_firstFrame = false;
    bool m_shutdown = false;
};

VideoPlaybackController::VideoPlaybackController(
    QSharedPointer<ImageSourceProvider> provider, QObject *parent,
    VideoMutePreference *mutePreference)
    : QObject(parent), m_provider(std::move(provider)),
      m_frameMailbox(QSharedPointer<FrameMailbox>::create()) {
    m_mutePreference = mutePreference ? mutePreference : new VideoMutePreference(this);
    m_muted = m_mutePreference->overrideValue().value_or(true);
    connect(m_mutePreference, &VideoMutePreference::overrideChanged,
            this, &VideoPlaybackController::applyMutePreference);
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
    connect(worker, &VideoPlaybackWorker::tracksReady, this,
            [this](qulonglong generation, const QVariantList &audio, const QVariantList &subtitles,
                   int activeAudio, int activeSubtitle, const QString &audioLanguage, const QString &subtitleLanguage) {
        if (generation != m_generation.load(std::memory_order_acquire)
            || m_state == QStringLiteral("failed")
            || (m_audioTracks == audio && m_subtitleTracks == subtitles
                && m_audioLanguage == audioLanguage && m_subtitleLanguage == subtitleLanguage
                && m_activeAudioTrack == activeAudio && m_activeSubtitleTrack == activeSubtitle))
            return;
        if (m_activeSubtitleTrack != activeSubtitle)
            updateSubtitleText({});
        m_audioLanguage = audioLanguage;
        m_subtitleLanguage = subtitleLanguage;
        m_audioTracks = audio;
        m_subtitleTracks = subtitles;
        m_activeAudioTrack = activeAudio;
        m_activeSubtitleTrack = activeSubtitle;
        emit tracksChanged();
    }, Qt::QueuedConnection);
    connect(worker, &VideoPlaybackWorker::subtitleReady, this,
            [this](qulonglong generation, const QString &text) {
        if (generation == m_generation.load(std::memory_order_acquire)
            && m_activeSubtitleTrack >= 0)
            updateSubtitleText(text);
    }, Qt::QueuedConnection);
    connect(worker, &VideoPlaybackWorker::shutdownFinished,
            thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &VideoPlaybackController::saveProgress);
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
    m_fileSettingsGroup = fileSettingsGroup(source);
    if (!m_fileSettingsGroup.isEmpty()) {
        QSettings settings;
        m_resumePosition = settings.value(m_fileSettingsGroup + QStringLiteral("/positionMs")).toLongLong();
        updateSubtitleOffsetPercent(settings.value(m_fileSettingsGroup + QStringLiteral("/subtitleOffsetPercent")).toInt(), false);
    }
    m_progressTimer.start();
    m_cancellation = QSharedPointer<ImageSourceCancellation>::create();
    const qulonglong generation = m_generation.load(std::memory_order_acquire);
    m_frameMailbox->generation.store(generation, std::memory_order_release);
    m_state = QStringLiteral("loading");
    m_error.clear();
    m_position = 0;
    m_duration = 0;
    m_playing = false;
    m_muted = m_mutePreference ? m_mutePreference->overrideValue().value_or(
        startMode != QStringLiteral("autoplay-sound")) : m_muted;
    emit changed();
    QMetaObject::invokeMethod(m_worker, "openSource", Qt::QueuedConnection,
        Q_ARG(qulonglong, generation), Q_ARG(QVariantMap, source),
        Q_ARG(QString, startMode), Q_ARG(bool, m_presentationVisible),
        Q_ARG(bool, m_muted), Q_ARG(qreal, m_volume),
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
    emit changed();
    postWorkerStop();
}

void VideoPlaybackController::playPause() {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "playPause", Qt::QueuedConnection);
        resetPresentationClock();
    }
}

void VideoPlaybackController::resume() {
    if (!m_worker || m_resumePosition <= 0 || m_resumePosition >= m_duration)
        return;
    QMetaObject::invokeMethod(m_worker, "resume", Qt::QueuedConnection,
        Q_ARG(qulonglong, m_generation.load(std::memory_order_acquire)), Q_ARG(qint64, m_resumePosition));
    resetPresentationClock();
    m_resumePosition = 0;
    emit changed();
}

void VideoPlaybackController::seekBy(qint64 deltaMs) {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "seekBy", Qt::QueuedConnection,
                                  Q_ARG(qint64, deltaMs));
        resetPresentationClock();
    }
}

void VideoPlaybackController::seekTo(qint64 positionMs) {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "seekTo", Qt::QueuedConnection,
                                  Q_ARG(qint64, positionMs));
        resetPresentationClock();
    }
}

void VideoPlaybackController::toggleLoop() {
    m_looping = !m_looping;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setLooping", Qt::QueuedConnection,
                                  Q_ARG(bool, m_looping));
        resetPresentationClock();
    }
    emit changed();
}

void VideoPlaybackController::toggleMute() {
    if (m_mutePreference) {
        m_mutePreference->setOverride(!m_muted);
    }
}

void VideoPlaybackController::selectAudioTrack(int index) {
    if (m_worker && index >= 0 && index < m_audioTracks.size()) {
        QMetaObject::invokeMethod(m_worker, "selectAudioTrack", Qt::QueuedConnection,
            Q_ARG(qulonglong, m_generation.load(std::memory_order_acquire)), Q_ARG(int, index));
        resetPresentationClock();
        if (!m_fileSettingsGroup.isEmpty()) {
            QSettings settings;
            settings.setValue(m_fileSettingsGroup + QStringLiteral("/audio"), index);
            settings.sync();
        }
    }
}

void VideoPlaybackController::selectSubtitleTrack(int index) {
    if (m_worker && index >= -1 && index < m_subtitleTracks.size()) {
        QMetaObject::invokeMethod(m_worker, "selectSubtitleTrack", Qt::QueuedConnection,
            Q_ARG(qulonglong, m_generation.load(std::memory_order_acquire)), Q_ARG(int, index));
        if (!m_fileSettingsGroup.isEmpty()) {
            QSettings settings;
            settings.setValue(m_fileSettingsGroup + QStringLiteral("/subtitle"), index);
            settings.sync();
        }
    }
}

void VideoPlaybackController::applyMutePreference(bool muted) {
    if (m_muted == muted) return;
    m_muted = muted;
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
        resetPresentationClock();
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

void VideoPlaybackController::setFrameSource(QObject *sourceObject) {
    auto *source = qobject_cast<ViewerVideoFrameSource *>(sourceObject);
    if (m_frameSource == source) return;
    if (m_frameSource) {
        m_frameSource->acceptFrame(QVideoFrame());
    }
    m_frameSource = source;
    if (m_frameSource) drainLatestFrame();
}

void VideoPlaybackController::drainLatestFrame() {
    if (!m_frameMailbox) {
        return;
    }
    QVideoFrame frame;
    qulonglong generation = 0;
    bool allowUpcoming = false;
    {
        QMutexLocker lock(&m_frameMailbox->mutex);
        frame = m_frameMailbox->frame;
        generation = m_frameMailbox->generation.load(std::memory_order_relaxed);
        allowUpcoming = m_frameMailbox->clockRunning && m_frameMailbox->clockRevision == m_clockRevision;
        m_frameMailbox->deliveryQueued = false;
    }
    if (generation == m_generation.load(std::memory_order_acquire)) {
        if (m_videoSink) m_videoSink->setVideoFrame(frame);
        if (m_frameSource) m_frameSource->acceptFrame(frame, allowUpcoming && m_presentationVisible && m_playing);
    }
}

void VideoPlaybackController::resetPresentationClock() {
#ifdef Q_OS_MACOS
    // Called immediately after posting a transport command. Only frames from
    // the worker's new clock can schedule a future image on the native layer.
    const quint64 revision = ++m_clockRevision;
    if (m_frameSource) m_frameSource->cancelUpcoming();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker.data(), revision] {
        worker->acknowledgeClock(revision);
    }, Qt::QueuedConnection);
#endif
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
    qint64 position, qint64 duration, bool playing) {
    if (generation != m_generation.load(std::memory_order_acquire)) {
        return;
    }
    if (state == QStringLiteral("ended")) {
        m_resumePosition = 0;
        m_pendingProgress = 0;
        saveProgress();
    } else if (state == QStringLiteral("ready") && duration > 0) {
        if (m_resumePosition >= duration)
            m_resumePosition = 0;
        if (position != m_position && position < duration)
            m_pendingProgress = position;
        if (!playing || m_progressTimer.elapsed() >= 5000)
            saveProgress();
    }
    m_state = state;
    m_error = error;
    m_position = position;
    m_duration = duration;
    if (m_playing && !playing && m_frameSource) m_frameSource->cancelUpcoming();
    m_playing = playing;
    if (state == QStringLiteral("failed"))
        resetTracks();
    emit changed();
}

void VideoPlaybackController::saveProgress() {
    if (!m_pendingProgress || m_fileSettingsGroup.isEmpty())
        return;
    QSettings settings;
    const QString key = m_fileSettingsGroup + QStringLiteral("/positionMs");
    if (*m_pendingProgress > 0)
        settings.setValue(key, *m_pendingProgress);
    else
        settings.remove(key);
    m_pendingProgress.reset();
    m_progressTimer.restart();
}

void VideoPlaybackController::invalidateSource(bool clearFrame) {
    saveProgress();
    m_pendingProgress.reset();
    m_resumePosition = 0;
    m_fileSettingsGroup.clear();
    updateSubtitleOffsetPercent(0, false);
    const qulonglong previousGeneration =
        m_generation.load(std::memory_order_acquire);
    const qulonglong generation =
        m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    resetTracks();
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
    if (clearFrame) {
        if (m_videoSink) m_videoSink->setVideoFrame(QVideoFrame());
        if (m_frameSource) m_frameSource->acceptFrame(QVideoFrame());
    }
}

void VideoPlaybackController::updateSubtitleText(const QString &text) {
    if (m_subtitleText == text)
        return;
    m_subtitleText = text;
    emit subtitleTextChanged();
}

void VideoPlaybackController::setSubtitleOffsetPercent(int percent) {
    updateSubtitleOffsetPercent(percent, true);
}

void VideoPlaybackController::updateSubtitleOffsetPercent(int percent, bool save) {
    percent = std::clamp(percent, -100, 100);
    if (m_subtitleOffsetPercent == percent)
        return;
    m_subtitleOffsetPercent = percent;
    if (save && !m_fileSettingsGroup.isEmpty())
        QSettings().setValue(m_fileSettingsGroup + QStringLiteral("/subtitleOffsetPercent"), percent);
    emit subtitleOffsetPercentChanged();
}

void VideoPlaybackController::resetTracks() {
    updateSubtitleText({});
    if (m_audioTracks.isEmpty() && m_subtitleTracks.isEmpty()
        && m_activeAudioTrack == -1 && m_activeSubtitleTrack == -1)
        return;
    m_audioLanguage.clear();
    m_subtitleLanguage.clear();
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_activeAudioTrack = -1;
    m_activeSubtitleTrack = -1;
    emit tracksChanged();
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
