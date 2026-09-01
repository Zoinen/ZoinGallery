#include "ExternalCatalogModelPrivate.h"
#include "ExternalCatalogMetadataPlanner.h"

namespace ZoinGallery {

void ExternalCatalogModel::resetProgressivePipeline() {
    _probePendingVersions.clear();
    _probeResolvedVersions.clear();
    _probeRetryableSources.clear();
    _probeRetryAttempts.clear();
    _probeRetryNotBeforeMs.clear();
    _probeVisibleRows.clear();
    _probeOverscanRows.clear();
    _probeUrgentRows.clear();
    _catalogProbeCursor = 0;
    _catalogProbeRequested = false;
    _probePumpScheduled = false;
    _probePassComplete = false;
    _catalogFitRows.clear();
    _catalogFitPendingKeys.clear();
    _catalogFitResolvedSources.clear();
    _catalogFitWaitingMetadata.clear();
    _catalogFitStarted = false;
    _catalogFitPumpScheduled = false;
    _deferredNativeEntryId.clear();
}

void ExternalCatalogModel::beginCatalogFitPass() {
    if (_shutdown || _catalogFitStarted) {
        return;
    }
    _catalogFitStarted = true;
    QSet<int> seen;
    const auto append = [this, &seen](int row) {
        if (validRow(row) && loadedEntry(row).image &&
            !seen.contains(row)) {
            seen.insert(row);
            _catalogFitRows.append(row);
        }
    };
    const int viewerRow = rowForEntryId(_viewerEntryId);
    for (const int row : viewerCandidateRows(viewerRow, 16)) {
        append(row);
    }
    for (const int row : std::as_const(_metadataVisibleRows)) {
        if (validRow(row) && expensiveSource(loadedEntry(row).source)) {
            append(row);
        }
    }
    for (const int row : std::as_const(_metadataOverscanRows)) {
        if (validRow(row) && expensiveSource(loadedEntry(row).source)) {
            append(row);
        }
    }
    for (const Entry &entry : std::as_const(_entries)) {
        if (expensiveSource(entry.source)) {
            append(entry.sourceIndex);
        }
    }

    if (!_entries.isEmpty()) {
        // Wake rows which intentionally deferred their normal thumbnail
        // decode while pass 1 was still filling provisional frames.
        QList<int> rows;
        rows.reserve(_entries.size());
        for (const Entry &entry : std::as_const(_entries)) {
            rows.append(entry.sourceIndex);
        }
        std::sort(rows.begin(), rows.end());
        for (qsizetype offset = 0; offset < rows.size();) {
            const int first = rows.at(offset++);
            int last = first;
            while (offset < rows.size() && rows.at(offset) == last + 1) {
                last = rows.at(offset++);
            }
            emit dataChanged(index(first), index(last),
                             {FileListModel::ImageIdUrlRole});
        }
    }
    scheduleMetadataPump();
    scheduleCatalogFitPump();
    tryScheduleDeferredNative();
}

ImageDecodeRequest ExternalCatalogModel::catalogFitRequestForRow(
    int row) const {
    if (!validRow(row)) {
        return {};
    }
    const Entry &entry = loadedEntry(row);
    const QSize originalSize = entry.originalSize.isValid()
        ? entry.originalSize
        : entry.item ? entry.item->fullSize() : QSize();
    if (!entry.image || !originalSize.isValid()) {
        return {};
    }
    const ImageInfo info = entry.imageInfo.sourceIdentity().isEmpty()
        && entry.item ? entry.item->info() : entry.imageInfo;
    ImageDecodeRequest request = ViewerImageCache::makeRequest(
        info, originalSize, _lastViewerFitViewportSize);
    stabilizeViewerFitRequest(request);
    request.requestNamespace = _sessionId;
    request.info.requestNamespace = _sessionId;
    request.info.source = entry.source;
    request.info.path = entry.sourceIdentity;
    request.info.sourceVersionToken = entry.contentVersion;
    request.viewerRequest = true;
    request.backgroundViewerRequest = true;
    request.fitToViewerRequest = true;
    request.checkCache = true;
    request.expandToCacheResolution = false;
    request.storeInPersistentCache = true;
    return request;
}

void ExternalCatalogModel::stabilizeViewerFitRequest(
    ImageDecodeRequest &request) const {
    if (!request.fitToViewerRequest || !request.targetSize.isValid()) {
        return;
    }
    const int row = _sourceToRow.value(
        request.info.sourceIdentity(), -1);
    if (!validRow(row)) {
        return;
    }
    const Entry &entry = loadedEntry(row);
    const QSize previous = _viewerImageCache.entryForPath(
        entry.sourceIdentity, false).requestedSize;
    request.targetSize = stableDecodeTarget(
        request.targetSize,
        entry.originalSize.isValid()
            ? entry.originalSize
            : entry.item ? entry.item->fullSize() : QSize(),
        previous,
        DecodeSizeFamily::ViewerFit, expensiveSource(entry.source));
}

void ExternalCatalogModel::scheduleCatalogFitPump() {
    if (_shutdown || !_catalogFitStarted ||
        _catalogFitPumpScheduled) {
        return;
    }
    _catalogFitPumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        _catalogFitPumpScheduled = false;
        pumpCatalogFitRequests();
    });
}

