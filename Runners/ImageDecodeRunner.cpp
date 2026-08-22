#include "ImageDecodeRunner.h"
#include "ThumbnailLoader.h"
#include "PersistentImageCache.h"

ImageDecodeRunner::ImageDecodeRunner(const ImageData &request)
    : _imageData(request) {
}

void ImageDecodeRunner::run() {
    DecodedImageInfo decodedInfo;
    QImage image = ThumbnailLoader::decode(_imageData, decodedInfo);
    QImage thumbnail = ThumbnailLoader::createThumbnail(image, _imageData.request.targetSize);
    emit imageReady(_imageData.request, thumbnail, decodedInfo);

    if (_imageData.request.storeInPersistentCache &&
        !PersistentImageCache::hasImage(_imageData.request)) {
        // External artifacts are keyed by request intent and stable size
        // tier, so persist the exact frame delivered to the consumer. The
        // standalone path keeps its historical 1024px reusable frame.
        const QImage &cacheImage = _imageData.request.info.source.isValid()
            ? thumbnail : image;
        const QByteArray imageData =
            PersistentImageCache::createImageForCache(
                _imageData.request, cacheImage);
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
