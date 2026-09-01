#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

void ExternalCatalogModel::handleImageInfo(const ImageInfo &info) {
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.metadata.delivered"), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("requestNamespace"), info.requestNamespace},
            {QStringLiteral("sourceIdentity"), info.sourceIdentity()},
        });
    handleImageInfos({info});
}

void ExternalCatalogModel::handleImageInfos(
    const QList<ImageInfo> &infos) {
    if (_shutdown || infos.isEmpty()) {
        return;
    }
    ImageInfoBatchState state;
    for (const ImageInfo &info : infos) {
        applyImageInfoResult(info, state);
    }
    publishImageInfoBatch(state);
}

void ExternalCatalogModel::applyImageInfoResult(
    const ImageInfo &info, ImageInfoBatchState &state) {
    if (info.requestNamespace != _sessionId) {
        MediaTimingTrace::event(
            QStringLiteral("qt.gallery.metadata.rejected"), {
                {QStringLiteral("reason"), QStringLiteral("namespace")},
                {QStringLiteral("sessionId"), _sessionId},
                {QStringLiteral("requestNamespace"), info.requestNamespace},
                {QStringLiteral("sourceIdentity"), info.sourceIdentity()},
            });
        return;
    }
    state.acceptedNamespace = true;
    const QString sourceIdentity = info.sourceIdentity();
    const auto pending = _metadataPendingVersions.constFind(sourceIdentity);
    if (pending != _metadataPendingVersions.cend()
        && pending.value() == info.sourceVersionToken) {
        _metadataPendingVersions.remove(sourceIdentity);
    }
    const QList<int> rows = sourceRows(sourceIdentity);
    if (rows.isEmpty()) {
        MediaTimingTrace::event(
            QStringLiteral("qt.gallery.metadata.rejected"),
            MediaTimingTrace::mergedFields(
                MediaTimingTrace::sourceFields(info.source), {
                    {QStringLiteral("reason"), QStringLiteral("row")},
                    {QStringLiteral("sourceIdentity"), sourceIdentity},
                }));
        return;
    }

    QList<int> authorityRows;
    authorityRows.reserve(rows.size());
    for (const int row : rows) {
        const Entry &entry = loadedEntry(row);
        if (entry.image
            && entry.contentVersion == info.sourceVersionToken
            && sourceAuthorityMatches(entry.source, info.source)) {
            authorityRows.append(row);
        }
    }
    if (authorityRows.isEmpty()) {
        const Entry &entry = loadedEntry(rows.constFirst());
        MediaTimingTrace::event(
            QStringLiteral("qt.gallery.metadata.rejected"),
            MediaTimingTrace::mergedFields(
                MediaTimingTrace::sourceFields(info.source), {
                    {QStringLiteral("reason"), QStringLiteral("authority")},
                    {QStringLiteral("sourceIdentity"), sourceIdentity},
                    {QStringLiteral("row"), rows.constFirst()},
                    {QStringLiteral("image"), entry.image},
                    {QStringLiteral("expectedVersion"), entry.contentVersion},
                    {QStringLiteral("actualVersion"), info.sourceVersionToken},
                    {QStringLiteral("authorityMatches"),
                     sourceAuthorityMatches(entry.source, info.source)},
                }));
        return;
    }
    if (info.sourceAccessFailed) {
        const Entry &entry = loadedEntry(authorityRows.constFirst());
        scheduleMetadataRetry(sourceIdentity, entry.contentVersion,
                              entry.source.resourceId,
                              !info.highPriority);
        return;
    }

    bool applied = false;
    for (const int row : authorityRows) {
        applied = applyImageInfoToRow(row, info, state) || applied;
    }
    if (!applied) {
        return;
    }

    _metadataResolvedPaths.insert(sourceIdentity);
    const QString retryKey = sourceIdentity + QChar(0x1f)
        + info.sourceVersionToken;
    _metadataRetryAttempts.remove(retryKey);
    _metadataRetryScheduled.remove(retryKey);
    _backgroundMetadataRetries.remove(retryKey);
    if (_catalogFitWaitingMetadata.remove(sourceIdentity)
        && !_catalogFitResolvedSources.contains(sourceIdentity)) {
        _catalogFitRows.prepend(authorityRows.constFirst());
        state.catalogFitChanged = true;
    }
}

