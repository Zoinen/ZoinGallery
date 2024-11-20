#include "ImageInfoReadRunner.h"
#include "ThumbnailLoader.h"

#include <QThread>

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

    ThumbnailLoader::readMetadata(result);

    // if (result.path.endsWith("09.jpeg") || result.path.endsWith("16.jpeg") || result.path.endsWith("23.jpeg") || result.path.endsWith("28.jpeg")) {
    //     QThread::sleep(5);
    // }
    // if (!_isFromEmbeddedView) {
    //     QThread::msleep(250);
    // }

    emit imageInfoReady(result);
    emit finished(this);
}

bool ImageInfoReadRunner::isEmbeddedRequest() const {
    return _isFromEmbeddedView;
}