void ExternalCatalogModel::pumpCatalogFitRequests() {
    if (_shutdown || !_catalogFitStarted) {
        return;
    }
    qsizetype available = catalogFitRequestLimit() -
        _catalogFitPendingKeys.size();
    if (available <= 0) {
        return;
    }

    QList<ImageDecodeRequest> requests;
    while (available > 0 && !_catalogFitRows.isEmpty()) {
        const int row = _catalogFitRows.takeFirst();
        if (!validRow(row)) {
            continue;
        }
        const Entry &entry = loadedEntry(row);
        if (!entry.image ||
            _catalogFitResolvedSources.contains(entry.sourceIdentity)) {
            continue;
        }
        const QSize originalSize = entry.originalSize.isValid()
            ? entry.originalSize
            : entry.item ? entry.item->fullSize() : QSize();
        if (!originalSize.isValid()) {
            if (_metadataResolvedPaths.contains(entry.sourceIdentity)) {
                _catalogFitResolvedSources.insert(entry.sourceIdentity);
            }
            else {
                _catalogFitWaitingMetadata.insert(entry.sourceIdentity);
                requestImageMetadataForRow(row, false);
            }
            continue;
        }

        ImageDecodeRequest request = catalogFitRequestForRow(row);
        if (!request.targetSize.isValid() ||
            !_viewerImageCache.needsDecode(request)) {
            _catalogFitResolvedSources.insert(entry.sourceIdentity);
            continue;
        }
        const QString key = viewerRequestKey(request);
        _catalogFitPendingKeys.insert(key);
        if (!_pendingViewerRequests.contains(key)) {
            _pendingViewerRequests.insert(key);
            requests.append(request);
        }
        --available;
    }
    if (!requests.isEmpty()) {
        _decodeManager->decodeImages(requests);
    }
}

void ExternalCatalogModel::completeCatalogFitRequest(
    const ImageDecodeRequest &request) {
    const QString key = viewerRequestKey(request);
    if (!_catalogFitPendingKeys.remove(key)) {
        return;
    }
    _catalogFitResolvedSources.insert(request.info.sourceIdentity());
    scheduleCatalogFitPump();
    tryScheduleDeferredNative();
}

bool ExternalCatalogModel::viewerFitWindowReady(
    int row, int count, const QSize &viewportSize) const {
    if (!viewportSize.isValid() || viewportSize.isEmpty()) {
        return false;
    }
    const QList<int> candidates = viewerCandidateRows(row, count);
    if (candidates.isEmpty()) {
        return false;
    }
    for (const int candidateRow : candidates) {
        if (!validRow(candidateRow)) {
            continue;
        }
        const Entry &entry = loadedEntry(candidateRow);
        const QSize originalSize = entry.originalSize.isValid()
            ? entry.originalSize
            : entry.item ? entry.item->fullSize() : QSize();
        if (!originalSize.isValid()) {
            return false;
        }
        const ImageInfo info = entry.imageInfo.sourceIdentity().isEmpty()
            && entry.item ? entry.item->info() : entry.imageInfo;
        ImageDecodeRequest fit = ViewerImageCache::makeRequest(
            info, originalSize, viewportSize);
        stabilizeViewerFitRequest(fit);
        if (!fit.targetSize.isValid() ||
            _viewerImageCache.needsDecode(fit)) {
            return false;
        }
    }
    return true;
}

