#include "VideoThumbnailRunner.h"

#include "PersistentImageCache.h"

#include <ZoinGallery/ImageSourceProvider.h>
#include <ZoinGallery/MediaTimingTrace.h>

#include <QEventLoop>
#include <QFileInfo>
#include <QIODevice>
#include <QMediaPlayer>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

namespace {

constexpr auto VideoContactSheetTransform = "video-contact-sheet-2x2-v3";
constexpr int VideoFrameCount = 4;
constexpr int VideoFrameTimeoutMs = 1800;
constexpr int VideoLoadTimeoutMs = 15000;
constexpr qint64 RangeSourceReadChunkSize = 1024 * 1024;

bool canStreamSource(const ZoinGallery::ImageSourceDescriptor &source,
                     const QSharedPointer<ZoinGallery::ImageSourceProvider>
                         &provider)
{
    if (!provider || !source.isValid() || source.size <= 0
        || source.storageClass == QStringLiteral("local")) {
        return false;
    }
    return source.accessProfile == QStringLiteral("nativeRange")
        || source.accessProfile == QStringLiteral("hybridRange");
}

QUrl sourceUrlHint(const ZoinGallery::ImageSourceDescriptor &source)
{
    const QString name = source.displayName.trimmed().isEmpty()
        ? QStringLiteral("f4-video") : source.displayName.trimmed();
    return QUrl::fromLocalFile(name);
}

// QMediaPlayer's FFmpeg backend can consume a seekable QIODevice. Keep the
// broker-backed source seekable while fetching only the requested byte ranges;
// this avoids turning a large range-capable remote video into a full temp-file
// download before the first thumbnail frame is decoded.
class RangeImageSourceDevice final : public QIODevice
{
public:
    RangeImageSourceDevice(
        QSharedPointer<ZoinGallery::ImageSourceProvider> provider,
        ZoinGallery::ImageSourceDescriptor source,
        QSharedPointer<ZoinGallery::ImageSourceCancellation> cancellation)
        : m_provider(std::move(provider))
        , m_source(std::move(source))
        , m_cancellation(std::move(cancellation))
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    bool isSequential() const override { return false; }

    qint64 size() const override { return m_source.size; }