bool ExternalCatalogModel::applyImageInfoToRow(
    int row, const ImageInfo &info, ImageInfoBatchState &state) {
    Entry &entry = loadedEntry(row);
    const QString sourceIdentity = info.sourceIdentity();
    if (!versionMatches(entry.contentVersion, entry.mtimeNs,
                        entry.size, info)) {
        MediaTimingTrace::event(
            QStringLiteral("qt.gallery.metadata.rejected"),
            MediaTimingTrace::mergedFields(
                MediaTimingTrace::sourceFields(info.source), {
                    {QStringLiteral("reason"), QStringLiteral("version")},
                    {QStringLiteral("sourceIdentity"), sourceIdentity},
                    {QStringLiteral("row"), row},
                    {QStringLiteral("expectedVersion"), entry.contentVersion},
                    {QStringLiteral("actualVersion"), info.sourceVersionToken},
                    {QStringLiteral("expectedSize"), entry.size},
                    {QStringLiteral("actualSize"), info.fileSize},
                    {QStringLiteral("expectedMtimeNs"), entry.mtimeNs},
                    {QStringLiteral("actualMtimeMs"),
                     info.lastModified.toMSecsSinceEpoch()},
                }));
        return false;
    }
    ImageInfo currentInfo = info;
    if (entry.mtimeNs != 0) {
        currentInfo.lastModified = QDateTime::fromMSecsSinceEpoch(
            entry.mtimeNs / 1000000, QTimeZone::UTC);
    }
    currentInfo.source = entry.source;
    currentInfo.path = entry.sourceIdentity;
    currentInfo.sourceVersionToken = entry.contentVersion;
    if (entry.size >= 0) {
        currentInfo.fileSize = entry.size;
    }
    entry.imageInfo = currentInfo;
    entry.originalSize = rotateToOrientation(
        currentInfo.imageSize, currentInfo.orientation);
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.metadata.applied"), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("row"), row},
            {QStringLiteral("sourceIdentity"), sourceIdentity},
            {QStringLiteral("imageWidth"), entry.originalSize.width()},
            {QStringLiteral("imageHeight"), entry.originalSize.height()},
        });
    if (entry.item) {
        entry.item->setInfo(entry.imageInfo);
        entry.item->setFullSize(entry.originalSize);
    }
    state.allChangedMetadataCached =
        state.allChangedMetadataCached && currentInfo.isCached;
    state.firstChangedRow = state.firstChangedRow < 0
        ? row : qMin(state.firstChangedRow, row);
    state.lastChangedRow = qMax(state.lastChangedRow, row);
    state.flushRequested = state.flushRequested || info.isLast;
    state.viewerMetadataChanged = true;
    return true;
}

void ExternalCatalogModel::publishImageInfoBatch(
    const ImageInfoBatchState &state) {
    // Cached dimensions arrive as one list. Publish one broad range only after
    // every ImageFile has its final size, making Masonry perform one rewrap
    // instead of one rewrap per manifest. Rows between sparse hits are safe:
    // Masonry ignores ImageFiles whose fullSize is still invalid.
    if (state.firstChangedRow >= 0) {
        QList<int> roles{FileListModel::ImageFullSizeRole,
                         KnownImageSizeRole};
        if (state.allChangedMetadataCached) {
            roles.append(FileListModel::CachedMetadataBatchRole);
        }
        if (state.flushRequested) {
            roles.append(FileListModel::TimeToFlushRole);
        }
        emit dataChanged(index(state.firstChangedRow),
                         index(state.lastChangedRow), roles);
    }
    if (state.catalogFitChanged) {
        scheduleCatalogFitPump();
    }
    if (state.viewerMetadataChanged) {
        const auto plans = _viewerPlans;
        for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
            const int centerRow = rowForEntryId(plan.key());
            if (validRow(centerRow)) {
                scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                       plan->prefetchCount);
            }
        }
    }
    if (state.acceptedNamespace) {
        scheduleMetadataPump();
    }
}

void ExternalCatalogModel::handleImageReady(
    const ImageDecodeRequest &request, const QImage &image,
    const DecodedImageInfo &decodedInfo) {
    if (request.requestNamespace != _sessionId) {
        return;
    }
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.decode.delivered"),
        MediaTimingTrace::mergedFields(
            decodeRequestTimingFields(request), {
                {QStringLiteral("imageWidth"), image.width()},
                {QStringLiteral("imageHeight"), image.height()},
                {QStringLiteral("null"), image.isNull()},
            }));
    const QList<int> authorityRows = decodeAuthorityRows(request);
    if (authorityRows.isEmpty()) {
        return;
    }
    clearCompletedDecodeRequest(request);
    if (_shutdown) {
        return;
    }
    if (image.isNull()) {
        if (!request.viewerRequest) {
            releaseFailedThumbnailRequest(request);
        }
        // A null decode from a VFS-backed source is commonly caused by a
        // transiently invalid materialized lease during transport recovery.
        // Keep the bounded retry path for it as well; local corrupt/unsupported
        // files retain their terminal behavior.
        if (request.info.source.isValid()) {
            scheduleSourceDecodeRetry(request);
        }
        return;
    }
    const QList<int> decodedRows = validatedDecodedRows(
        authorityRows, request);
    if (decodedRows.isEmpty()) {
        return;
    }
    if (request.viewerRequest) {
        publishViewerImage(decodedRows, request, image, decodedInfo);
    } else {
        publishThumbnailImage(
            loadedEntry(decodedRows.constFirst()), request, image);
    }
}

