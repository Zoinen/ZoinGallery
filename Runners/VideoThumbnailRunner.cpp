#include "VideoThumbnailRunner.h"

#include "PersistentImageCache.h"

#include <ZoinGallery/ImageSourceProvider.h>
#include <ZoinGallery/MediaTimingTrace.h>

#include <QEventLoop>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

constexpr auto VideoContactSheetTransform = "video-contact-sheet-2x2-v2";
constexpr int VideoFrameCount = 4;
constexpr int VideoFrameTimeoutMs = 1800;
constexpr int VideoLoadTimeoutMs = 15000;

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
                 QStringLiteral("derived-cache-satisfied"));
        emit finished(this);
        return;
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
    QString sourcePath = _request.info.path;
    if (_request.info.source.isValid()) {
        lease = _provider
            ? _provider->materialize(_request.info.source, _cancellation)
            : QSharedPointer<ZoinGallery::ImageSourceLease>();
        if (lease) {
            sourcePath = lease->localPath();
        }
    }
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)
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
        positions = {duration * 10 / 100, duration * 35 / 100,
                     duration * 60 / 100, duration * 85 / 100};
        captureNext();
    });
    connect(&player, &QMediaPlayer::mediaStatusChanged, &eventLoop,
            [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia) {
            fail();
        }
    });

    loadTimer.start(VideoLoadTimeoutMs);
    player.setSource(QUrl::fromLocalFile(QFileInfo(sourcePath).absoluteFilePath()));
    eventLoop.exec();

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
