#include "ImageReadRunner.h"
#include "ThumbnailLoader.h"

ImageReadRunner::ImageReadRunner(const ImageDecodeRequest &request)
    : _request(request) {
}

void ImageReadRunner::run() {
    ImageData imageData(_request);
    if (ThumbnailLoader::readImage(imageData)) {
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