QList<int> ExternalCatalogModel::sourceRows(
    const QString &sourceIdentity) const {
    QList<int> rows;
    const QList<QString> entryIds = _sourceEntryIds.values(sourceIdentity);
    rows.reserve(entryIds.size());
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (validRow(row)) {
            rows.append(row);
        }
    }
    if (rows.isEmpty()) {
        const int fallbackRow = _sourceToRow.value(sourceIdentity, -1);
        if (validRow(fallbackRow)) {
            rows.append(fallbackRow);
        }
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

QList<int> ExternalCatalogModel::decodeAuthorityRows(
    const ImageDecodeRequest &request) const {
    QList<int> rows = sourceRows(request.info.sourceIdentity());
    if (rows.isEmpty() && !request.info.source.isValid()
        && !request.info.path.isEmpty()) {
        const int pathRow = _pathToRow.value(
            QDir::cleanPath(request.info.path), -1);
        if (validRow(pathRow)) {
            rows.append(pathRow);
        }
    }
    rows.erase(std::remove_if(
        rows.begin(), rows.end(), [this, &request](int row) {
            return !sourceAuthorityMatches(
                loadedEntry(row).source, request.info.source);
        }), rows.end());
    return rows;
}

void ExternalCatalogModel::clearCompletedDecodeRequest(
    const ImageDecodeRequest &request) {
    const QString requestKey = request.viewerRequest
        ? viewerRequestKey(request) : thumbnailRequestKey(request);
    const QString retryKey = (request.viewerRequest
        ? QStringLiteral("viewer:") : QStringLiteral("thumbnail:"))
        + requestKey;
    _sourceDecodeRetryAttempts.remove(retryKey);
    _sourceDecodeRetryScheduled.remove(retryKey);
    if (request.viewerRequest) {
        _backgroundDecodeRetries.remove(retryKey);
        _pendingViewerRequests.remove(requestKey);
        completeCatalogFitRequest(request);
    } else {
        _pendingThumbnailRequests.remove(requestKey);
    }
}

ExternalCatalogModel::Entry *ExternalCatalogModel::validatedDecodedEntry(
    int row, const ImageDecodeRequest &request) {
    if (!validRow(row)) {
        return nullptr;
    }
    Entry &entry = loadedEntry(row);
    const bool authorityMatches =
        sourceAuthorityMatches(entry.source, request.info.source);
    const bool matchingVersion =
        versionMatches(entry.contentVersion, entry.mtimeNs,
                       entry.size, request.info);
    if (!entry.image || !authorityMatches || !matchingVersion) {
        MediaTimingTrace::event(
            QStringLiteral("qt.gallery.decode.rejected"),
            MediaTimingTrace::mergedFields(
                decodeRequestTimingFields(request), {
                    {QStringLiteral("row"), row},
                    {QStringLiteral("image"), entry.image},
                    {QStringLiteral("authorityMatches"), authorityMatches},
                    {QStringLiteral("versionMatches"), matchingVersion},
                    {QStringLiteral("expectedVersion"), entry.contentVersion},
                    {QStringLiteral("actualVersion"),
                     request.info.sourceVersionToken},
                    {QStringLiteral("expectedSize"), entry.size},
                    {QStringLiteral("actualSize"), request.info.fileSize},
                }));
        return nullptr;
    }
    return &entry;
}

QList<int> ExternalCatalogModel::validatedDecodedRows(
    const QList<int> &rows, const ImageDecodeRequest &request) {
    QList<int> validated;
    validated.reserve(rows.size());
    for (const int row : rows) {
        if (validatedDecodedEntry(row, request)) {
            validated.append(row);
        }
    }
    return validated;
}

void ExternalCatalogModel::deriveCatalogThumbnail(
    const ImageDecodeRequest &request, const QImage &image, Entry &entry) {
    if (!request.backgroundViewerRequest || !request.fitToViewerRequest
        || !_thumbnailCache) {
        return;
    }
    const QSize target = stableDecodeTarget(
        QSize(CatalogThumbnailLongEdge, CatalogThumbnailLongEdge),
        entry.originalSize.isValid()
            ? entry.originalSize
            : entry.item ? entry.item->fullSize() : QSize(),
        {}, DecodeSizeFamily::Thumbnail, true);
    QVariantMap fields = decodeRequestTimingFields(request);
    fields.insert(QStringLiteral("thumbnailTargetWidth"), target.width());
    fields.insert(QStringLiteral("thumbnailTargetHeight"), target.height());
    MediaTimingTrace::Span span(
        QStringLiteral("qt.gallery.catalog_thumbnail_derive"), fields);
    const QImage thumbnail = ThumbnailLoader::createThumbnail(image, target);
    span.set(QStringLiteral("ok"), !thumbnail.isNull());
    span.set(QStringLiteral("outputWidth"), thumbnail.width());
    span.set(QStringLiteral("outputHeight"), thumbnail.height());
    if (!thumbnail.isNull()) {
        _thumbnailCache->storeDecoded(
            _sessionId, entry.sourceIdentity, entry.contentVersion,
            entry.size, target,
            QString::fromLatin1(
                ThumbnailMemoryCache::DefaultTransformKey), thumbnail);
    }
}

void ExternalCatalogModel::publishViewerImage(
    const QList<int> &rows, const ImageDecodeRequest &request,
    const QImage &image, const DecodedImageInfo &decodedInfo) {
    Q_ASSERT(!rows.isEmpty());
    Entry &representative = loadedEntry(rows.constFirst());
    const ViewerImageCache::StoredImage stored =
        _viewerImageCache.storeDecodedImage(request, image, decodedInfo);
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.viewer_image.stored"), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("row"), rows.constFirst()},
            {QStringLiteral("duplicateRows"), rows.size()},
            {QStringLiteral("entryId"), representative.id},
            {QStringLiteral("viewerEntryId"), _viewerEntryId},
            {QStringLiteral("accepted"), stored.accepted},
            {QStringLiteral("presentable"), stored.presentable},
            {QStringLiteral("level"), stored.level},
            {QStringLiteral("url"), stored.url},
        });
    deriveCatalogThumbnail(request, image, representative);
    if (stored.presentable) {
        bool activeViewerChanged = false;
        for (const int row : rows) {
            emit viewerSourceAtChanged(row);
            activeViewerChanged = activeViewerChanged
                || loadedEntry(row).id == _viewerEntryId;
        }
        if (activeViewerChanged) {
            notifyViewerImageUrlChanged();
        }
    }
    if (request.fitToViewerRequest) {
        tryScheduleDeferredNative();
    }
}