void ExternalCatalogModel::tryScheduleDeferredNative() {
    if (_shutdown || _deferredNativeEntryId.isEmpty()) {
        return;
    }
    const int row = rowForEntryId(_deferredNativeEntryId);
    if (!validRow(row) || _viewerEntryId != _deferredNativeEntryId ||
        !_viewerViewportSize.isEmpty()) {
        _deferredNativeEntryId.clear();
        invalidateNativeDwell();
        return;
    }
    constexpr int NativeWindow = 5; // current +/-2 images
    if (!viewerFitWindowReady(
            row, NativeWindow, _lastViewerFitViewportSize)) {
        if (_nativeDwellTimer.isActive()) {
            invalidateNativeDwell();
        }
        return;
    }
    if (_nativeDwellTimer.isActive() &&
        _scheduledNativeDwellGeneration == _nativeDwellGeneration &&
        _scheduledNativeDwellEntryId == _deferredNativeEntryId &&
        _scheduledNativeDwellFitViewportSize ==
            _lastViewerFitViewportSize) {
        return;
    }
    _scheduledNativeDwellGeneration = _nativeDwellGeneration;
    _scheduledNativeDwellEntryId = _deferredNativeEntryId;
    _scheduledNativeDwellFitViewportSize = _lastViewerFitViewportSize;
    _nativeDwellTimer.start(NativeDwellMs);
}

void ExternalCatalogModel::finishDeferredNativeDwell() {
    constexpr int NativeWindow = 5; // current +/-2 images
    const QString entryId = _scheduledNativeDwellEntryId;
    const QSize fitViewportSize = _scheduledNativeDwellFitViewportSize;
    const quint64 generation = _scheduledNativeDwellGeneration;
    _scheduledNativeDwellEntryId.clear();
    _scheduledNativeDwellFitViewportSize = {};
    if (_shutdown || generation != _nativeDwellGeneration ||
        entryId.isEmpty() || entryId != _deferredNativeEntryId ||
        entryId != _viewerEntryId || !_viewerViewportSize.isEmpty() ||
        fitViewportSize != _lastViewerFitViewportSize) {
        return;
    }
    const int row = rowForEntryId(entryId);
    if (!validRow(row) ||
        !viewerFitWindowReady(row, NativeWindow, fitViewportSize)) {
        return;
    }
    _deferredNativeEntryId.clear();
    const QSize nativeSentinel(0, 0);
    _viewerPlans.insert(entryId, {nativeSentinel, NativeWindow});
    scheduleViewerDecodeAt(row, nativeSentinel, NativeWindow);
}

void ExternalCatalogModel::invalidateNativeDwell() {
    ++_nativeDwellGeneration;
    _nativeDwellTimer.stop();
    _scheduledNativeDwellGeneration = 0;
    _scheduledNativeDwellEntryId.clear();
    _scheduledNativeDwellFitViewportSize = {};
}

void ExternalCatalogModel::requestImageMetadataForRow(
    int row, bool highPriority) {
    if (_shutdown || !validRow(row)) {
        return;
    }
    QList<int> &queue = highPriority ? _metadataUrgentRows
                                     : _metadataAdHocRows;
    if (!queue.contains(row)) {
        queue.append(row);
    }
    if (!probeResolvedFor(loadedEntry(row))) {
        QList<int> &probeQueue = highPriority
            ? _probeUrgentRows : _probeOverscanRows;
        if (!probeQueue.contains(row)) {
            probeQueue.append(row);
        }
        scheduleProbePump();
    }
    // Before the catalog barrier only an explicit high-priority viewer row
    // may materialize. Masonry visible/overscan requests stay in their queues
    // until every bounded probe has produced an outcome.
    const Entry &entry = loadedEntry(row);
    const bool explicitCurrentViewer = highPriority &&
        entry.id == _viewerEntryId;
    if (!_catalogProbeRequested || _probePassComplete ||
        (explicitCurrentViewer && probeResolvedFor(entry))) {
        scheduleMetadataPump();
    }
}

void ExternalCatalogModel::scheduleMetadataPump() {
    if (_shutdown || _metadataPumpScheduled) {
        return;
    }
    _metadataPumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        _metadataPumpScheduled = false;
        pumpMetadataRequests();
    });
}

void ExternalCatalogModel::pumpMetadataRequests() {
    ExternalCatalogMetadataPlanner planner(*this);
    planner.run();
}

