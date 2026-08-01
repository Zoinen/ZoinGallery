#include "ViewerImageCache.h"

#include <QReadLocker>
#include <QWriteLocker>

#include <utility>

ViewerImageCache::ViewerImageCache(
    QString idPrefix,
    QSharedPointer<ProviderImageStore> providerImageStore)
    : _idPrefix(std::move(idPrefix)),
      _providerImageStore(std::move(providerImageStore)) {
}

ImageDecodeRequest ViewerImageCache::makeRequest(
    const ImageInfo &info, const QSize &originalSize,
    const QSize &viewerSize) {
    const bool fitToViewerRequest =
        viewerSize.width() > 0 && viewerSize.height() > 0;
    QSize targetSize = originalSize;
    if (fitToViewerRequest && targetSize.isValid() &&
        (targetSize.width() > viewerSize.width() ||
         targetSize.height() > viewerSize.height())) {
        targetSize = targetSize.scaled(viewerSize, Qt::KeepAspectRatio);
    }

    return ImageDecodeRequest{
        .info = info,
        .targetSize = targetSize,
        .viewerRequest = true,
        .checkCache = info.isCached,
        .fitToViewerRequest = fitToViewerRequest,
    };
}

ViewerImageCache::RequestPlan ViewerImageCache::planRequest(
    const QList<ImageFile *> &items, int currentIndex, const QSize &viewerSize,
    int prefetchCount) {
    RequestPlan result;
    if (currentIndex < 0 || currentIndex >= items.size()) {
        return result;
    }

    const ImageDecodeRequest currentRequest =
        requestForItem(items[currentIndex], viewerSize);
    result.cachedImages = cachedImagesForRequest(currentRequest);

    int imagesChecked = 0;
    bool hitStart = false;
    bool hitEnd = false;
    for (int counter = 0;
         imagesChecked < prefetchCount && !(hitStart && hitEnd); ++counter) {
        const int index = counter % 2 == 0
            ? currentIndex + counter / 2
            : currentIndex - (counter + 1) / 2;
        if (index < 0) {
            hitStart = true;
        }
        if (index >= items.size()) {
            hitEnd = true;
        }
        if (index < 0 || index >= items.size() || !items[index]->isImage()) {
            continue;
        }

        ++imagesChecked;
        const ImageDecodeRequest request =
            requestForItem(items[index], viewerSize);
        if (!request.targetSize.isValid()) {
            continue;
        }

        if (!needsDecode(request)) {
            continue;
        }

        result.decodeRequests.append(request);
    }

    return result;
}

ViewerImageCache::StoredImage ViewerImageCache::storeDecodedImage(
    const ImageDecodeRequest &request, const QImage &image,
    const DecodedImageInfo &decodedInfo) {
    StoredImage result;
    if (!request.viewerRequest) {
        return result;
    }

    const bool fullSize = isFullSizeRequest(request);
    QWriteLocker locker(&_lock);
    if (image.isNull()) {
        return result;
    }

    QHash<QString, Entry> &cache =
        fullSize ? _fullSizeImages : _viewerImages;
    QHash<QString, QString> &idToPath =
        fullSize ? _fullSizeIdToPath : _viewerIdToPath;
    auto cacheIt = cache.find(request.info.path);
    if (cacheIt != cache.end() && !cacheIt->image.isNull()) {
        const bool incomingVersionKnown =
            request.info.lastModified.isValid() ||
            request.info.fileSize >= 0;
        const bool sameSourceVersion =
            (!request.info.lastModified.isValid() ||
             (cacheIt->sourceLastModified.isValid() &&
              request.info.lastModified ==
                  cacheIt->sourceLastModified)) &&
            (request.info.fileSize < 0 ||
             (cacheIt->sourceFileSize >= 0 &&
              request.info.fileSize == cacheIt->sourceFileSize));
        const bool sourceVersionReplacement =
            incomingVersionKnown && !sameSourceVersion;
        const bool sameSize =
            cacheIt->image.size() == image.size();
        const bool equalSizeSourceReplacement =
            sameSize && !decodedInfo.isFromCache;
        const bool qualityDowngrade =
            !cacheIt->decodedInfo.isFromCache &&
            decodedInfo.isFromCache;
        const bool existingCoversIncoming =
            covers(cacheIt->image.size(), image.size());

        if (!sourceVersionReplacement &&
            ((qualityDowngrade &&
              satisfies(cacheIt.value(), request.targetSize)) ||
             (existingCoversIncoming &&
              !equalSizeSourceReplacement))) {
            return result;
        }
    }

    if (cacheIt != cache.end() && !cacheIt->imageId.isEmpty()) {
        idToPath.remove(cacheIt->imageId);
        _providerImageStore->remove(cacheIt->imageId);
    }

    const QString imageId = nextImageId();
    cache[request.info.path] = Entry{
        .image = image,
        .imageId = imageId,
        .requestedSize = request.targetSize,
        .decodedInfo = decodedInfo,
        .sourceLastModified = request.info.lastModified,
        .sourceFileSize = request.info.fileSize,
    };
    idToPath.insert(imageId, request.info.path);
    _providerImageStore->publish(imageId, image);

    result.accepted = true;
    // Cache tier and presentation tier intentionally differ here: a native
    // small image stays in the full-size cache, but Fit can display it through
    // the synchronous viewer layer without the delayed full-size transition.
    const bool presentAsFitImage =
        !fullSize || request.fitToViewerRequest;
    result.level = presentAsFitImage ? 1 : 2;
    result.url = QStringLiteral("image://") +
        (presentAsFitImage ? QStringLiteral("thumbnails/") :
                             QStringLiteral("async/")) +
        imageId;
    return result;
}