void ExternalCatalogModel::publishThumbnailImage(
    Entry &entry, const ImageDecodeRequest &request, const QImage &image) {
    if (!_thumbnailCache) {
        return;
    }
    if (!entry.thumbnailRequestedSize.isValid()) {
        entry.thumbnailRequestedSize = request.targetSize;
        entry.thumbnailTransformKey = thumbnailTransformKey(request);
    }
    _thumbnailCache->storeDecoded(
        _sessionId, entry.sourceIdentity, entry.contentVersion,
        entry.size, request.targetSize, thumbnailTransformKey(request), image);
}

void ExternalCatalogModel::handleImageReadFailed(
    const ImageDecodeRequest &request) {
    if (_shutdown || request.requestNamespace != _sessionId) {
        return;
    }
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.image_read.failed"),
        decodeRequestTimingFields(request));
    if (decodeAuthorityRows(request).isEmpty()) {
        return;
    }
    if (request.viewerRequest) {
        const QString key = viewerRequestKey(request);
        _pendingViewerRequests.remove(key);
        if (request.sourceAccessFailed) {
            // Free the bounded Fit slot but do not mark this immutable source
            // complete: a transport timeout says nothing about its pixels.
            _catalogFitPendingKeys.remove(key);
            scheduleCatalogFitPump();
            scheduleSourceDecodeRetry(request);
        }
        else {
            completeCatalogFitRequest(request);
        }
        return;
    }

    releaseFailedThumbnailRequest(request);
    // External/VFS reads can fail transiently while the media transport is
    // reconnecting. A normal thumbnail must get the same bounded retry as a
    // visible/high-priority request; otherwise one timeout leaves its card
    // permanently empty until the folder is reopened.
    if (request.sourceAccessFailed) {
        scheduleSourceDecodeRetry(request);
    }
}

void ExternalCatalogModel::releaseFailedThumbnailRequest(
    const ImageDecodeRequest &request, bool retryWaiters) {
    const QString requestKey = thumbnailRequestKey(request);
    _pendingThumbnailRequests.remove(requestKey);
    if (!_thumbnailCache) {
        return;
    }

    // requestReleased normally wakes coalesced sessions so they can elect a
    // new owner after cancellation. A decode failure is different: waking the
    // visible-item planner synchronously would submit the same corrupt frame
    // forever. The shared retryWaiters=false reason clears admission state in
    // every session without allowing cross-session failure ping-pong.
    // The shared admission key uses the host-authoritative source size. A
    // materialized file may discover an actual QFile size while the host
    // descriptor intentionally remains unknown (-1); mixing those values
    // would leave the original single-flight owner pending forever.
    const qint64 admittedSourceSize = request.info.source.isValid()
        ? request.info.source.size : request.info.fileSize;
    _thumbnailCache->releaseRequest(
        _sessionId, request.info.sourceIdentity(),
        request.info.sourceVersionToken, admittedSourceSize,
        request.targetSize, thumbnailTransformKey(request), retryWaiters);
}

void ExternalCatalogModel::scheduleSourceDecodeRetry(
    const ImageDecodeRequest &request) {
    const QString retryKey =
        (request.viewerRequest ? QStringLiteral("viewer:")
                               : QStringLiteral("thumbnail:")) +
        (request.viewerRequest ? viewerRequestKey(request)
                               : thumbnailRequestKey(request));
    if (_sourceDecodeRetryScheduled.contains(retryKey)) {
        return;
    }
    const int MaxAutomaticAttempts =
        request.backgroundViewerRequest ? 1 :
        (request.info.source.isValid() ? 8 : 3);
    const int attempt = _sourceDecodeRetryAttempts.value(retryKey) + 1;
    if (attempt > MaxAutomaticAttempts) {
        return;
    }
    _sourceDecodeRetryAttempts.insert(retryKey, attempt);
    _sourceDecodeRetryScheduled.insert(retryKey);
    const int delayMs = static_cast<int>(qMin<qint64>(
        30000, 500LL << qMin(attempt - 1, 6)));
    if (request.backgroundViewerRequest) {
        _backgroundDecodeRetries.insert(retryKey, BackgroundDecodeRetry{
            .request = request,
            .notBeforeMs = QDateTime::currentMSecsSinceEpoch() + delayMs,
        });
        scheduleBackgroundRetryWake();
        return;
    }
    const QString sourceIdentity = request.info.sourceIdentity();
    const QString contentVersion = request.info.sourceVersionToken;
    const QString resourceId = request.info.source.resourceId;
    const bool viewerRequest = request.viewerRequest;
    const bool backgroundViewerRequest = request.backgroundViewerRequest;
    QTimer::singleShot(
        delayMs, this,
        [this, retryKey, sourceIdentity, contentVersion, resourceId,
         viewerRequest, backgroundViewerRequest]() {
        _sourceDecodeRetryScheduled.remove(retryKey);
        if (_shutdown) {
            return;
        }
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row) ||
            loadedEntry(row).contentVersion != contentVersion ||
            loadedEntry(row).source.resourceId != resourceId) {
            _sourceDecodeRetryAttempts.remove(retryKey);
            return;
        }
        if (!viewerRequest) {
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageIdUrlRole});
            return;
        }
        if (backgroundViewerRequest && _catalogFitStarted &&
            !_catalogFitResolvedSources.contains(sourceIdentity) &&
            !_catalogFitRows.contains(row)) {
            _catalogFitRows.prepend(row);
            scheduleCatalogFitPump();
        }
        const auto plans = _viewerPlans;
        for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
            const int centerRow = rowForEntryId(plan.key());
            if (validRow(centerRow)) {
                scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                       plan->prefetchCount);
            }
        }
    });
}