void ExternalCatalogModel::scheduleMetadataRetry(
    const QString &sourceIdentity, const QString &contentVersion,
    const QString &resourceId, bool background) {
    const QString retryKey = sourceIdentity + QChar(0x1f) +
        contentVersion;
    if (_metadataRetryScheduled.contains(retryKey)) {
        return;
    }
    const int MaxAutomaticAttempts = background ? 1 : 3;
    const int attempt = _metadataRetryAttempts.value(retryKey) + 1;
    if (attempt > MaxAutomaticAttempts) {
        return;
    }
    _metadataRetryAttempts.insert(retryKey, attempt);
    _metadataRetryScheduled.insert(retryKey);
    const int delayMs = static_cast<int>(qMin<qint64>(
        30000, 500LL << qMin(attempt - 1, 6)));
    if (background) {
        _backgroundMetadataRetries.insert(retryKey,
                                          BackgroundMetadataRetry{
            .sourceIdentity = sourceIdentity,
            .contentVersion = contentVersion,
            .resourceId = resourceId,
            .notBeforeMs = QDateTime::currentMSecsSinceEpoch() + delayMs,
        });
        scheduleBackgroundRetryWake();
        return;
    }
    QTimer::singleShot(delayMs, this,
                       [this, sourceIdentity, contentVersion, resourceId,
                        retryKey]() {
        _metadataRetryScheduled.remove(retryKey);
        if (_shutdown) {
            return;
        }
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row)) {
            _metadataRetryAttempts.remove(retryKey);
            return;
        }
        const Entry &entry = loadedEntry(row);
        if (!entry.image || entry.contentVersion != contentVersion ||
            entry.source.resourceId != resourceId ||
            _metadataResolvedPaths.contains(sourceIdentity) ||
            _metadataPendingVersions.contains(sourceIdentity)) {
            return;
        }
        if (!_metadataAdHocRows.contains(row)) {
            _metadataAdHocRows.append(row);
        }
        scheduleMetadataPump();
    });
}

void ExternalCatalogModel::scheduleBackgroundRetryWake() {
    if (_shutdown) {
        return;
    }
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (const BackgroundMetadataRetry &retry :
         std::as_const(_backgroundMetadataRetries)) {
        earliest = qMin(earliest, retry.notBeforeMs);
    }
    for (const BackgroundDecodeRetry &retry :
         std::as_const(_backgroundDecodeRetries)) {
        earliest = qMin(earliest, retry.notBeforeMs);
    }
    if (earliest == std::numeric_limits<qint64>::max()) {
        _backgroundRetryTimer.stop();
        return;
    }
    const int delayMs = static_cast<int>(qBound<qint64>(
        qint64{0}, earliest - QDateTime::currentMSecsSinceEpoch(),
        qint64{30000}));
    if (_backgroundRetryTimer.isActive() &&
        _backgroundRetryTimer.remainingTime() <= delayMs) {
        return;
    }
    _backgroundRetryTimer.start(delayMs);
}

void ExternalCatalogModel::processBackgroundRetries() {
    if (_shutdown) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = _backgroundMetadataRetries.begin();
         it != _backgroundMetadataRetries.end();) {
        if (it->notBeforeMs > now) {
            ++it;
            continue;
        }
        const QString retryKey = it.key();
        const BackgroundMetadataRetry retry = it.value();
        it = _backgroundMetadataRetries.erase(it);
        _metadataRetryScheduled.remove(retryKey);
        const int row = _sourceToRow.value(retry.sourceIdentity, -1);
        if (!validRow(row)) {
            _metadataRetryAttempts.remove(retryKey);
            continue;
        }
        const Entry &entry = loadedEntry(row);
        if (!entry.image ||
            entry.contentVersion != retry.contentVersion ||
            entry.source.resourceId != retry.resourceId ||
            _metadataResolvedPaths.contains(retry.sourceIdentity) ||
            _metadataPendingVersions.contains(retry.sourceIdentity)) {
            continue;
        }
        if (!_metadataAdHocRows.contains(row)) {
            _metadataAdHocRows.append(row);
        }
    }

    for (auto it = _backgroundDecodeRetries.begin();
         it != _backgroundDecodeRetries.end();) {
        if (it->notBeforeMs > now) {
            ++it;
            continue;
        }
        const QString retryKey = it.key();
        const ImageDecodeRequest request = it->request;
        it = _backgroundDecodeRetries.erase(it);
        _sourceDecodeRetryScheduled.remove(retryKey);
        const QString sourceIdentity = request.info.sourceIdentity();
        const int row = _sourceToRow.value(sourceIdentity, -1);
        if (!validRow(row) ||
            loadedEntry(row).contentVersion !=
                request.info.sourceVersionToken ||
            loadedEntry(row).source.resourceId !=
                request.info.source.resourceId) {
            _sourceDecodeRetryAttempts.remove(retryKey);
            continue;
        }
        if (_catalogFitStarted &&
            !_catalogFitResolvedSources.contains(sourceIdentity) &&
            !_catalogFitRows.contains(row)) {
            _catalogFitRows.prepend(row);
        }
    }

    scheduleMetadataPump();
    scheduleCatalogFitPump();
    const auto plans = _viewerPlans;
    for (auto plan = plans.cbegin(); plan != plans.cend(); ++plan) {
        const int centerRow = rowForEntryId(plan.key());
        if (validRow(centerRow)) {
            scheduleViewerDecodeAt(centerRow, plan->viewportSize,
                                   plan->prefetchCount);
        }
    }
    scheduleBackgroundRetryWake();
}

