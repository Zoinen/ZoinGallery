#include "ImageDecodeRunner.h"

#include "ThumbnailLoader.h"

ImageDecodeRunner::ImageDecodeRunner(const ImageData &request)
    : _imageData(request) {
}

void ImageDecodeRunner::run() {
    ThumbnailLoader loader;
    QImage image;
    if (!_imageData.data.isNull()) {
        image = loader.decodeImage(_imageData.data, _imageData.mimeType, rotateToOrientation(_imageData.request.targetSize, _imageData.request.orientation));

        if (image.isNull() && _imageData.previewData) {
            QByteArray previewData = QByteArray::fromRawData(_imageData.previewData.get(), _imageData.previewDataSize);
            image = loader.decodeImage(previewData, _imageData.previewMimeType, rotateToOrientation(_imageData.request.targetSize, _imageData.request.orientation));
        }
    }
    image = loader.rotateAndFlip(image, _imageData.request.orientation);
    if (!ThumbnailLoader::isVectorImage(_imageData.request.path)) {
        image = loader.createThumbnail(image, _imageData.request.targetSize);
    }
    emit imageReady(_imageData.request, image);
    emit finished(this);
}

bool ImageDecodeRunner::isViewerRequest() const {
    return _imageData.request.viewerRequest;
}
