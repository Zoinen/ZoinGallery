#include "ImageDecodeRunner.h"
#include "ThumbnailLoader.h"
#include "PersistentImageCache.h"

ImageDecodeRunner::ImageDecodeRunner(const ImageData &request)
    : _imageData(request) {
}

void ImageDecodeRunner::run() {
    QImage image = ThumbnailLoader::decode(_imageData);
    QImage thumbnail = ThumbnailLoader::createThumbnail(image, _imageData.request.targetSize);
    emit imageReady(_imageData.request, thumbnail, false);

    if (!PersistentImageCache::hasImage(_imageData.request.info.path)) {
        QByteArray imageData = PersistentImageCache::createImageForCache(image);
        emit storeInCache(_imageData.request, imageData);
    }

    emit finished(this);
}

QString ImageDecodeRunner::path() const {
    return _imageData.request.info.path;
}

bool ImageDecodeRunner::isViewerRequest() const {
    return _imageData.request.viewerRequest;
}