void ExternalCatalogModel::resetMetadataPlanner() {
    _metadataPendingVersions.clear();
    _metadataResolvedPaths.clear();
    _metadataVisibleRows.clear();
    _metadataLastVisibleRows.clear();
    _metadataOverscanRows.clear();
    _metadataUrgentRows.clear();
    _metadataAdHocRows.clear();
    _catalogMetadataCursor = 0;
    _metadataPeakPending = 0;
    _metadataSubmittedBatches = 0;
    _catalogMetadataRequested = false;
    _metadataPumpScheduled = false;
    _metadataRetryAttempts.clear();
    _metadataRetryScheduled.clear();
    _backgroundMetadataRetries.clear();
    if (_backgroundDecodeRetries.isEmpty()) {
        _backgroundRetryTimer.stop();
    }
}

void ExternalCatalogModel::scheduleViewerDecode() {
    const int row = rowForEntryId(_viewerEntryId);
    if (_shutdown || !validRow(row) || !_viewerViewportSize.isValid()) {
        return;
    }
    const bool nativeRequest = _viewerViewportSize.isEmpty();
    const int prefetchCount = nativeRequest ? 5 : 16;
    _viewerPlans.insert(
        _viewerEntryId, {_viewerViewportSize, prefetchCount});
    if (nativeRequest) {
        _deferredNativeEntryId = _viewerEntryId;
        scheduleViewerDecodeAt(
            row, _lastViewerFitViewportSize, prefetchCount);
        tryScheduleDeferredNative();
        return;
    }
    scheduleViewerDecodeAt(row, _viewerViewportSize, prefetchCount);
}

void ExternalCatalogModel::scheduleViewerDecodeAt(
    int row, const QSize &viewportSize, int prefetchCount) {
    if (_shutdown || !validRow(row) || !loadedEntry(row).image ||
        !viewportSize.isValid() || prefetchCount <= 0) {
        return;
    }

    const bool probeBarrierActive =
        _catalogProbeRequested && !_probePassComplete;
    const int effectivePrefetchCount = probeBarrierActive ? 1 : prefetchCount;
    const bool explicitCurrentPlan = loadedEntry(row).id == _viewerEntryId;
    const QList<int> candidates = viewerCandidateRows(
        row, effectivePrefetchCount);
    for (const int candidateRow : candidates) {
        const Entry &candidate = loadedEntry(candidateRow);
        if (!candidate.originalSize.isValid() &&
            (!candidate.item || !candidate.item->fullSize().isValid())) {
            requestImageMetadataForRow(
                candidateRow,
                candidateRow == row && explicitCurrentPlan);
        }
    }

    int windowCenterRow = -1;
    const QList<ImageFile *> windowItems = viewerItems(
        row, effectivePrefetchCount, &windowCenterRow);
    ViewerImageCache::RequestPlan plan = _viewerImageCache.planRequest(
        windowItems, windowCenterRow, viewportSize,
        effectivePrefetchCount,
        [this](ImageDecodeRequest &request) {
            stabilizeViewerFitRequest(request);
        });
    if (!plan.cachedImages.isEmpty()) {
        emit viewerSourceAtChanged(row);
        if (loadedEntry(row).id == _viewerEntryId) {
            notifyViewerImageUrlChanged();
        }
    }

    QList<ImageDecodeRequest> requests;
    requests.reserve(plan.decodeRequests.size());
    for (ImageDecodeRequest request : std::as_const(plan.decodeRequests)) {
        request.requestNamespace = _sessionId;
        request.info.requestNamespace = _sessionId;
        const int requestRow = _sourceToRow.value(
            request.info.sourceIdentity(), -1);
        if (validRow(requestRow)) {
            const Entry &sourceEntry = loadedEntry(requestRow);
            request.info.source = sourceEntry.source;
            request.info.path = sourceEntry.sourceIdentity;
            request.info.sourceVersionToken =
                sourceEntry.contentVersion;
        }
        // Fit artifacts are safe to persist by opaque source revision. Native
        // frames remain RAM-only and are produced only for the deferred
        // current +/-2 window.
        request.checkCache = request.fitToViewerRequest;
        request.expandToCacheResolution = false;
        request.storeInPersistentCache = request.fitToViewerRequest;
        const QString key = viewerRequestKey(request);
        if (_pendingViewerRequests.contains(key)) {
            if (!_catalogFitPendingKeys.contains(key)) {
                continue;
            }
            // Upgrade the interactive subscriber. The background decode may
            // still finish, but shared materialization prevents a second
            // download and this request enters the viewer priority band.
            requests.append(request);
            continue;
        }
        _pendingViewerRequests.insert(key);
        requests.append(request);
    }
    if (!requests.isEmpty()) {
        _decodeManager->decodeImages(requests);
    }
}

