#include "ImageInfoReadRunner.h"
#include "ThumbnailLoader.h"

ImageInfoReadRunner::ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView)
    : _path(path), _isLast(isLast), _isFromEmbeddedView(isFromEmbeddedView) {
}

void ImageInfoReadRunner::run() {
    ImageInfo result{
        .path = _path,
        .isLast = _isLast,
        .isFromEmbeddedView = _isFromEmbeddedView,
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
