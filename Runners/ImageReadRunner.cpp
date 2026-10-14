#include "ImageReadRunner.h"
#include "ThumbnailLoader.h"
#include "VectorSource.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <utility>
#include <QFile>

ImageReadRunner::ImageReadRunner(
    const ImageDecodeRequest &request,
    QSharedPointer<ZoinGallery::ImageSourceProvider> provider)
    : _request(request),
      _provider(std::move(provider)),
      _cancellation(QSharedPointer<ZoinGallery::ImageSourceCancellation>::create()),
      _derivedLookupGate(
          isVectorSource(request.info) ? PersistentDerivedImageCache::LookupGate{} : PersistentDerivedImageCache::joinLookup(request)) {
}

void ImageReadRunner::run() {
    QVariantMap timingFields =
        ZoinGallery::MediaTimingTrace::sourceFields(_request.info.source);
    timingFields.insert(QStringLiteral("sourceIdentity"),
                        _request.info.sourceIdentity());
    timingFields.insert(QStringLiteral("targetWidth"),
                        _request.targetSize.width());
    timingFields.insert(QStringLiteral("targetHeight"),
                        _request.targetSize.height());
    timingFields.insert(QStringLiteral("viewer"), _request.viewerRequest);
    timingFields.insert(QStringLiteral("highPriority"),
                        _request.highPriority);
    ZoinGallery::MediaTimingTrace::Span timingSpan(
        QStringLiteral("qt.gallery.image_read"), timingFields);
    const bool vector = isVectorSource(_request.info);
    bool derivedLookupWon = false;
    {
        ZoinGallery::MediaTimingTrace::Span lookupSpan(
            QStringLiteral("qt.gallery.derived_lookup_wait"), timingFields);
        derivedLookupWon = !vector && PersistentDerivedImageCache::waitForLookup(
            _derivedLookupGate, _cancellation);
        lookupSpan.set(QStringLiteral("satisfied"), derivedLookupWon);
    }
    if (derivedLookupWon) {
        timingSpan.set(QStringLiteral("outcome"),
                       _request.info.source.isValid()
                           ? QStringLiteral("derived-cache-satisfied")
                           : QStringLiteral("cache-satisfied"));
        emit finished(this);
        return;
    }
    if (_cancellation->isCanceled()) {
        timingSpan.set(QStringLiteral("outcome"), QStringLiteral("cancelled"));
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
            timingSpan.set(QStringLiteral("outcome"),
                           _cancellation->isCanceled()
                               ? QStringLiteral("cancelled")
                               : QStringLiteral("materialize-failed"));
            emit finished(this);
            return;
        }
        materializedRequest.info.path = lease->localPath();
    }
    ImageData imageData(materializedRequest);
    imageData.sourceLease = lease;
    if (vector) {
        QFile backing(materializedRequest.info.path);
        if (!backing.open(QIODevice::ReadOnly)) {
            auto failed = _request;
            failed.sourceAccessFailed = true;
            if (!_cancellation->isCanceled()) emit imageReadFailed(failed);
            emit finished(this);
            return;
        }
        backing.close();
        imageData.vectorSource = acquireVectorSource(_request.info, lease);
        if (!_cancellation->isCanceled()) {
            if (imageData.vectorSource) emit imageReadReady(imageData);
            else emit imageReadFailed(_request);
        }
        emit finished(this);
        return;
    }
    bool read = false;
    {
        ZoinGallery::MediaTimingTrace::Span compressedReadSpan(
            QStringLiteral("qt.gallery.compressed_read"), timingFields);
        read = ThumbnailLoader::readImage(imageData);
        compressedReadSpan.set(QStringLiteral("ok"), read);
        compressedReadSpan.set(QStringLiteral("compressedBytes"),
                               imageData.data.size());
    }
    if (read) {
        timingSpan.set(QStringLiteral("outcome"), QStringLiteral("ok"));
        timingSpan.set(QStringLiteral("compressedBytes"),
                       imageData.data.size());
        emit imageReadReady(imageData);
    }
    else {
        // A materialized external path can disappear or become unreadable
        // while the VFS/media transport is reconnecting. Treat that as a
        // retryable source failure; otherwise the catalog sees a terminal
        // decode failure and leaves the thumbnail empty for this generation.
        ImageDecodeRequest failedRequest = _request;
        if (_request.info.source.isValid()) {
            failedRequest.sourceAccessFailed = true;
        }
        timingSpan.set(QStringLiteral("outcome"),
                       QStringLiteral("compressed-read-failed"));
        emit imageReadFailed(failedRequest);
    }
    emit finished(this);
}

QString ImageReadRunner::path() const {
    return _request.info.sourceIdentity();
}

bool ImageReadRunner::isViewerRequest() const {
    return _request.viewerRequest && !_request.backgroundViewerRequest;
}
