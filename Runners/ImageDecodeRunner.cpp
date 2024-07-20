#include "ImageDecodeRunner.h"
#include "ThumbnailLoader.h"
#include "PersistentImageCache.h"

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
        image = loader.decodeImage(_imageData.data, _imageData.mimeType, rotateToOrientation(targetSize, _imageData.orientation));

        if (image.isNull() && _imageData.previewData) {
            QByteArray previewData = QByteArray::fromRawData(_imageData.previewData.get(), _imageData.previewDataSize);
            image = loader.decodeImage(previewData, _imageData.previewMimeType, rotateToOrientation(targetSize, _imageData.previewOrientation));
            image = loader.rotateAndFlip(image, _imageData.previewOrientation);
        }
        else {
            image = loader.rotateAndFlip(image, _imageData.orientation);
        }
    }
    QImage thumbnail = image;
    if (!ThumbnailLoader::isVectorImage(_imageData.request.info.path)) {
        thumbnail = loader.createThumbnail(image, _imageData.request.targetSize);
    }
    emit imageReady(_imageData.request, thumbnail, false);

    if (!PersistentImageCache::hasImage(_imageData.request.info.path)) {
        emit storeInCache(_imageData.request, image);
    }

    emit finished(this);
}

bool ImageDecodeRunner::isViewerRequest() const {
    return _imageData.request.viewerRequest;
}
