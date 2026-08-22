#include "ImageInfoReadRunner.h"
#include "PersistentDerivedImageCache.h"
#include "ThumbnailLoader.h"

#include <QThread>

#include <utility>

ImageInfoReadRunner::ImageInfoReadRunner(const QString &path, bool isLast, bool isFromEmbeddedView, bool isFromScanner,
                                         int directOpenGeneration,
                                         bool highPriority,
                                         QString requestNamespace,
                                         QString sourceVersionToken,
                                         ZoinGallery::ImageSourceDescriptor source,
                                         QSharedPointer<ZoinGallery::ImageSourceProvider> provider,
                                         bool readDerivedMetadataCache,
                                         bool writeDerivedMetadataCache)
    : _path(path), _isLast(isLast), _isFromEmbeddedView(isFromEmbeddedView), _isFromScanner(isFromScanner),
      _directOpenGeneration(directOpenGeneration),
      _highPriority(highPriority),
      _requestNamespace(std::move(requestNamespace)),
      _sourceVersionToken(std::move(sourceVersionToken)),
      _source(std::move(source)),
      _provider(std::move(provider)),
      _cancellation(QSharedPointer<ZoinGallery::ImageSourceCancellation>::create()),
      _readDerivedMetadataCache(readDerivedMetadataCache),
      _writeDerivedMetadataCache(writeDerivedMetadataCache) {
}

void ImageInfoReadRunner::run() {
    ImageInfo result{
        .path = _path,
        .source = _source,
        .sourceVersionToken = _sourceVersionToken,
        .isLast = _isLast,
        .isFromEmbeddedView = _isFromEmbeddedView,
        .isFromScanner = _isFromScanner,
        .directOpenGeneration = _directOpenGeneration,
        .highPriority = _highPriority,
        .requestNamespace = _requestNamespace,
    };

    QSharedPointer<ZoinGallery::ImageSourceLease> lease;
    if (_source.isValid()) {
        result.path = _source.runtimeIdentity();
        result.fileSize = _source.size;
        if (_readDerivedMetadataCache &&
            PersistentDerivedImageCache::retrieveMetadata(result)) {
            emit imageInfoReady(result);
            emit finished(this);
            return;
        }
        lease = _provider
            ? _provider->materialize(_source, _cancellation)
            : QSharedPointer<ZoinGallery::ImageSourceLease>();
        if (_cancellation->isCanceled()) {
            emit finished(this);
            return;
        }
        if (!lease) {
            // Transport failure is retryable. Publish the identity/version so
            // the bounded planner releases its slot, but distinguish it from
            // a successfully opened corrupt/unsupported file.
            result.path = _source.runtimeIdentity();
            result.sourceAccessFailed = true;
            emit imageInfoReady(result);
            emit finished(this);
            return;
        }
        result.path = lease->localPath();
    }

    const bool metadataRead = ThumbnailLoader::readMetadata(result);

    if (_source.isValid()) {
        // Never leak a temporary materialized path into catalog identity or a
        // later cache key. Decode stages acquire their own shared lease.
        result.path = _source.runtimeIdentity();
        if (_writeDerivedMetadataCache && metadataRead) {
            PersistentDerivedImageCache::storeMetadata(result);
        }
    }

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
