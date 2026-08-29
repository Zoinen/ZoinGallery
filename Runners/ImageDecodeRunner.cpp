#include "ImageDecodeRunner.h"
#include "ThumbnailLoader.h"
#include "PersistentImageCache.h"

#include <ZoinGallery/MediaTimingTrace.h>

ImageDecodeRunner::ImageDecodeRunner(const ImageData &request)
    : _imageData(request) {
}

void ImageDecodeRunner::run() {
    QVariantMap timingFields =
        ZoinGallery::MediaTimingTrace::sourceFields(
            _imageData.request.info.source);
    timingFields.insert(QStringLiteral("sourceIdentity"),
                        _imageData.request.info.sourceIdentity());
    timingFields.insert(QStringLiteral("targetWidth"),
                        _imageData.request.targetSize.width());
    timingFields.insert(QStringLiteral("targetHeight"),
                        _imageData.request.targetSize.height());
    timingFields.insert(QStringLiteral("viewer"),
                        _imageData.request.viewerRequest);
    timingFields.insert(QStringLiteral("highPriority"),
                        _imageData.request.highPriority);
    timingFields.insert(QStringLiteral("compressedBytes"),
                        _imageData.data.size());
    ZoinGallery::MediaTimingTrace::Span timingSpan(
        QStringLiteral("qt.gallery.decode"), timingFields);
    DecodedImageInfo decodedInfo;
    QImage image;
    {
        ZoinGallery::MediaTimingTrace::Span decodeSpan(
            QStringLiteral("qt.gallery.decode.pixels"), timingFields);
        image = ThumbnailLoader::decode(_imageData, decodedInfo);
        decodeSpan.set(QStringLiteral("ok"), !image.isNull());
        decodeSpan.set(QStringLiteral("decodedWidth"), image.width());
        decodeSpan.set(QStringLiteral("decodedHeight"), image.height());
    }
    QImage thumbnail;
    {
        ZoinGallery::MediaTimingTrace::Span resizeSpan(
            QStringLiteral("qt.gallery.decode.resize"), timingFields);
        thumbnail = ThumbnailLoader::createThumbnail(
            image, _imageData.request.targetSize);
        resizeSpan.set(QStringLiteral("ok"), !thumbnail.isNull());
        resizeSpan.set(QStringLiteral("outputWidth"), thumbnail.width());
        resizeSpan.set(QStringLiteral("outputHeight"), thumbnail.height());
    }
    timingSpan.set(QStringLiteral("ok"), !thumbnail.isNull());
    timingSpan.set(QStringLiteral("decodedWidth"), image.width());
    timingSpan.set(QStringLiteral("decodedHeight"), image.height());
    timingSpan.set(QStringLiteral("outputWidth"), thumbnail.width());
    timingSpan.set(QStringLiteral("outputHeight"), thumbnail.height());
    emit imageReady(_imageData.request, thumbnail, decodedInfo);

    if (_imageData.request.storeInPersistentCache &&
        !PersistentImageCache::hasImage(_imageData.request)) {
        // External artifacts are keyed by request intent and stable size
        // tier, so persist the exact frame delivered to the consumer. The
        // standalone path keeps its historical 1024px reusable frame.
        const QImage &cacheImage = _imageData.request.info.source.isValid()
            ? thumbnail : image;
        QByteArray imageData;
        {
            ZoinGallery::MediaTimingTrace::Span cacheEncodeSpan(
                QStringLiteral("qt.gallery.cache_encode"), timingFields);
            imageData = PersistentImageCache::createImageForCache(
                _imageData.request, cacheImage);
            cacheEncodeSpan.set(QStringLiteral("bytes"), imageData.size());
            cacheEncodeSpan.set(QStringLiteral("ok"), !imageData.isEmpty());
        }
        if (!imageData.isEmpty()) {
            emit storeInCache(_imageData.request, imageData);
        }
    }

    emit finished(this);
}

QString ImageDecodeRunner::path() const {
    return _imageData.request.info.sourceIdentity();
}

bool ImageDecodeRunner::isViewerRequest() const {
    return _imageData.request.viewerRequest &&
        !_imageData.request.backgroundViewerRequest;
}
