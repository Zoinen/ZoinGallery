#include "VideoThumbnailRunner.h"

#include "PersistentImageCache.h"
#include "../VideoFrameGeometry.h"
#include "../src/embed/ImageSourceRangeDevice.h"

#include <ZoinGallery/ImageSourceProvider.h>
#include <ZoinGallery/MediaTimingTrace.h>

#include <QEventLoop>
#include <QFileInfo>
#include <QIODevice>
#include <QMediaPlayer>
#include <QMediaFormat>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <utility>

namespace {

constexpr auto VideoContactSheetTransform = "video-contact-sheet-2x2-display-v4";
constexpr auto VideoPosterTransform = "video-first-frame-poster-1024-display-v3";
constexpr int VideoFrameCount = 4;
constexpr int VideoPosterMaxLongEdge = 1024;
constexpr int VideoFrameTimeoutMs = 1800;
constexpr int VideoLoadTimeoutMs = 15000;
constexpr qint64 RangeSourceReadChunkSize = 1024 * 1024;

QImage composeContactSheet(const QList<QImage> &frames, const QSize &target) {
    const auto firstFrame = std::find_if(frames.cbegin(), frames.cend(),
        [](const QImage &frame) { return !frame.isNull(); });
    if (firstFrame == frames.cend())
        return {};
    const QSize fittedSize = firstFrame->size().scaled(target, Qt::KeepAspectRatio);
    const QSize canvasSize(qMax(2, fittedSize.width()), qMax(2, fittedSize.height()));
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

QImage scaleVideoPoster(const QImage &image) {
    if (image.isNull()) {
        return image;
    }
    const QSize targetSize = VideoThumbnailRunner::posterSizeFor(image.size());
    if (targetSize == image.size()) {
        return image;
    }
    return image.scaled(targetSize, Qt::IgnoreAspectRatio,
                        Qt::SmoothTransformation);
}

ImageDecodeRequest videoPosterRequestFor(const ImageDecodeRequest &request) {
    ImageDecodeRequest poster = request;
    poster.targetSize = QSize(VideoPosterMaxLongEdge, VideoPosterMaxLongEdge);
    poster.viewerRequest = false;
    poster.backgroundViewerRequest = false;
    poster.panelThumbnailRequest = false;
    poster.fitToViewerRequest = false;
    poster.expandToCacheResolution = false;
    poster.thumbnailTransformKey = QString::fromLatin1(VideoPosterTransform);
    poster.videoPosterRequest = true;
    return poster;
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

QVariantMap VideoThumbnailRunner::fileFieldsForMetadata(
    const QMediaMetaData &metadata, qint64 durationMs, qint64 fileSize) {
    QVariantMap fields;
    const auto number = [&](const QString &id, double value) {
        if (std::isfinite(value) && value > 0) fields.insert(id, value);
    };
    if (durationMs > 0) {
        number(QStringLiteral("media.duration"), double(durationMs) / 1000);
        number(QStringLiteral("media.bitrate"), double(fileSize) * 8000 / durationMs);
    }
    number(QStringLiteral("video.bitrate"), metadata.value(QMediaMetaData::VideoBitRate).toDouble());
    number(QStringLiteral("audio.bitrate"), metadata.value(QMediaMetaData::AudioBitRate).toDouble());
    number(QStringLiteral("video.frame_rate"), metadata.value(QMediaMetaData::VideoFrameRate).toDouble());
    for (const auto &entry : {std::pair{QMediaMetaData::FileFormat, "media.format"},
                             std::pair{QMediaMetaData::VideoCodec, "video.codec"},
                             std::pair{QMediaMetaData::AudioCodec, "audio.codec"}}) {
        const auto value = metadata.value(entry.first);
        const QString text = metadata.stringValue(entry.first).trimmed();
        if (value.isValid() && value.toInt() >= 0 && !text.isEmpty())
            fields.insert(QString::fromLatin1(entry.second), text);
    }
    const QSize resolution = metadata.value(QMediaMetaData::Resolution).toSize();
    if (resolution.width() > 0 && resolution.height() > 0)
        fields.insert(QStringLiteral("video.resolution"),
                      QStringLiteral("%1×%2").arg(resolution.width()).arg(resolution.height()));
    return fields;
}

QVector<qint64> VideoThumbnailRunner::thumbnailPositions(qint64 duration) {
    if (duration <= 0) {
        return {};
    }
    return {duration * 20 / 100, duration * 40 / 100,
            duration * 60 / 100, duration * 80 / 100};
}

QVector<qint64> VideoThumbnailRunner::capturePositions(qint64 duration) {
    if (duration <= 0) {
        return {};
    }
    QVector<qint64> positions{0};
    positions += thumbnailPositions(duration);
    return positions;
}

QSize VideoThumbnailRunner::posterSizeFor(const QSize &frameSize) {
    if (frameSize.width() <= 0 || frameSize.height() <= 0
        || qMax(frameSize.width(), frameSize.height())
            <= VideoPosterMaxLongEdge) {
        return frameSize;
    }

    // Pick a downscaled integer multiple of the reduced frame dimensions so
    // the cached poster keeps the exact display aspect ratio. QSize::scaled()
    // rounds each dimension independently and can otherwise shift the fit
    // geometry by a pixel when the live frame replaces this poster.
    const int divisor = std::gcd(frameSize.width(), frameSize.height());
    const QSize aspectUnit(frameSize.width() / divisor,
                           frameSize.height() / divisor);
    const int scale = VideoPosterMaxLongEdge
        / qMax(aspectUnit.width(), aspectUnit.height());
    if (scale > 0) {
        return QSize(aspectUnit.width() * scale,
                     aspectUnit.height() * scale);
    }

    // Extremely unusual coprime dimensions cannot be reduced below the
    // budget without changing their exact integer ratio. Keep the normal
    // bounded-size fallback for those sources.
    return frameSize.scaled(QSize(VideoPosterMaxLongEdge,
                                  VideoPosterMaxLongEdge),
                            Qt::KeepAspectRatio);
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

    const bool sheetCacheHit = PersistentDerivedImageCache::waitForLookup(
        _derivedLookupGate, _cancellation);
    if (_request.checkCache && !_request.info.fileFieldsRead)
        PersistentDerivedImageCache::retrieveMetadata(_request.info);
    ImageDecodeRequest posterRequest = videoPosterRequestFor(_request);
    QImage poster = PersistentImageCache::retrieveImage(posterRequest);
    const bool posterCacheHit = !poster.isNull();
    if (posterCacheHit) {
        DecodedImageInfo posterInfo;
        posterInfo.decoderUsed = QStringLiteral("PersistentImageCache");
        posterInfo.previewUsed = QStringLiteral("video-first-frame-poster");
        posterInfo.isFromCache = true;
        posterInfo.isAuthoritativeDerivedCache =
            posterRequest.info.source.isValid();
        emit imageReady(posterRequest, poster, posterInfo);
    }
    const bool needPoster = !posterCacheHit;
    const bool needSheet = !sheetCacheHit;
    const bool needMetadata = !_request.info.fileFieldsRead;
    if (_cancellation->isCanceled() || isCanceled()) {
        span.set(QStringLiteral("outcome"), QStringLiteral("cancelled"));
        emit finished(this);
        return;
    }
    if (!needPoster && !needSheet && !needMetadata) {
        span.set(QStringLiteral("outcome"),
                 _request.info.source.isValid()
                     ? QStringLiteral("derived-cache-satisfied")
                     : QStringLiteral("cache-satisfied"));
        emit finished(this);
        return;
    }

    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
    std::unique_ptr<ZoinGallery::ImageSourceRangeDevice> rangeDevice;
    QString sourcePath = _request.info.path;
    if (_request.info.source.isValid()) {
        if (ZoinGallery::imageSourceSupportsRangeReads(
                _request.info.source, _provider)) {
            rangeDevice = std::make_unique<ZoinGallery::ImageSourceRangeDevice>(
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
    struct Capture {
        qint64 position = 0;
        int sheetIndex = -1;
        bool poster = false;
    };
    QVector<Capture> captures;
    qint64 duration = -1;
    int currentCapture = -1;
    quint64 frameSerial = 0;
    quint64 frameBaseline = 0;
    bool frameCaptured = false;
    bool captureStarted = false;
    bool completed = false;
    bool posterCaptured = false;
    QSize posterSourceFrameSize;
    QRect posterViewport;
    QSize posterFrameSize;

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
        ++currentCapture;
        if (currentCapture >= captures.size()) {
            finish();
            return;
        }
        frameBaseline = frameSerial;
        frameCaptured = false;
        frameTimer.start(VideoFrameTimeoutMs);
        player.pause();
        player.setPosition(captures.at(currentCapture).position);
        player.play();
    };

    connect(&sink, &QVideoSink::videoFrameChanged, &eventLoop,
            [&](const QVideoFrame &videoFrame) {
        if (completed || currentCapture < 0 || frameCaptured
            || frameSerial++ < frameBaseline
            || !videoFrame.isValid()) {
            return;
        }
        const Capture capture = captures.at(currentCapture);
        const QImage image = ZoinGallery::VideoFrameGeometry::displayImage(videoFrame);
        if (image.isNull()) {
            return;
        }
        frameTimer.stop();
        player.pause();
        frameCaptured = true;
        if (capture.poster) {
            posterSourceFrameSize = videoFrame.size();
            posterViewport =
                ZoinGallery::VideoFrameGeometry::visibleViewport(videoFrame);
            posterFrameSize = image.size();
            posterRequest.info.imageSize = image.size();
            posterRequest.info.orientation = ExifOrientation::Horizontal;
            poster = scaleVideoPoster(image);
            posterCaptured = !poster.isNull();
        } else if (capture.sheetIndex >= 0
                   && capture.sheetIndex < frames.size()) {
            frames[capture.sheetIndex] = image;
        }
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
        const QVector<qint64> positions = capturePositions(duration);
        if (needPoster && !positions.isEmpty()) {
            captures.append({positions.constFirst(), -1, true});
        }
        if (needSheet && positions.size() >= VideoFrameCount + 1) {
            for (int frame = 0; frame < VideoFrameCount; ++frame) {
                captures.append({positions.at(frame + 1), frame, false});
            }
        }
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
        player.setSourceDevice(
            rangeDevice.get(),
            ZoinGallery::imageSourceUrlHint(_request.info.source));
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
    if (duration > 0) {
        auto metadata = player.metaData();
        const auto appendTrack = [&](const QList<QMediaMetaData> &tracks, int active) {
            if (active < 0 && !tracks.isEmpty()) active = 0;
            if (active < 0 || active >= tracks.size()) return;
            for (const auto key : tracks.at(active).keys())
                metadata.insert(key, tracks.at(active).value(key));
        };
        appendTrack(player.videoTracks(), player.activeVideoTrack());
        appendTrack(player.audioTracks(), player.activeAudioTrack());
        ImageInfo info = _request.info;
        info.typedFileFields = fileFieldsForMetadata(metadata, duration,
            info.source.isValid() ? info.source.size : QFileInfo(sourcePath).size());
        const QSize resolution = metadata.value(QMediaMetaData::Resolution).toSize();
        if (resolution.isValid()) info.imageSize = resolution;
        else if (posterFrameSize.isValid()) info.imageSize = posterFrameSize;
        info.fileFieldsRead = true;
        if (_request.storeInPersistentCache) PersistentDerivedImageCache::storeMetadata(info);
        emit imageInfoReady(info);
        posterRequest.info.typedFileFields = info.typedFileFields;
        posterRequest.info.fileFieldsRead = true;
        _request.info.typedFileFields = info.typedFileFields;
        _request.info.fileFieldsRead = true;
    }
    if (needPoster && posterCaptured) {
        const qreal sourceAspect = posterFrameSize.height() > 0
            ? qreal(posterFrameSize.width()) / posterFrameSize.height() : 0;
        const qreal posterAspect = poster.height() > 0
            ? qreal(poster.width()) / poster.height() : 0;
        ZoinGallery::MediaTimingTrace::event(
            QStringLiteral("qt.gallery.video_thumbnail.poster"),
            ZoinGallery::MediaTimingTrace::mergedFields(fields,
                {{QStringLiteral("frameWidth"), posterSourceFrameSize.width()},
                 {QStringLiteral("frameHeight"), posterSourceFrameSize.height()},
                 {QStringLiteral("viewport"), posterViewport},
                 {QStringLiteral("displayWidth"), posterFrameSize.width()},
                 {QStringLiteral("displayHeight"), posterFrameSize.height()},
                 {QStringLiteral("posterWidth"), poster.width()},
                 {QStringLiteral("posterHeight"), poster.height()},
                 {QStringLiteral("aspectDelta"),
                  sourceAspect > 0
                      ? qAbs(sourceAspect - posterAspect) / sourceAspect : 0}}));
        DecodedImageInfo posterInfo;
        posterInfo.decoderUsed = QStringLiteral("QtMultimedia/FFmpeg");
        posterInfo.previewUsed = QStringLiteral("video-first-frame-poster");
        emit imageReady(posterRequest, poster, posterInfo);
        if (posterRequest.storeInPersistentCache) {
            const QByteArray data = PersistentImageCache::createImageForCache(
                posterRequest, poster);
            if (!data.isEmpty()) {
                emit storeInCache(posterRequest, data);
            }
        }
    }
    if (needSheet && validFrames == 0) {
        ImageDecodeRequest failed = _request;
        failed.sourceAccessFailed = _request.info.source.isValid();
        emit imageReadFailed(failed);
        if (!posterCaptured) {
            span.set(QStringLiteral("outcome"), QStringLiteral("no-frame"));
            emit finished(this);
            return;
        }
    }
    if (!needSheet && !posterCaptured) {
        span.set(QStringLiteral("outcome"), QStringLiteral("no-frame"));
        emit finished(this);
        return;
    }

    if (needSheet) {
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
        ImageDecodeRequest sheetRequest = _request;
        sheetRequest.info.imageSize = fallback.size();
        sheetRequest.info.orientation = ExifOrientation::Horizontal;
        if (_request.storeInPersistentCache)
            PersistentDerivedImageCache::storeMetadata(sheetRequest.info);
        ZoinGallery::MediaTimingTrace::event(
            QStringLiteral("qt.gallery.video_thumbnail.aspect"),
            ZoinGallery::MediaTimingTrace::mergedFields(fields,
                {{QStringLiteral("fix"), QStringLiteral("[FIX:video-thumbnail-aspect]")},
                 {QStringLiteral("displayWidth"), fallback.width()},
                 {QStringLiteral("displayHeight"), fallback.height()},
                 {QStringLiteral("outputWidth"), sheet.width()},
                 {QStringLiteral("outputHeight"), sheet.height()}}));
        DecodedImageInfo decodedInfo;
        decodedInfo.decoderUsed = QStringLiteral("QtMultimedia/FFmpeg");
        decodedInfo.previewUsed = QStringLiteral("video-contact-sheet-2x2");
        emit imageReady(sheetRequest, sheet, decodedInfo);
        if (_request.storeInPersistentCache) {
            const QByteArray data = PersistentImageCache::createImageForCache(
                sheetRequest, sheet);
            if (!data.isEmpty()) {
                emit storeInCache(sheetRequest, data);
            }
        }
        span.set(QStringLiteral("outputWidth"), sheet.width());
        span.set(QStringLiteral("outputHeight"), sheet.height());
    }
    span.set(QStringLiteral("outcome"), QStringLiteral("ok"));
    span.set(QStringLiteral("durationMs"), duration);
    span.set(QStringLiteral("capturedFrames"), validFrames);
    span.set(QStringLiteral("posterCaptured"), posterCaptured);
    emit finished(this);
}

QString VideoThumbnailRunner::path() const {
    return _request.info.sourceIdentity();
}

bool VideoThumbnailRunner::isViewerRequest() const {
    return _request.viewerRequest && !_request.backgroundViewerRequest;
}
