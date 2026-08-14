#include "ViewerImageCache.h"

#include <QReadLocker>
#include <QWriteLocker>

#include <algorithm>
#include <limits>
#include <utility>

ViewerImageCache::ViewerImageCache(
    QString idPrefix,
    QSharedPointer<ProviderImageStore> providerImageStore,
    QString thumbnailProviderName,
    QString asyncProviderName,
    qint64 byteBudget)
    : ViewerImageCache(std::move(idPrefix),
                       std::move(providerImageStore),
                       std::move(thumbnailProviderName),
                       std::move(asyncProviderName),
                       byteBudget, byteBudget) {
}

ViewerImageCache::ViewerImageCache(
    QString idPrefix,
    QSharedPointer<ProviderImageStore> providerImageStore,
    QString thumbnailProviderName,
    QString asyncProviderName,
    qint64 fitByteBudget,
    qint64 nativeByteBudget)
    : _idPrefix(std::move(idPrefix)),
      _thumbnailProviderName(std::move(thumbnailProviderName)),
      _asyncProviderName(std::move(asyncProviderName)),
      _providerImageStore(std::move(providerImageStore)),
      _fitByteBudget(fitByteBudget < 0
                         ? std::numeric_limits<qint64>::max()
                         : qMax<qint64>(0, fitByteBudget)),
      _nativeByteBudget(nativeByteBudget < 0
                            ? std::numeric_limits<qint64>::max()
                            : qMax<qint64>(0, nativeByteBudget)),
      _boundedRetention(fitByteBudget >= 0 &&
                        nativeByteBudget >= 0) {
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

    QList<ImageFile *> plannedItems;
    plannedItems.reserve(qMax(0, prefetchCount));
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
        plannedItems.append(items[index]);
    }

    QList<QString> plannedPaths;
    plannedPaths.reserve(plannedItems.size());
    for (const ImageFile *item : std::as_const(plannedItems)) {
        const QString path = item ? item->info().path : QString();
        if (!path.isEmpty() && !plannedPaths.contains(path)) {
            plannedPaths.append(path);
        }
    }
    QList<ImageDecodeRequest> plannedRequests;
    plannedRequests.reserve(plannedItems.size());
    for (const ImageFile *item : std::as_const(plannedItems)) {
        const ImageDecodeRequest request = requestForItem(item, viewerSize);
        if (request.targetSize.isValid()) {
            plannedRequests.append(request);
        }
    }

    {
        QWriteLocker locker(&_lock);
        updateRetentionPlanLocked(currentRequest.info.path, plannedPaths,
                                  prefetchCount);
        for (const ImageDecodeRequest &request :
             std::as_const(plannedRequests)) {
            recordPlannedTargetLocked(request);
        }
    }

    result.cachedImages = cachedImagesForRequest(currentRequest);
    for (const ImageDecodeRequest &request :
         std::as_const(plannedRequests)) {
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
    const QSize latestPlannedTarget =
        latestPlannedTargetLocked(request);
    const bool hasPlannedTarget = latestPlannedTarget.isValid();
    const QSize presentationTarget = hasPlannedTarget
        ? latestPlannedTarget : request.targetSize;
    if (hasPlannedTarget &&
        request.targetSize != latestPlannedTarget &&
        !covers(image.size(), latestPlannedTarget)) {
        // Resizing can supersede an in-flight decode without cancelling it.
        // A stale result is still useful when its actual frame covers the
        // latest planned target (for example, a large decode finishing after
        // the viewer shrank). Never publish a stale undersized result after
        // the viewer grew.
        return result;
    }
    if (_hasRetentionPlan &&
        !_retentionRanks.contains(request.info.path)) {
        // A result from an obsolete navigation generation must not enter the
        // cache after the active predecode sequence has moved elsewhere.
        // Frames which completed before that move remain reusable until the
        // configured byte budget applies pressure.
        return result;
    }
    if (_currentPath.isEmpty()) {
        // Direct-open decoding can populate the cache before its first
        // planRequest(). Its first frame is the only reliable current-image
        // signal available at that stage.
        _currentPath = request.info.path;
    }

    QHash<QString, Entry> &cache =
        fullSize ? _fullSizeImages : _viewerImages;
    QHash<QString, QString> &idToPath =
        fullSize ? _fullSizeIdToPath : _viewerIdToPath;
    auto cacheIt = cache.find(request.info.path);
    if (cacheIt != cache.end() && !cacheIt->image.isNull()) {
        const bool incomingVersionKnown =
            request.info.lastModified.isValid() ||
            request.info.fileSize >= 0 ||
            request.info.sourceVersionToken != 0;
        const bool sameSourceVersion =
            (request.info.sourceVersionToken == 0 ||
             (cacheIt->sourceVersionToken != 0 &&
              request.info.sourceVersionToken ==
                  cacheIt->sourceVersionToken)) &&
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
            if (request.fitToViewerRequest) {
                markFitPreparedLocked(cacheIt.value(), fullSize);
                pruneToBudgetsLocked();
            }
            return result;
        }
    }

    if (cacheIt != cache.end()) {
        subtractRetainedBytesLocked(cacheIt.value(), fullSize);
        if (!cacheIt->imageId.isEmpty()) {
            idToPath.remove(cacheIt->imageId);
            _providerImageStore->remove(cacheIt->imageId);
        }
        cache.erase(cacheIt);
    }

    const QString imageId = nextImageId(request);
    const Entry entry{
        .image = image,
        .imageId = imageId,
        .requestedSize = request.targetSize,
        .decodedInfo = decodedInfo,
        .sourceLastModified = request.info.lastModified,
        .sourceFileSize = request.info.fileSize,
        .sourceVersionToken = request.info.sourceVersionToken,
        .fitPrepared = request.fitToViewerRequest || !fullSize,
    };
    cache.insert(request.info.path, entry);
    addRetainedBytesLocked(entry, fullSize);
    idToPath.insert(imageId, request.info.path);
    _providerImageStore->publish(imageId, image);

    pruneToBudgetsLocked();
    const auto retainedIt = cache.constFind(request.info.path);
    if (retainedIt == cache.constEnd() || retainedIt->imageId != imageId) {
        return result;
    }

    result.accepted = true;
    result.presentable = satisfies(retainedIt.value(),
                                   presentationTarget);
    if (!result.presentable) {
        // Keep the provisional cache frame for a later, smaller viewport but
        // do not advertise it as a prepared Fit tier for this request.  The
        // masonry thumbnail remains the explicit level-0 fallback until the
        // source decode covering the presentation target arrives.
        return result;
    }
    // Cache tier and presentation tier intentionally differ here: a native
    // small image stays in the full-size cache, but Fit can display it through
    // the synchronous viewer layer without the delayed full-size transition.
    const bool presentAsFitImage =
        !fullSize || request.fitToViewerRequest;
    result.level = presentAsFitImage ? 1 : 2;
    result.url = QStringLiteral("image://") +
        (presentAsFitImage ? _thumbnailProviderName :
                             _asyncProviderName) + QLatin1Char('/') +
        imageId;
    return result;
}