QList<QPair<QString, int>> ViewerImageCache::cachedImagesForPath(
    const QString &path, bool includeFullSize) const {
    return cachedImagesForPath(path, includeFullSize, false);
}

QList<QPair<QString, int>> ViewerImageCache::cachedImagesForRequest(
    const ImageDecodeRequest &request) const {
    return cachedImagesForPath(
        request.info.path, isFullSizeRequest(request),
        request.fitToViewerRequest);
}

QList<QPair<QString, int>> ViewerImageCache::cachedImagesForPath(
    const QString &path, bool includeFullSize,
    bool presentFullSizeAsFitImage) const {
    QReadLocker locker(&_lock);
    QList<QPair<QString, int>> result;
    const auto viewerIt = _viewerImages.constFind(path);
    if (viewerIt != _viewerImages.constEnd() && !viewerIt->image.isNull()) {
        result.append({
            QStringLiteral("image://thumbnails/") + viewerIt->imageId, 1
        });
    }

    const auto fullSizeIt = _fullSizeImages.constFind(path);
    if (includeFullSize && fullSizeIt != _fullSizeImages.constEnd() &&
        !fullSizeIt->image.isNull()) {
        result.append({
            QStringLiteral("image://") +
                (presentFullSizeAsFitImage
                    ? QStringLiteral("thumbnails/")
                    : QStringLiteral("async/")) +
                fullSizeIt->imageId,
            presentFullSizeAsFitImage ? 1 : 2
        });
    }
    return result;
}

QString ViewerImageCache::bestImageUrl(const ImageFile *item) const {
    if (!item) {
        return QString();
    }

    const QString path = item->fullPath();
    const QString fallbackUrl = item->imageIdUrl();
    QReadLocker locker(&_lock);
    const auto fullSizeIt = _fullSizeImages.constFind(path);
    if (fullSizeIt != _fullSizeImages.constEnd() &&
        !fullSizeIt->image.isNull()) {
        return QStringLiteral("image://async/") + fullSizeIt->imageId;
    }

    const auto viewerIt = _viewerImages.constFind(path);
    if (viewerIt != _viewerImages.constEnd() && !viewerIt->image.isNull()) {
        return QStringLiteral("image://thumbnails/") + viewerIt->imageId;
    }
    return fallbackUrl;
}

QImage ViewerImageCache::viewerImageForId(const QString &imageId) const {
    QReadLocker locker(&_lock);
    const auto pathIt = _viewerIdToPath.constFind(imageId);
    return pathIt == _viewerIdToPath.constEnd()
        ? QImage()
        : _viewerImages.value(pathIt.value()).image;
}

QImage ViewerImageCache::fullSizeImageForId(const QString &imageId) const {
    QReadLocker locker(&_lock);
    const auto pathIt = _fullSizeIdToPath.constFind(imageId);
    return pathIt == _fullSizeIdToPath.constEnd()
        ? QImage()
        : _fullSizeImages.value(pathIt.value()).image;
}

ViewerImageCache::Entry ViewerImageCache::entryForPath(
    const QString &path, bool fullSize) const {
    QReadLocker locker(&_lock);
    return (fullSize ? _fullSizeImages : _viewerImages).value(path);
}