    bool seek(qint64 position) override
    {
        if (position < 0 || (m_source.size >= 0 && position > m_source.size)) {
            return false;
        }
        if (!QIODevice::seek(position)) {
            return false;
        }
        if (position < m_prefetchOffset
            || position >= m_prefetchOffset + m_prefetched.size()) {
            clearPrefetch();
        }
        return true;
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        if (!m_provider || maxlen <= 0) {
            return 0;
        }
        if (m_cancellation && m_cancellation->isCanceled()) {
            setErrorString(QStringLiteral("video source read cancelled"));
            return -1;
        }

        const qint64 offset = pos();
        if (offset < 0 || (m_source.size >= 0 && offset >= m_source.size)) {
            return 0;
        }
        const qint64 prefetchEnd = m_prefetchOffset >= 0
            ? m_prefetchOffset + m_prefetched.size() : -1;
        if (m_prefetchOffset < 0 || offset < m_prefetchOffset
            || offset >= prefetchEnd) {
            if (m_prefetchEndOfFile && offset >= prefetchEnd) {
                return 0;
            }
            const qint64 requested = std::min(
                RangeSourceReadChunkSize, m_source.size - offset);
            if (requested <= 0) {
                return 0;
            }

            const ZoinGallery::ImageSourceReadResult result =
                m_provider->readRange(m_source, offset, requested, m_cancellation);
            if (!result.succeeded()) {
                setErrorString(result.errorString);
                return -1;
            }
            if (result.data.isEmpty()) {
                if (result.endOfFile) {
                    m_prefetchOffset = offset;
                    m_prefetched.clear();
                    m_prefetchEndOfFile = true;
                    return 0;
                }
                setErrorString(QStringLiteral("video source returned no data"));
                return -1;
            }
            m_prefetchOffset = offset;
            m_prefetched = result.data;
            m_prefetchEndOfFile = result.endOfFile;
        }

        const qint64 relative = offset - m_prefetchOffset;
        const qint64 available = std::min<qint64>(
            maxlen, static_cast<qint64>(m_prefetched.size()) - relative);
        if (available <= 0) {
            return 0;
        }
        std::memcpy(data, m_prefetched.constData() + relative,
                    static_cast<size_t>(available));
        return available;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    void clearPrefetch()
    {
        m_prefetchOffset = -1;
        m_prefetched.clear();
        m_prefetchEndOfFile = false;
    }

    QSharedPointer<ZoinGallery::ImageSourceProvider> m_provider;
    ZoinGallery::ImageSourceDescriptor m_source;
    QSharedPointer<ZoinGallery::ImageSourceCancellation> m_cancellation;
    QByteArray m_prefetched;
    qint64 m_prefetchOffset = -1;
    bool m_prefetchEndOfFile = false;
};

QImage composeContactSheet(const QList<QImage> &frames, const QSize &target) {
    const QSize canvasSize(qMax(2, target.width()), qMax(2, target.height()));
    QImage canvas(canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    const int cellWidth = (canvas.width() + 1) / 2;
    const int cellHeight = (canvas.height() + 1) / 2;
    for (int index = 0; index < frames.size() && index < VideoFrameCount;
         ++index) {
        const QImage &frame = frames.at(index);
        if (frame.isNull()) {
            continue;
        }
        const QImage scaled = frame.scaled(
            QSize(cellWidth, cellHeight), Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        const int x = (index % 2) * cellWidth
            + (cellWidth - scaled.width()) / 2;
        const int y = (index / 2) * cellHeight
            + (cellHeight - scaled.height()) / 2;
        painter.drawImage(QPoint(x, y), scaled);
    }
    return canvas;
}

QVariantMap videoTraceFields(const ImageDecodeRequest &request) {
    QVariantMap fields = ZoinGallery::MediaTimingTrace::sourceFields(
        request.info.source);
    fields.insert(QStringLiteral("sourceIdentity"),
                  request.info.sourceIdentity());
    fields.insert(QStringLiteral("targetWidth"), request.targetSize.width());
    fields.insert(QStringLiteral("targetHeight"), request.targetSize.height());
    fields.insert(QStringLiteral("requestNamespace"), request.requestNamespace);
    fields.insert(QStringLiteral("thumbnailKind"), request.info.thumbnailKind);
    fields.insert(QStringLiteral("transform"),
                  QString::fromLatin1(VideoContactSheetTransform));
    return fields;
}

} // namespace

QVector<qint64> VideoThumbnailRunner::thumbnailPositions(qint64 duration) {
    if (duration <= 0) {
        return {};
    }
    return {duration * 20 / 100, duration * 40 / 100,
            duration * 60 / 100, duration * 80 / 100};
}

VideoThumbnailRunner::VideoThumbnailRunner(
    const ImageDecodeRequest &request,
    QSharedPointer<ZoinGallery::ImageSourceProvider> provider)
    : _request(request),
      _provider(std::move(provider)),
      _cancellation(QSharedPointer<ZoinGallery::ImageSourceCancellation>::create()),
      _derivedLookupGate(PersistentDerivedImageCache::joinLookup(request)) {
}

void VideoThumbnailRunner::run() {
    QVariantMap fields = videoTraceFields(_request);
    ZoinGallery::MediaTimingTrace::Span span(
        QStringLiteral("qt.gallery.video_thumbnail"), fields);

    if (PersistentDerivedImageCache::waitForLookup(
            _derivedLookupGate, _cancellation)) {
        span.set(QStringLiteral("outcome"),
                 _request.info.source.isValid()
                     ? QStringLiteral("derived-cache-satisfied")
                     : QStringLiteral("cache-satisfied"));
        emit finished(this);
        return;
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
    std::unique_ptr<RangeImageSourceDevice> rangeDevice;
    QString sourcePath = _request.info.path;
    if (_request.info.source.isValid()) {
        if (canStreamSource(_request.info.source, _provider)) {
            rangeDevice = std::make_unique<RangeImageSourceDevice>(
                _provider, _request.info.source, _cancellation);
            span.set(QStringLiteral("sourceMode"), QStringLiteral("range"));
        } else {
            lease = _provider
                ? _provider->materialize(_request.info.source, _cancellation)
                : QSharedPointer<ZoinGallery::ImageSourceLease>();
            if (lease) {
                sourcePath = lease->localPath();
            }
            span.set(QStringLiteral("sourceMode"), QStringLiteral("materialized"));
        }
    }
    if ((!rangeDevice && (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)))
        || _cancellation->isCanceled() || isCanceled()) {
        if (!isCanceled() && !_cancellation->isCanceled()) {
            ImageDecodeRequest failed = _request;
            failed.sourceAccessFailed = _request.info.source.isValid();
            emit imageReadFailed(failed);
        }
        span.set(QStringLiteral("outcome"),
                 isCanceled() || _cancellation->isCanceled()
                     ? QStringLiteral("cancelled")
                     : QStringLiteral("source-unavailable"));
        emit finished(this);
        return;
    }

    QMediaPlayer player;
    QVideoSink sink;
    QEventLoop eventLoop;
    QTimer loadTimer;
    QTimer frameTimer;
    loadTimer.setSingleShot(true);
    frameTimer.setSingleShot(true);
    player.setVideoSink(&sink);

    QList<QImage> frames(VideoFrameCount);
    QVector<qint64> positions;
    qint64 duration = -1;
    int currentFrame = -1;
    quint64 frameSerial = 0;
    quint64 frameBaseline = 0;
    bool frameCaptured = false;
    bool captureStarted = false;
    bool completed = false;

    const auto fail = [&]() {
        if (completed) {
            return;
        }
        completed = true;
        frameTimer.stop();
        loadTimer.stop();
        player.stop();
        eventLoop.quit();
    };
    const auto finish = [&]() {
        if (completed) {
            return;
        }
        completed = true;
        frameTimer.stop();
        loadTimer.stop();
        player.stop();
        eventLoop.quit();
    };

    // Runner::cancel() is called by DecodeManager's thread, while this media
    // player and its nested event loop live on a decode worker.  A queued
    // cancellation callback both stops FFmpeg promptly and releases the
    // worker slot instead of waiting for the 1.8 s frame or 15 s load timer.
    connect(this, &Runner::cancelRequested, &eventLoop,
            [&, cancelFields = fields]() {
        if (completed) {
            return;
        }
        ZoinGallery::MediaTimingTrace::event(
            QStringLiteral("qt.gallery.video_thumbnail.cancel_wake"),
            ZoinGallery::MediaTimingTrace::mergedFields(
                cancelFields,
                {{QStringLiteral("fix"),
                  QStringLiteral("[FIX:video-cancel-wake]")}}));
        fail();
    }, Qt::QueuedConnection);

    std::function<void()> captureNext;
    captureNext = [&]() {
        if (completed || isCanceled() || _cancellation->isCanceled()) {
            fail();
            return;
        }
        ++currentFrame;
        if (currentFrame >= VideoFrameCount) {
            finish();
            return;
        }
        frameBaseline = frameSerial;
        frameCaptured = false;
        frameTimer.start(VideoFrameTimeoutMs);
        player.pause();
        player.setPosition(positions.value(currentFrame));
        player.play();
    };

    connect(&sink, &QVideoSink::videoFrameChanged, &eventLoop,
            [&](const QVideoFrame &videoFrame) {
        if (completed || currentFrame < 0 || frameCaptured
            || frameSerial++ < frameBaseline
            || !videoFrame.isValid()) {
            return;
        }
        const QImage image = videoFrame.toImage();
        if (image.isNull()) {
            return;
        }
        frameTimer.stop();
        player.pause();
        frameCaptured = true;
        frames[currentFrame] = image;
        QTimer::singleShot(0, &eventLoop, captureNext);
    });
    connect(&frameTimer, &QTimer::timeout, &eventLoop, [&]() {
        if (completed) {
            return;
        }
        // A broken stream may not produce a frame at a requested seek point.
        // Leave that cell black and continue so one bad seek does not discard
        // the other usable timestamps.
        QTimer::singleShot(0, &eventLoop, captureNext);
    });
    connect(&loadTimer, &QTimer::timeout, &eventLoop, fail);
    connect(&player, &QMediaPlayer::errorOccurred, &eventLoop,
            [&](QMediaPlayer::Error, const QString &) { fail(); });
    connect(&player, &QMediaPlayer::durationChanged, &eventLoop,
            [&](qint64 value) {
        if (captureStarted || value <= 0) {
            duration = value;
            return;
        }
        duration = value;
        captureStarted = true;
        positions = thumbnailPositions(duration);
        captureNext();
    });
    connect(&player, &QMediaPlayer::mediaStatusChanged, &eventLoop,
            [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia) {
            fail();
        }
    });

    loadTimer.start(VideoLoadTimeoutMs);
    if (rangeDevice) {
        player.setSourceDevice(rangeDevice.get(), sourceUrlHint(_request.info.source));
    } else {
        player.setSource(QUrl::fromLocalFile(QFileInfo(sourcePath).absoluteFilePath()));
    }
    if (isCanceled() || _cancellation->isCanceled()) {
        fail();
    } else {
        eventLoop.exec();
    }

    if (!completed || isCanceled() || _cancellation->isCanceled()) {
        span.set(QStringLiteral("outcome"), QStringLiteral("cancelled"));
        emit finished(this);
        return;
    }
    const int validFrames = static_cast<int>(std::count_if(
        frames.cbegin(), frames.cend(), [](const QImage &image) {
            return !image.isNull();
        }));
    if (validFrames == 0) {
        ImageDecodeRequest failed = _request;
        failed.sourceAccessFailed = _request.info.source.isValid();
        emit imageReadFailed(failed);
        span.set(QStringLiteral("outcome"), QStringLiteral("no-frame"));
        emit finished(this);
        return;
    }

    // Keep the sheet deterministic even for a short or partially damaged
    // stream: repeat the nearest captured frame into missing cells.
    QImage fallback;
    for (const QImage &frame : std::as_const(frames)) {
        if (!frame.isNull()) {
            fallback = frame;
            break;
        }
    }
    for (QImage &frame : frames) {
        if (frame.isNull()) {
            frame = fallback;
        }
    }
    const QImage sheet = composeContactSheet(frames, _request.targetSize);
    DecodedImageInfo decodedInfo;
    decodedInfo.decoderUsed = QStringLiteral("QtMultimedia/FFmpeg");
    decodedInfo.previewUsed = QStringLiteral("video-contact-sheet-2x2");
    emit imageReady(_request, sheet, decodedInfo);
    if (_request.storeInPersistentCache) {
        const QByteArray data = PersistentImageCache::createImageForCache(
            _request, sheet);
        if (!data.isEmpty()) {
            emit storeInCache(_request, data);
        }
    }
    span.set(QStringLiteral("outcome"), QStringLiteral("ok"));
    span.set(QStringLiteral("durationMs"), duration);
    span.set(QStringLiteral("capturedFrames"), validFrames);
    span.set(QStringLiteral("outputWidth"), sheet.width());
    span.set(QStringLiteral("outputHeight"), sheet.height());
    emit finished(this);
}

QString VideoThumbnailRunner::path() const {
    return _request.info.sourceIdentity();
}

bool VideoThumbnailRunner::isViewerRequest() const {
    return _request.viewerRequest && !_request.backgroundViewerRequest;
}