QList<QPair<QString, int>> ViewerImageCache::cachedImagesForPath(
    const QString &path, bool includeFullSize) const {
    return cachedImagesForPath(path, includeFullSize, false);
}

QList<QPair<QString, int>> ViewerImageCache::imageSources(
    const ImageFile *item, const QSize &viewerSize) const {
    QList<QPair<QString, int>> result;
    if (!item) {
        return result;
    }

    if (!item->imageIdUrl().isEmpty()) {
        result.append({item->imageIdUrl(), 0});
    }

    const ImageDecodeRequest request = requestForItem(item, viewerSize);
    const auto cached = cachedImagesForRequest(request);
    for (const auto &source : cached) {
        if (source.first.isEmpty()) {
            continue;
        }
        const bool duplicate = std::any_of(
            result.cbegin(), result.cend(), [&source](const auto &existing) {
                return existing.first == source.first &&
                    existing.second == source.second;
            });
        if (!duplicate) {
            result.append(source);
        }
    }
    return result;
}

QList<QPair<QString, int>> ViewerImageCache::cachedImagesForRequest(
    const ImageDecodeRequest &request) const {
    // Fit presentation is coverage-based: an oversized frame decoded for an
    // earlier viewport remains immediately reusable, while an undersized frame
    // is withheld until a decode covering the current target completes. In
    // native mode the retained Fit frame intentionally remains available as
    // the base/fallback underneath the asynchronous full-resolution texture.
    const QSize minimumViewerSize = request.fitToViewerRequest
        ? request.targetSize : QSize();
    return cachedImagesForPath(
        request.info.path, isFullSizeRequest(request),
        request.fitToViewerRequest, minimumViewerSize,
        request.targetSize);
}

