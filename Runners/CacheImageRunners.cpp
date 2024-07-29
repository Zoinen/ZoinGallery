#include "CacheImageRunners.h"

#include "PersistentImageCache.h"

CachedImageRetrieveRunner::CachedImageRetrieveRunner(const ImageDecodeRequest &request)
    : _request(request) {
}

void CachedImageRetrieveRunner::run() {
    // QImage img(1, 1, QImage::Format_ARGB32);
    QImage img = PersistentImageCache::retrieveImage(_request);
    // qDebug() << "XX REQ" << _request.info.path << img << _request.targetSize;
    if (!img.isNull()) {
        emit cachedThumbnailRetrieved(_request, img, true);
    }
    emit finished(this);
}


CachedImageStoreRunner::CachedImageStoreRunner(const ImageInfo &imageInfo, const QByteArray &imageData)
    : _imageInfo(imageInfo), _imageData(imageData) {
}

void CachedImageStoreRunner::run() {
    PersistentImageCache::storeImage(_imageInfo, _imageData);
    emit finished(this);
}




CachedImageInfoRunner::CachedImageInfoRunner(const QStringList &imagePaths, bool isFromEmbeddedView)
    : _imagePaths(imagePaths), _isFromEmbeddedView(isFromEmbeddedView) {
}

void CachedImageInfoRunner::run() {
    if (!_imagePaths.size()) {
        emit finished(this);
        return;
    }
    QElapsedTimer t;
    t.start();
    QStringList notFound;
    QList<ImageInfo> results;

    PersistentImageCache::retrieveImagesInfo(_imagePaths, results, notFound);

    for (ImageInfo &info : results) {
        if (info.path == _imagePaths.last()) {
            info.isLast = true;
        }
        info.isFromEmbeddedView = _isFromEmbeddedView;
    }

    // qDebug() << "ZZ FINISHED CACHE RETRIEVAL" << _imagePaths.size() << ":" << t.restart() << "ms" << ", not found" << notFound;

    emit cachedImageInfoRetrieved(results, notFound, _isFromEmbeddedView, _imagePaths.last());
    emit finished(this);
}
