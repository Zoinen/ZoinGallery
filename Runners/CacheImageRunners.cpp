#include "CacheImageRunners.h"

#include "PersistentImageCache.h"

#include <utility>

CachedImageRetrieveRunner::CachedImageRetrieveRunner(const ImageDecodeRequest &request, bool validateRequestVersion)
    : _request(request), _validateRequestVersion(validateRequestVersion),
      _derivedLookupGate(
          PersistentDerivedImageCache::beginLookup(request)) {
}

void CachedImageRetrieveRunner::run() {
    // QImage img(1, 1, QImage::Format_ARGB32);
    QImage img = PersistentImageCache::retrieveImage(_request, _validateRequestVersion);
    PersistentDerivedImageCache::completeLookup(_derivedLookupGate,
                                                 !img.isNull());
    // qDebug() << "XX REQ" << _request.info.path << img << _request.targetSize;
    if (!img.isNull()) {
        emit cachedThumbnailRetrieved(
            _request, img,
            DecodedImageInfo{
                .isFromCache = true,
                .isAuthoritativeDerivedCache =
                    _request.info.source.isValid(),
            });
    }
    emit finished(this);
}


CachedImageStoreRunner::CachedImageStoreRunner(const ImageInfo &imageInfo, const QByteArray &imageData)
    : _imageInfo(imageInfo), _imageData(imageData) {
}

void CachedImageStoreRunner::run() {
    if (!isCanceled()) {
        PersistentImageCache::storeImage(_imageInfo, _imageData);
    }
    emit finished(this);
}




CachedImageInfoRunner::CachedImageInfoRunner(const QStringList &imagePaths, bool isFromEmbeddedView,
                                             bool validateSource, int directOpenGeneration,
                                             bool highPriority,
                                             QString requestNamespace,
                                             QString sourceVersionToken)
    : _imagePaths(imagePaths), _isFromEmbeddedView(isFromEmbeddedView),
      _validateSource(validateSource), _directOpenGeneration(directOpenGeneration),
      _highPriority(highPriority),
      _requestNamespace(std::move(requestNamespace)),
      _sourceVersionToken(std::move(sourceVersionToken)) {
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

    PersistentImageCache::retrieveImagesInfo(_imagePaths, results, notFound, _validateSource);

    for (ImageInfo &info : results) {
        if (info.path == _imagePaths.last()) {
            info.isLast = true;
        }
        info.isFromEmbeddedView = _isFromEmbeddedView;
        info.directOpenGeneration = _directOpenGeneration;
        info.highPriority = _highPriority;
        info.requestNamespace = _requestNamespace;
        info.sourceVersionToken = _sourceVersionToken;
    }

    // qDebug() << "ZZ FINISHED CACHE RETRIEVAL" << _imagePaths.size() << ":" << t.restart() << "ms" << ", not found" << notFound;

    emit cachedImageInfoRetrieved(results, notFound, _isFromEmbeddedView,
                                  _imagePaths.last(), _directOpenGeneration,
                                  _highPriority, _requestNamespace,
                                  _sourceVersionToken);
    emit finished(this);
}