void ExternalCatalogModel::clearPublishedImage(Entry &entry) {
    if (!entry.item) {
        return;
    }
    detachThumbnail(entry);
}

void ExternalCatalogModel::attachThumbnail(
    int row, const QString &providerId) {
    if (!validRow(row) || providerId.isEmpty()) {
        return;
    }
    Entry &entry = loadedEntry(row);
    ImageFile *item = ensureItem(row);
    if (!item) {
        return;
    }
    if (entry.thumbnailProviderId == providerId &&
        item->imageIdUrl().endsWith(providerId)) {
        return;
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        _providerEntryIds.remove(entry.thumbnailProviderId, entry.id);
    }
    else {
        // Remove only a legacy per-session publication. Shared cache IDs are
        // owned solely by ThumbnailMemoryCache and remain valid for users in
        // the other panel.
        const QString legacyId = item->imageIdUrl().section(
            QLatin1Char('/'), -1);
        if (!legacyId.isEmpty()) {
            _store->remove(legacyId);
        }
    }
    entry.thumbnailProviderId = providerId;
    _providerEntryIds.insert(providerId, entry.id);
    const QVariantMap attachFields = MediaTimingTrace::mergedFields(
        MediaTimingTrace::sourceFields(entry.source), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("row"), row},
            {QStringLiteral("entryId"), entry.id},
            {QStringLiteral("providerId"), providerId},
            {QStringLiteral("requestedWidth"),
             entry.thumbnailRequestedSize.width()},
            {QStringLiteral("requestedHeight"),
             entry.thumbnailRequestedSize.height()},
            {QStringLiteral("transformKey"),
             entry.thumbnailTransformKey},
            {QStringLiteral("provisional"),
             !entry.thumbnailRequestedSize.isValid()},
        });
    MediaTimingTrace::Span attachSpan(
        QStringLiteral("qt.gallery.thumbnail.attach"), attachFields);
    item->setImage({}, {});
    item->setImageId(providerId);
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.thumbnail.published"),
        attachFields);
    emit dataChanged(index(row), index(row),
                     {FileListModel::ImageIdUrlRole,
                      VisualSnapshotRole});
    emit viewerSourceAtChanged(row);
    if (entry.id == _viewerEntryId &&
        _viewerImageCache.bestImageUrl(item) == item->imageIdUrl()) {
        notifyViewerImageUrlChanged();
    }
}

void ExternalCatalogModel::detachThumbnail(Entry &entry) {
    if (!entry.item) {
        return;
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        _providerEntryIds.remove(entry.thumbnailProviderId, entry.id);
    }
    else {
        const QString legacyId = entry.item->imageIdUrl().section(
            QLatin1Char('/'), -1);
        if (!legacyId.isEmpty()) {
            _store->remove(legacyId);
        }
    }
    entry.thumbnailProviderId.clear();
    entry.item->setImageId({});
    entry.item->setImage({}, {});
}

bool ExternalCatalogModel::adoptCachedThumbnail(int row) {
    if (!_thumbnailCache || !validRow(row)) {
        return false;
    }
    const Entry &entry = loadedEntry(row);
    if (!entry.image || !entry.thumbnailRequestedSize.isValid()) {
        return false;
    }
    const ThumbnailMemoryCache::Handle cached = _thumbnailCache->lookup(
        entry.sourceIdentity, entry.contentVersion, entry.size,
        entry.thumbnailRequestedSize, entry.thumbnailTransformKey);
    if (!cached.isValid()) {
        return false;
    }
    attachThumbnail(row, cached.providerId);
    return true;
}