QList<int> ExternalCatalogModel::viewerCandidateRows(
    int row, int count) const {
    QList<int> result;
    if (!validRow(row) || count <= 0) {
        return result;
    }

    bool hitStart = false;
    bool hitEnd = false;
    for (int counter = 0;
         result.size() < count && !(hitStart && hitEnd); ++counter) {
        const int candidate = counter % 2 == 0
            ? row + counter / 2
            : row - (counter + 1) / 2;
        if (candidate < 0) {
            hitStart = true;
        }
        if (candidate >= logicalRowCount()) {
            hitEnd = true;
        }
        if (validRow(candidate) && loadedEntry(candidate).image) {
            result.append(candidate);
        }
    }
    return result;
}

QList<ImageFile *> ExternalCatalogModel::viewerItems(
    int centerRow, int prefetchCount, int *windowCenterRow) const {
    if (windowCenterRow) {
        *windowCenterRow = -1;
    }
    const QList<int> rows = viewerCandidateRows(centerRow, prefetchCount);
    if (rows.isEmpty()) {
        return {};
    }
    const auto [minimum, maximum] = std::minmax_element(
        rows.cbegin(), rows.cend());
    QList<ImageFile *> result(*maximum - *minimum + 1, nullptr);
    for (const int row : rows) {
        result[row - *minimum] = ensureItem(row);
    }
    if (windowCenterRow) {
        *windowCenterRow = centerRow - *minimum;
    }
    return result;
}

void ExternalCatalogModel::notifyViewerImageUrlChanged() {
    const int row = rowForEntryId(_viewerEntryId);
    const QString nextUrl = validRow(row)
        ? bestViewerImageUrlAt(row) : QString();
    if (_lastViewerImageUrl == nextUrl) {
        return;
    }
    _lastViewerImageUrl = nextUrl;
    emit viewerImageUrlChanged();
}

QString ExternalCatalogModel::viewerRequestKey(
    const ImageDecodeRequest &request) const {
    const qint64 sourceSize = request.info.source.isValid()
        ? request.info.source.size : request.info.fileSize;
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4x%5\x1f%6")
        .arg(request.info.sourceIdentity())
        .arg(request.info.sourceVersionToken)
        .arg(sourceSize)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(request.fitToViewerRequest ? 1 : 0);
}

QString ExternalCatalogModel::thumbnailRequestKey(
    const ImageDecodeRequest &request) const {
    const qint64 sourceSize = request.info.source.isValid()
        ? request.info.source.size : request.info.fileSize;
    return QStringLiteral("%1\x1f%2\x1f%3\x1f%4x%5\x1f%6")
        .arg(request.info.sourceIdentity())
        .arg(request.info.sourceVersionToken)
        .arg(sourceSize)
        .arg(request.targetSize.width())
        .arg(request.targetSize.height())
        .arg(thumbnailTransformKey(request));
}

bool ExternalCatalogModel::validRow(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded;
}

QString ExternalCatalogModel::nextImageId(const Entry &entry) {
    return QStringLiteral("%1-%2-v%3-s%4-%5")
        .arg(_sessionId)
        .arg(QString::number(qHash(entry.id), 16))
        .arg(entry.contentVersion)
        .arg(entry.size)
        .arg(++_nextImageSerial);
}


} // namespace ZoinGallery
