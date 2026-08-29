#include "ImageInfoReadRunner.h"
#include "PersistentDerivedImageCache.h"
#include "ThumbnailLoader.h"

#include <ZoinGallery/MediaTimingTrace.h>

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
    QVariantMap timingFields =
        ZoinGallery::MediaTimingTrace::sourceFields(_source);
    timingFields.insert(QStringLiteral("path"), _path);
    timingFields.insert(QStringLiteral("highPriority"), _highPriority);
    timingFields.insert(QStringLiteral("requestNamespace"),
                        _requestNamespace);
    ZoinGallery::MediaTimingTrace::Span timingSpan(
        QStringLiteral("qt.gallery.metadata"), timingFields);
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
            timingSpan.set(QStringLiteral("outcome"),
                           QStringLiteral("derived-cache-hit"));
            timingSpan.set(QStringLiteral("imageWidth"),
                           result.imageSize.width());
            timingSpan.set(QStringLiteral("imageHeight"),
                           result.imageSize.height());
            emit imageInfoReady(result);
            emit finished(this);
            return;
        }
        lease = _provider
            ? _provider->materialize(_source, _cancellation)
            : QSharedPointer<ZoinGallery::ImageSourceLease>();
        if (_cancellation->isCanceled()) {
            timingSpan.set(QStringLiteral("outcome"),
                           QStringLiteral("cancelled"));
            emit finished(this);
            return;
        }
        if (!lease) {
            // Transport failure is retryable. Publish the identity/version so
            // the bounded planner releases its slot, but distinguish it from
            // a successfully opened corrupt/unsupported file.
            result.path = _source.runtimeIdentity();
            result.sourceAccessFailed = true;
            timingSpan.set(QStringLiteral("outcome"),
                           QStringLiteral("materialize-failed"));
            emit imageInfoReady(result);
            emit finished(this);
            return;
        }
        result.path = lease->localPath();
    }

    bool metadataRead = false;
    {
        ZoinGallery::MediaTimingTrace::Span readSpan(
            QStringLiteral("qt.gallery.metadata.decode"), timingFields);
        metadataRead = ThumbnailLoader::readMetadata(result);
        readSpan.set(QStringLiteral("ok"), metadataRead);
        readSpan.set(QStringLiteral("imageWidth"),
                     result.imageSize.width());
        readSpan.set(QStringLiteral("imageHeight"),
                     result.imageSize.height());
    }

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

    timingSpan.set(QStringLiteral("outcome"),
                   metadataRead ? QStringLiteral("ok")
                                : QStringLiteral("decode-failed"));
    timingSpan.set(QStringLiteral("imageWidth"), result.imageSize.width());
    timingSpan.set(QStringLiteral("imageHeight"), result.imageSize.height());
    emit imageInfoReady(result);
    emit finished(this);
}

bool ImageInfoReadRunner::isEmbeddedRequest() const {
    return _isFromEmbeddedView;
}