void ExternalCatalogModel::handleThumbnailFrameAvailable(
    const QString &sourceIdentity, const QString &versionToken,
    qint64 sourceFileSize, const QSize &requestedSize,
    const QString &transformKey, const QString &providerId) {
    if (_shutdown) {
        return;
    }
    const QList<QString> entryIds =
        _sourceEntryIds.values(sourceIdentity);
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = loadedEntry(row);
        if (entry.contentVersion != versionToken ||
            (entry.size >= 0 && sourceFileSize >= 0 &&
             entry.size != sourceFileSize)) {
            continue;
        }
        ImageFile *item = ensureItem(row);
        if (!item) {
            continue;
        }
        ImageDecodeRequest desired;
        desired.info = item->info();
        desired.targetSize = entry.thumbnailRequestedSize;
        desired.thumbnailTransformKey = entry.thumbnailTransformKey;
        const QString desiredKey = thumbnailRequestKey(desired);
        const QString completedTransform =
            normalizedThumbnailTransformKey(transformKey);
        const auto pending = _pendingThumbnailRequests.constFind(desiredKey);
        const bool exactCurrentCompletion =
            requestedSize == desired.targetSize &&
            completedTransform == thumbnailTransformKey(desired);
        if (pending == _pendingThumbnailRequests.constEnd()) {
            // The exact request owner removes its local admission record in
            // handleImageReady immediately before storeDecoded emits this
            // synchronous completion. A session with no related request must
            // not consume another tier's completion.
            if (!exactCurrentCompletion) {
                continue;
            }
        }
        else {
            const bool completionMatchesAdmission =
                pending->admittedTargetSize == requestedSize &&
                normalizedThumbnailTransformKey(
                    pending->admittedTransformKey) == completedTransform;
            if (!completionMatchesAdmission) {
                // A genuinely active owner for another tier stays pending
                // until its own imageReady. This matters when a small request
                // was admitted first and a later larger request completes
                // sooner.
                continue;
            }
            if (pending->owner) {
                // Only this owner's own imageReady removes the record before
                // its synchronous storeDecoded completion. A frame from a
                // different request/session must never consume a genuinely
                // active owner, even when both happen to use the same tier.
                continue;
            }
            _pendingThumbnailRequests.erase(pending);
        }

        if (exactCurrentCompletion &&
            !providerId.isEmpty()) {
            // The provider is the authoritative result of this exact target,
            // even when an aspect-preserving decoder returns (for example)
            // 24x32 for a 32x32 bounding request. Equal-target waiters in all
            // sessions attach it directly; cross-tier reuse remains strict.
            attachThumbnail(row, providerId);
            continue;
        }
        if (!adoptCachedThumbnail(row) &&
            entry.thumbnailProviderId.isEmpty()) {
            // The completed bounding request did not yield enough pixels for
            // this different tier. Let this waiter acquire and decode its own
            // exact target instead of weakening cache coverage semantics.
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageIdUrlRole});
        }
    }
}

void ExternalCatalogModel::handleThumbnailFrameEvicted(
    const QString &providerId) {
    if (_shutdown || providerId.isEmpty()) {
        return;
    }
    const QList<QString> entryIds =
        _providerEntryIds.values(providerId);
    _providerEntryIds.remove(providerId);
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = loadedEntry(row);
        if (entry.thumbnailProviderId != providerId) {
            continue;
        }
        entry.thumbnailProviderId.clear();
        if (entry.item) {
            entry.item->setImageId({});
            entry.item->setImage({}, {});
        }
        emit dataChanged(index(row), index(row),
                         {FileListModel::ImageIdUrlRole,
                          VisualSnapshotRole});
        emit viewerSourceAtChanged(row);
        if (entry.id == _viewerEntryId) {
            notifyViewerImageUrlChanged();
        }
    }
}

void ExternalCatalogModel::handleThumbnailRequestReleased(
    const QString &sourceIdentity, const QString &versionToken,
    qint64 sourceFileSize, const QSize &requestedSize,
    const QString &transformKey, bool retryWaiters) {
    if (_shutdown) {
        return;
    }
    const QList<QString> entryIds =
        _sourceEntryIds.values(sourceIdentity);
    for (const QString &entryId : entryIds) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = loadedEntry(row);
        if (entry.contentVersion != versionToken ||
            (entry.size >= 0 && sourceFileSize >= 0 &&
             entry.size != sourceFileSize) ||
            !entry.thumbnailRequestedSize.isValid()) {
            continue;
        }
        ImageDecodeRequest desired;
        ImageFile *item = ensureItem(row);
        if (!item) {
            continue;
        }
        desired.info = item->info();
        desired.targetSize = entry.thumbnailRequestedSize;
        desired.thumbnailTransformKey = entry.thumbnailTransformKey;
        const QString desiredKey = thumbnailRequestKey(desired);
        const auto pending = _pendingThumbnailRequests.constFind(desiredKey);
        if (pending == _pendingThumbnailRequests.constEnd() ||
            pending->admittedTargetSize != requestedSize ||
            normalizedThumbnailTransformKey(
                pending->admittedTransformKey) !=
                normalizedThumbnailTransformKey(transformKey)) {
            continue;
        }
        _pendingThumbnailRequests.erase(pending);
        if (entry.thumbnailProviderId.isEmpty() && retryWaiters) {
            // The viewport planner treats an empty ImageIdUrlRole update as a
            // request to re-plan visible/overscan work after owner cancel.
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageIdUrlRole});
        }
    }
}

void ExternalCatalogModel::enqueueProbeRows(
    const QList<int> &rows, bool highPriority) {
    QList<int> &queue = highPriority ? _probeVisibleRows
                                     : _probeOverscanRows;
    for (const int row : rows) {
        if (!validRow(row)) {
            continue;
        }
        const Entry &entry = loadedEntry(row);
        if (!entry.image || !entry.source.isValid() ||
            !expensiveSource(entry.source)) {
            continue;
        }
        const QString cacheKey = entry.source.cacheKey();
        if (_probeRetryableSources.contains(cacheKey)) {
            // A transient transport failure is not a negative-cache entry.
            // Retry only after visibility/user intent, never merely because a
            // layout emitted dataChanged again.
            if (_probeRetryNotBeforeMs.value(cacheKey) >
                QDateTime::currentMSecsSinceEpoch()) {
                continue;
            }
            _probeResolvedVersions.remove(entry.sourceIdentity);
            _probeRetryableSources.remove(cacheKey);
        }
        if (!queue.contains(row)) {
            queue.append(row);
        }
    }
}

