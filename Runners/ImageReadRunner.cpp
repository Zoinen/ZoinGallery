#include "ImageReadRunner.h"
#include "ThumbnailLoader.h"

#include <utility>

ImageReadRunner::ImageReadRunner(
    const ImageDecodeRequest &request,
    QSharedPointer<ZoinGallery::ImageSourceProvider> provider)
    : _request(request),
      _provider(std::move(provider)),
      _cancellation(QSharedPointer<ZoinGallery::ImageSourceCancellation>::create()),
      _derivedLookupGate(
          PersistentDerivedImageCache::joinLookup(request)) {
}

void ImageReadRunner::run() {
    if (PersistentDerivedImageCache::waitForLookup(
            _derivedLookupGate, _cancellation)) {
        emit finished(this);
        return;
    }
    if (_cancellation->isCanceled()) {
        emit finished(this);
        return;
    }
    ImageDecodeRequest materializedRequest = _request;
    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
    if (_request.info.source.isValid()) {
        lease = _provider
            ? _provider->materialize(_request.info.source, _cancellation)
            : QSharedPointer<ZoinGallery::ImageSourceLease>();
        if (!lease || _cancellation->isCanceled()) {
            if (!_cancellation->isCanceled()) {
                ImageDecodeRequest failedRequest = _request;
                failedRequest.sourceAccessFailed = true;
                emit imageReadFailed(failedRequest);
            }
            emit finished(this);
            return;
        }
        materializedRequest.info.path = lease->localPath();
    }
    ImageData imageData(materializedRequest);
    imageData.sourceLease = lease;
    if (ThumbnailLoader::readImage(imageData)) {
        emit imageReadReady(imageData);
    }
    else {
        emit imageReadFailed(_request);
    }
    emit finished(this);
}

QString ImageReadRunner::path() const {
    return _request.info.sourceIdentity();
}

bool ImageReadRunner::isViewerRequest() const {
    return _request.viewerRequest && !_request.backgroundViewerRequest;
}
