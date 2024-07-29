#include "ImageDecodeRunner.h"
#include "ThumbnailLoader.h"
#include "PersistentImageCache.h"

#include <QBuffer>

ImageDecodeRunner::ImageDecodeRunner(const ImageData &request)
    : _imageData(request) {
}

void ImageDecodeRunner::run() {
    ThumbnailLoader loader;
    QImage image;
    if (!_imageData.data.isNull()) {
        QSize targetSize = _imageData.request.targetSize;
        if (!_imageData.request.checkCache) {
            targetSize = expandToCacheImageResolution(targetSize);
        }
        image = loader.decodeImage(_imageData.data, _imageData.mimeType, rotateToOrientation(targetSize, _imageData.request.info.orientation));

        // if (_imageData.request.viewerRequest) {
        //     emit finished(this);
        //     return;
        // }


        if (image.isNull() && _imageData.previewData) {
            QByteArray previewData = QByteArray::fromRawData(_imageData.previewData.get(), _imageData.previewDataSize);
            image = loader.decodeImage(previewData, _imageData.previewMimeType, rotateToOrientation(targetSize, _imageData.request.info.orientation));
            image = loader.rotateAndFlip(image, _imageData.request.info.orientation);
        }
        else {
            image = loader.rotateAndFlip(image, _imageData.request.info.orientation);
        }
    }

    QImage thumbnail = image;
    if (!ThumbnailLoader::isVectorImage(_imageData.request.info.path)) {
        thumbnail = loader.createThumbnail(image, _imageData.request.targetSize);
    }

    emit imageReady(_imageData.request, thumbnail, false);

    if (!PersistentImageCache::hasImage(_imageData.request.info.path)) {
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);

        QImage scaled = image;
        if (CACHE_IMAGE_RESOLUTION.width() < image.width() ||
            CACHE_IMAGE_RESOLUTION.height() < image.height()) { // Never upscale
            scaled = image.scaled(CACHE_IMAGE_RESOLUTION, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        // scaled.invertPixels();
        scaled.save(&buffer, "webp", 50);

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