bool ExternalCatalogModel::probeResolvedFor(const Entry &entry) const {
    return !entry.image || !entry.source.isValid() ||
        !expensiveSource(entry.source) ||
        _probeResolvedVersions.value(entry.sourceIdentity) ==
            entry.contentVersion;
}

bool ExternalCatalogModel::probeBarrierReached() const {
    for (const Entry &entry : _entries) {
        if (entry.image && entry.source.isValid() &&
            expensiveSource(entry.source) &&
            !probeResolvedFor(entry)) {
            return false;
        }
    }
    return true;
}

void ExternalCatalogModel::scheduleProbePump() {
    const bool retryWaiting = !_probeUrgentRows.isEmpty() ||
        !_probeVisibleRows.isEmpty() || !_probeOverscanRows.isEmpty();
    if (_shutdown || _probePumpScheduled ||
        (_probePassComplete && !retryWaiting)) {
        return;
    }
    _probePumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        _probePumpScheduled = false;
        pumpProbeRequests();
    });
}

void ExternalCatalogModel::pumpProbeRequests() {
    const bool retryWaiting = !_probeUrgentRows.isEmpty() ||
        !_probeVisibleRows.isEmpty() || !_probeOverscanRows.isEmpty();
    if (_shutdown || (_probePassComplete && !retryWaiting)) {
        return;
    }

    ProbeBatch batch;
    batch.available = probeRequestLimit() - _probePendingVersions.size();
    if (batch.available <= 0) {
        return;
    }
    const bool foregroundWaiting = !_probeUrgentRows.isEmpty() ||
        !_probeVisibleRows.isEmpty();
    if (!foregroundWaiting &&
        _probePendingVersions.size() > probeRefillLowWatermark()) {
        return;
    }

    drainProbeRows(batch, _probeUrgentRows, true);
    drainProbeRows(batch, _probeVisibleRows, true);
    drainProbeRows(batch, _probeOverscanRows, false);
    collectCatalogProbeRows(batch);
    submitProbeBatch(batch.highRequests, true);
    submitProbeBatch(batch.backgroundRequests, false);
    if (finishCatalogProbePass()) {
        beginCatalogFitPass();
    }
}

void ExternalCatalogModel::appendProbeRow(
    ProbeBatch &batch, int row, bool highPriority) {
    if (batch.available <= 0 || !validRow(row)) {
        return;
    }
    const Entry &entry = loadedEntry(row);
    if (!entry.image || !entry.source.isValid() || probeResolvedFor(entry)
        || _probePendingVersions.contains(entry.sourceIdentity)) {
        return;
    }
    _probePendingVersions.insert(entry.sourceIdentity, entry.contentVersion);
    QList<ImageProbeRequest> &requests = highPriority
        ? batch.highRequests : batch.backgroundRequests;
    requests.append(ImageProbeRequest{
        .source = entry.source,
        .requestNamespace = _sessionId,
        .highPriority = highPriority,
    });
    --batch.available;
}

void ExternalCatalogModel::drainProbeRows(
    ProbeBatch &batch, QList<int> &rows, bool highPriority) {
    while (batch.available > 0 && !rows.isEmpty()) {
        appendProbeRow(batch, rows.takeFirst(), highPriority);
    }
}

void ExternalCatalogModel::collectCatalogProbeRows(ProbeBatch &batch) {
    if (!_catalogProbeRequested) {
        return;
    }
    while (batch.available > 0 && _catalogProbeCursor < _entries.size()) {
        const int row = _entries.at(_catalogProbeCursor++).sourceIndex;
        appendProbeRow(batch, row, false);
    }
}

void ExternalCatalogModel::submitProbeBatch(
    const QList<ImageProbeRequest> &requests, bool highPriority) {
    if (requests.isEmpty()) {
        return;
    }
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.probe.batch_submitted"), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("priority"), highPriority
                ? QStringLiteral("high") : QStringLiteral("background")},
            {QStringLiteral("submitted"), requests.size()},
            {QStringLiteral("pending"), _probePendingVersions.size()},
            {QStringLiteral("catalogCursor"), _catalogProbeCursor},
        });
    _decodeManager->probeImages(requests);
}

void ExternalCatalogModel::handleImageProbe(
    const ImageProbeResult &result) {
    QString sourceIdentity;
    QString version;
    if (!acceptImageProbe(result, sourceIdentity, version)) {
        return;
    }
    recordProbeStatus(result, sourceIdentity, version);
    const QString providerId = storeProbePreview(
        result, sourceIdentity, version);
    publishProbeRows(result, sourceIdentity, version, providerId);
    replanAfterImageProbe(finishCatalogProbePass());
}