QList<QPair<QString, int>> ViewerImageCache::cachedImagesForPath(
    const QString &path, bool includeFullSize,
    bool presentFullSizeAsFitImage, const QSize &minimumViewerSize,
    const QSize &minimumFullSize) const {
    QReadLocker locker(&_lock);
    QList<QPair<QString, int>> result;
    const auto viewerIt = _viewerImages.constFind(path);
    if (viewerIt != _viewerImages.constEnd() &&
        !viewerIt->image.isNull() &&
        (!minimumViewerSize.isValid() ||
         satisfies(viewerIt.value(), minimumViewerSize))) {
        result.append({
            QStringLiteral("image://") + _thumbnailProviderName +
                QLatin1Char('/') + viewerIt->imageId, 1
        });
    }

    const auto fullSizeIt = _fullSizeImages.constFind(path);
    if (includeFullSize && fullSizeIt != _fullSizeImages.constEnd() &&
        !fullSizeIt->image.isNull() &&
        (!minimumFullSize.isValid() ||
         satisfies(fullSizeIt.value(), minimumFullSize))) {
        result.append({
            QStringLiteral("image://") +
                (presentFullSizeAsFitImage
                    ? _thumbnailProviderName
                    : _asyncProviderName) + QLatin1Char('/') +
                fullSizeIt->imageId,
            presentFullSizeAsFitImage ? 1 : 2
        });
    }
    return result;
}