bool ViewerImageCache::needsDecode(
    const ImageDecodeRequest &request) const {
    return needsDecode(request.info, request.targetSize,
                       isFullSizeRequest(request));
}

bool ViewerImageCache::needsDecode(
    const ImageInfo &info, const QSize &targetSize, bool fullSize) const {
    QReadLocker locker(&_lock);
    const QHash<QString, Entry> &cache =
        fullSize ? _fullSizeImages : _viewerImages;
    const auto it = cache.constFind(info.path);
    const bool sameVersion = it != cache.constEnd() &&
        (!info.lastModified.isValid() ||
         (it->sourceLastModified.isValid() &&
          info.lastModified == it->sourceLastModified)) &&
        (info.fileSize < 0 ||
         (it->sourceFileSize >= 0 &&
          info.fileSize == it->sourceFileSize));
    return it == cache.constEnd() ||
        !sameVersion ||
        !satisfies(it.value(), targetSize) ||
        it->decodedInfo.isFromCache;
}

qsizetype ViewerImageCache::viewerImageCount() const {
    QReadLocker locker(&_lock);
    return _viewerImages.size();
}

qsizetype ViewerImageCache::fullSizeImageCount() const {
    QReadLocker locker(&_lock);
    return _fullSizeImages.size();
}

void ViewerImageCache::removeIncomplete(const QString &path) {
    QWriteLocker locker(&_lock);
    auto viewerIt = _viewerImages.find(path);
    if (viewerIt != _viewerImages.end() && viewerIt->image.isNull()) {
        removeEntryLocked(path, false);
    }
    auto fullSizeIt = _fullSizeImages.find(path);
    if (fullSizeIt != _fullSizeImages.end() &&
        fullSizeIt->image.isNull()) {
        removeEntryLocked(path, true);
    }
}

void ViewerImageCache::remove(const QString &path) {
    QWriteLocker locker(&_lock);
    removeEntryLocked(path, false);
    removeEntryLocked(path, true);
}

void ViewerImageCache::clear() {
    QWriteLocker locker(&_lock);
    QStringList imageIds;
    imageIds.reserve(_viewerImages.size() + _fullSizeImages.size());
    for (const Entry &entry : std::as_const(_viewerImages)) {
        imageIds.append(entry.imageId);
    }
    for (const Entry &entry : std::as_const(_fullSizeImages)) {
        imageIds.append(entry.imageId);
    }
    _providerImageStore->remove(imageIds);
    _viewerImages.clear();
    _fullSizeImages.clear();
    _viewerIdToPath.clear();
    _fullSizeIdToPath.clear();
}

ImageDecodeRequest ViewerImageCache::requestForItem(
    const ImageFile *item, const QSize &viewerSize) {
    if (!item) {
        return {};
    }
    return makeRequest(item->info(), item->fullSize(), viewerSize);
}

bool ViewerImageCache::isFullSizeRequest(
    const ImageDecodeRequest &request) {
    return request.targetSize.isValid() &&
           (request.targetSize == request.info.imageSize ||
            request.targetSize ==
                rotateToOrientation(request.info.imageSize,
                                    request.info.orientation));
}

bool ViewerImageCache::satisfies(const Entry &entry,
                                 const QSize &targetSize) {
    return !entry.image.isNull() && covers(entry.image.size(), targetSize);
}

bool ViewerImageCache::covers(const QSize &size,
                              const QSize &targetSize) {
    return targetSize.isValid() && size.width() >= targetSize.width() &&
        size.height() >= targetSize.height();
}

void ViewerImageCache::removeEntry(const QString &path, bool fullSize) {
    QWriteLocker locker(&_lock);
    removeEntryLocked(path, fullSize);
}

void ViewerImageCache::removeEntryLocked(
    const QString &path, bool fullSize) {
    QHash<QString, Entry> &cache =
        fullSize ? _fullSizeImages : _viewerImages;
    QHash<QString, QString> &idToPath =
        fullSize ? _fullSizeIdToPath : _viewerIdToPath;
    const auto it = cache.find(path);
    if (it == cache.end()) {
        return;
    }
    if (!it->imageId.isEmpty()) {
        idToPath.remove(it->imageId);
        _providerImageStore->remove(it->imageId);
    }
    cache.erase(it);
}

QString ViewerImageCache::nextImageId() {
    return _idPrefix + QString::number(_lastImageId++);
}