bool ExternalCatalogModel::acceptImageProbe(
    const ImageProbeResult &result, QString &sourceIdentity,
    QString &version) {
    if (_shutdown || result.request.requestNamespace != _sessionId) {
        return false;
    }
    sourceIdentity = result.request.source.runtimeIdentity();
    version = result.request.source.contentVersion;
    const auto pending = _probePendingVersions.constFind(sourceIdentity);
    if (pending == _probePendingVersions.cend() || pending.value() != version) {
        return false;
    }
    const int row = _sourceToRow.value(sourceIdentity, -1);
    if (!validRow(row)
        || !sourceAuthorityMatches(
            loadedEntry(row).source, result.request.source)) {
        return false;
    }
    _probePendingVersions.remove(sourceIdentity);
    _probeResolvedVersions.insert(sourceIdentity, version);
    return true;
}

void ExternalCatalogModel::recordProbeStatus(
    const ImageProbeResult &result, const QString &sourceIdentity,
    const QString &version) {
    const QString cacheKey = result.request.source.cacheKey();
    if (result.status != ImageSourceProbeStatus::Failed) {
        _probeRetryableSources.remove(cacheKey);
        _probeRetryAttempts.remove(cacheKey);
        _probeRetryNotBeforeMs.remove(cacheKey);
        return;
    }
    _probeRetryableSources.insert(cacheKey);
    const int attempt = qMin(7, _probeRetryAttempts.value(cacheKey) + 1);
    _probeRetryAttempts.insert(cacheKey, attempt);
    const qint64 delayMs = qMin<qint64>(
        30000, 500LL << qMin(attempt - 1, 6));
    _probeRetryNotBeforeMs.insert(
        cacheKey, QDateTime::currentMSecsSinceEpoch() + delayMs);
    if (!result.request.highPriority || attempt > 3
        || (_catalogProbeRequested && !_probePassComplete)) {
        return;
    }
    const QString resourceId = result.request.source.resourceId;
    QTimer::singleShot(
        static_cast<int>(delayMs), this,
        [this, sourceIdentity, version, resourceId, cacheKey]() {
        if (_shutdown || !_probeRetryableSources.contains(cacheKey)) {
            return;
        }
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row)) {
            return;
        }
        const Entry &entry = loadedEntry(row);
        if (entry.contentVersion != version
            || entry.source.resourceId != resourceId) {
            return;
        }
        enqueueProbeRows({row}, true);
        scheduleProbePump();
    });
}

QString ExternalCatalogModel::storeProbePreview(
    const ImageProbeResult &result, const QString &sourceIdentity,
    const QString &version) {
    if (!result.found() || !_thumbnailCache) {
        return {};
    }
    const ThumbnailMemoryCache::Handle provisional =
        _thumbnailCache->storeDecoded(
            _sessionId, sourceIdentity, version,
            result.request.source.size, result.preview.size(),
            QString::fromLatin1(EmbeddedProvisionalTransform),
            result.preview);
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.probe.provisional_stored"),
        MediaTimingTrace::mergedFields(
            MediaTimingTrace::sourceFields(result.request.source), {
                {QStringLiteral("providerId"), provisional.providerId},
                {QStringLiteral("previewWidth"), result.preview.width()},
                {QStringLiteral("previewHeight"), result.preview.height()},
                {QStringLiteral("sourceBytesRead"), result.sourceBytesRead},
                {QStringLiteral("rangeRequests"), result.rangeRequests},
            }));
    return provisional.providerId;
}

void ExternalCatalogModel::publishProbeRows(
    const ImageProbeResult &result, const QString &sourceIdentity,
    const QString &version, const QString &providerId) {
    for (const QString &entryId : _sourceEntryIds.values(sourceIdentity)) {
        const int row = rowForEntryId(entryId);
        if (!validRow(row) || loadedEntry(row).contentVersion != version) {
            continue;
        }
        Entry &entry = loadedEntry(row);
        if (result.sourceSize.isValid()) {
            ImageFile *item = ensureItem(row);
            ImageInfo info = item->info();
            info.source = entry.source;
            info.path = entry.sourceIdentity;
            info.sourceVersionToken = entry.contentVersion;
            info.imageSize = result.sourceSize;
            if (result.orientation >= ExifOrientation::Horizontal
                && result.orientation <= ExifOrientation::Rotate270CW) {
                info.orientation =
                    static_cast<ExifOrientation>(result.orientation);
            }
            item->setInfo(info);
            item->setFullSize(rotateToOrientation(
                info.imageSize, info.orientation));
            emit dataChanged(index(row), index(row),
                             {FileListModel::ImageFullSizeRole});
        }
        if (!providerId.isEmpty() && entry.thumbnailProviderId.isEmpty()) {
            attachThumbnail(row, providerId);
        }
    }
}

bool ExternalCatalogModel::finishCatalogProbePass() {
    const bool completed = _catalogProbeRequested
        && _catalogProbeCursor >= _entries.size()
        && _probePendingVersions.isEmpty() && probeBarrierReached();
    if (completed) {
        _probePassComplete = true;
    }
    return completed;
}

void ExternalCatalogModel::replanAfterImageProbe(bool completed) {
    const auto plans = _viewerPlans;
    for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
        const int centerRow = rowForEntryId(plan.key());
        if (validRow(centerRow)) {
            scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                   plan->prefetchCount);
        }
    }
    scheduleMetadataPump();
    scheduleProbePump();
    if (completed) {
        beginCatalogFitPass();
    }
}


} // namespace ZoinGallery
