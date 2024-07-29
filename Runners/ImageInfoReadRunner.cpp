#include "ImageInfoReadRunner.h"
#include "ThumbnailLoader.h"

ImageInfoReadRunner::ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView, bool isFromScanner)
    : _path(path), _isLast(isLast), _isFromEmbeddedView(isFromEmbeddedView), _isFromScanner(isFromScanner) {
}

void ImageInfoReadRunner::run() {
    ImageInfo result{
        .path = _path,
        .isLast = _isLast,
        .isFromEmbeddedView = _isFromEmbeddedView,
        .isFromScanner = _isFromScanner
    };

    ThumbnailLoader loader;
    if (!loader.readExif(result)) {
        loader.readGenericInfo(result);
    }

    emit imageInfoReady(result);
    emit finished(this);
}

bool ImageInfoReadRunner::isEmbeddedRequest() const {
    return _isFromEmbeddedView;
}