QString ViewerImageCache::bestImageUrl(const ImageFile *item) const {
    const auto sources = imageSources(item);
    return sources.isEmpty() ? QString() : sources.constLast().first;
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
        (info.sourceVersionToken == 0 ||
         (it->sourceVersionToken != 0 &&
          info.sourceVersionToken == it->sourceVersionToken)) &&
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

void ViewerImageCache::recordPlannedTargetLocked(
    const ImageDecodeRequest &request) {
    QHash<QString, QSize> &targets = request.fitToViewerRequest
        ? _latestPlannedFitTargets : _latestPlannedNativeTargets;
    targets.insert(request.info.path, request.targetSize);
}

QSize ViewerImageCache::latestPlannedTargetLocked(
    const ImageDecodeRequest &request) const {
    const QHash<QString, QSize> &targets = request.fitToViewerRequest
        ? _latestPlannedFitTargets : _latestPlannedNativeTargets;
    const auto it = targets.constFind(request.info.path);
    return it == targets.constEnd() ? QSize() : it.value();
}

qsizetype ViewerImageCache::viewerImageCount() const {
    QReadLocker locker(&_lock);
    return _viewerImages.size();
}

qsizetype ViewerImageCache::fullSizeImageCount() const {
    QReadLocker locker(&_lock);
    return _fullSizeImages.size();
}

qint64 ViewerImageCache::retainedBytes() const {
    QReadLocker locker(&_lock);
    return _fitRetainedBytes + _nativeRetainedBytes;
}

qint64 ViewerImageCache::fitRetainedBytes() const {
    QReadLocker locker(&_lock);
    return _fitRetainedBytes;
}

qint64 ViewerImageCache::nativeRetainedBytes() const {
    QReadLocker locker(&_lock);
    return _nativeRetainedBytes;
}

qint64 ViewerImageCache::byteBudget() const {
    QReadLocker locker(&_lock);
    return _fitByteBudget;
}

qint64 ViewerImageCache::fitByteBudget() const {
    QReadLocker locker(&_lock);
    return _fitByteBudget;
}

qint64 ViewerImageCache::nativeByteBudget() const {
    QReadLocker locker(&_lock);
    return _nativeByteBudget;
}

void ViewerImageCache::setByteBudget(qint64 byteBudget) {
    setByteBudgets(byteBudget, byteBudget);
}

void ViewerImageCache::setByteBudgets(qint64 fitByteBudget,
                                      qint64 nativeByteBudget) {
    QWriteLocker locker(&_lock);
    _boundedRetention = fitByteBudget >= 0 && nativeByteBudget >= 0;
    _fitByteBudget = fitByteBudget < 0
        ? std::numeric_limits<qint64>::max()
        : qMax<qint64>(0, fitByteBudget);
    _nativeByteBudget = nativeByteBudget < 0
        ? std::numeric_limits<qint64>::max()
        : qMax<qint64>(0, nativeByteBudget);
    if (!_boundedRetention) {
        _hasRetentionPlan = false;
        _retentionRanks.clear();
        _supplementalPaths.clear();
        _primaryPrefetchCount = 0;
    }
    pruneToBudgetsLocked();
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
    _latestPlannedFitTargets.remove(path);
    _latestPlannedNativeTargets.remove(path);
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
    _latestPlannedFitTargets.clear();
    _latestPlannedNativeTargets.clear();
    _retentionRanks.clear();
    _supplementalPaths.clear();
    _currentPath.clear();
    _primaryPrefetchCount = 0;
    _hasRetentionPlan = false;
    _fitRetainedBytes = 0;
    _nativeRetainedBytes = 0;
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

qint64 ViewerImageCache::entryByteSize(const Entry &entry) {
    return qMax<qint64>(
        0, static_cast<qint64>(entry.image.sizeInBytes()));
}

bool ViewerImageCache::usesFitBudget(const Entry &entry,
                                     bool fullSize) {
    return !fullSize || entry.fitPrepared;
}

void ViewerImageCache::updateRetentionPlanLocked(
    const QString &currentPath, const QList<QString> &plannedPaths,
    int prefetchCount) {
    if (!_boundedRetention) {
        // Exact standalone parity: the plan still determines what to decode
        // next, but it must not invalidate frames decoded by earlier plans.
        _currentPath = currentPath;
        _hasRetentionPlan = false;
        return;
    }
    const bool supplementalRequest =
        _hasRetentionPlan && _primaryPrefetchCount > 1 &&
        prefetchCount <= 1 && currentPath != _currentPath;
    const bool narrowerRefresh =
        _hasRetentionPlan && currentPath == _currentPath &&
        prefetchCount < _primaryPrefetchCount;
    if (supplementalRequest) {
        // A transition may explicitly prepare a row just outside the regular
        // prefetch window. Keep at most two such transition frames without
        // replacing the authoritative navigation plan.
        if (!currentPath.isEmpty() &&
            !_retentionRanks.contains(currentPath)) {
            _supplementalPaths.removeAll(currentPath);
            _supplementalPaths.append(currentPath);
            while (_supplementalPaths.size() > 2) {
                _retentionRanks.remove(_supplementalPaths.takeFirst());
            }
            int nextRank = 0;
            for (auto it = _retentionRanks.cbegin();
                 it != _retentionRanks.cend(); ++it) {
                nextRank = qMax(nextRank, it.value() + 1);
            }
            _retentionRanks.insert(currentPath, nextRank);
        }
        pruneToBudgetsLocked();
        return;
    }
    if (narrowerRefresh) {
        // Metadata refresh for the current frame is not a new navigation
        // plan and must not discard its prefetched sequence.
        return;
    }

    // Metadata and viewport updates can rebuild the active prefetch plan
    // while an explicitly prepared transition frame is still decoding. Keep
    // those bounded supplemental paths across a refresh of the same current
    // image; otherwise the in-flight result is mistaken for stale work and
    // rejected by storeDecodedImage(). A real navigation changes
    // currentPath and intentionally starts with a clean supplemental set.
    const QList<QString> retainedSupplementalPaths =
        _hasRetentionPlan && currentPath == _currentPath &&
            prefetchCount > 1
        ? _supplementalPaths
        : QList<QString>();

    _hasRetentionPlan = true;
    _currentPath = currentPath;
    _primaryPrefetchCount = qMax(0, prefetchCount);
    _retentionRanks.clear();
    _supplementalPaths.clear();
    int rank = 0;
    for (const QString &path : plannedPaths) {
        if (!path.isEmpty() && !_retentionRanks.contains(path)) {
            _retentionRanks.insert(path, rank++);
        }
    }
    if (!currentPath.isEmpty()) {
        _retentionRanks.insert(currentPath, 0);
    }
    for (const QString &path : retainedSupplementalPaths) {
        if (path.isEmpty() || _retentionRanks.contains(path)) {
            continue;
        }
        _supplementalPaths.append(path);
        _retentionRanks.insert(path, rank++);
    }

    pruneToBudgetsLocked();
}

void ViewerImageCache::pruneToBudgetsLocked() {
    if (!_boundedRetention) {
        return;
    }
    // Fit and native frames deliberately have independent pressure. A single
    // oversized panorama may exceed the native budget while it is current,
    // but it must never consume or evict the prepared Fit sequence.
    pruneTierToBudgetLocked(true);
    pruneTierToBudgetLocked(false);
}

void ViewerImageCache::pruneTierToBudgetLocked(bool fitTier) {
    qint64 &retainedBytes = fitTier
        ? _fitRetainedBytes : _nativeRetainedBytes;
    const qint64 byteBudget = fitTier
        ? _fitByteBudget : _nativeByteBudget;
    while (retainedBytes > byteBudget) {
        QString evictionPath;
        bool evictionIsFullSize = false;
        int evictionRank = -1;
        qint64 evictionBytes = -1;

        const auto consider = [&](const QString &path, const Entry &entry,
                                  bool fullSize) {
            if (usesFitBudget(entry, fullSize) != fitTier) {
                return;
            }
            if (path == _currentPath) {
                return;
            }
            const int rank = _retentionRanks.value(
                path, std::numeric_limits<int>::max());
            const qint64 bytes = entryByteSize(entry);
            if (evictionPath.isEmpty() || rank > evictionRank ||
                (rank == evictionRank && bytes > evictionBytes) ||
                (rank == evictionRank && bytes == evictionBytes &&
                 fullSize && !evictionIsFullSize)) {
                evictionPath = path;
                evictionIsFullSize = fullSize;
                evictionRank = rank;
                evictionBytes = bytes;
            }
        };

        for (auto it = _viewerImages.cbegin();
             it != _viewerImages.cend(); ++it) {
            consider(it.key(), it.value(), false);
        }
        for (auto it = _fullSizeImages.cbegin();
             it != _fullSizeImages.cend(); ++it) {
            consider(it.key(), it.value(), true);
        }

        if (evictionPath.isEmpty()) {
            // The current frame is intentionally allowed to exceed the
            // budget: dropping it would blank the viewer during navigation.
            break;
        }
        removeEntryLocked(evictionPath, evictionIsFullSize);
    }
}

void ViewerImageCache::addRetainedBytesLocked(const Entry &entry,
                                               bool fullSize) {
    qint64 &retainedBytes = usesFitBudget(entry, fullSize)
        ? _fitRetainedBytes : _nativeRetainedBytes;
    retainedBytes += entryByteSize(entry);
}

void ViewerImageCache::subtractRetainedBytesLocked(const Entry &entry,
                                                    bool fullSize) {
    qint64 &retainedBytes = usesFitBudget(entry, fullSize)
        ? _fitRetainedBytes : _nativeRetainedBytes;
    retainedBytes = qMax<qint64>(
        0, retainedBytes - entryByteSize(entry));
}

void ViewerImageCache::markFitPreparedLocked(Entry &entry,
                                              bool fullSize) {
    if (!fullSize || entry.fitPrepared) {
        return;
    }
    subtractRetainedBytesLocked(entry, fullSize);
    entry.fitPrepared = true;
    addRetainedBytesLocked(entry, fullSize);
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
    subtractRetainedBytesLocked(it.value(), fullSize);
    cache.erase(it);
}

QString ViewerImageCache::nextImageId(
    const ImageDecodeRequest &request) {
    const int serial = _lastImageId++;
    if (request.info.sourceVersionToken == 0) {
        return _idPrefix + QString::number(serial);
    }
    return _idPrefix + QStringLiteral("v%1-s%2-%3")
        .arg(request.info.sourceVersionToken)
        .arg(request.info.fileSize)
        .arg(serial);
}
