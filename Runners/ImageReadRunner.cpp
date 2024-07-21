#include "ImageReadRunner.h"
#include "ThumbnailLoader.h"

ImageReadRunner::ImageReadRunner(const ImageDecodeRequest &request)
    : _request(request) {
}

void ImageReadRunner::run() {
    ThumbnailLoader loader;
    ImageData imageData(_request);
    if (loader.readImage(imageData)) {
        emit imageReadReady(imageData);
    }
    emit finished(this);
}

QString ImageReadRunner::path() const {
    return _request.info.path;
}

bool ImageReadRunner::isViewerRequest() const {
    return _request.viewerRequest;
}
