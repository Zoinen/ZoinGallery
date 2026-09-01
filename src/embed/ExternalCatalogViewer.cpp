#include "ExternalCatalogModelPrivate.h"
#include "ExternalCatalogThumbnailPlanner.h"

namespace ZoinGallery {

QString ExternalCatalogModel::viewerImageUrlAt(int row) const {
    if (!validRow(row)) {
        return {};
    }
    const Entry &entry = loadedEntry(row);
    if (entry.id == _viewerEntryId) {
        const auto sources = viewerImageSourcesAt(row);
        return sources.isEmpty() ? QString() : sources.constLast().first;
    }
    ImageFile *item = ensureItem(row);
    return item ? item->imageIdUrl() : QString();
}

QString ExternalCatalogModel::bestViewerImageUrlAt(int row) const {
    const auto sources = viewerImageSourcesAt(row);
    return sources.isEmpty() ? QString() : sources.constLast().first;
}

QList<QPair<QString, int>> ExternalCatalogModel::viewerImageSourcesAt(
    int row) const {
    if (!validRow(row)) {
        return {};
    }

    QSize viewerSize = _viewerViewportSize;
    const Entry &entry = loadedEntry(row);
    const auto plan = _viewerPlans.constFind(entry.id);
    if (plan != _viewerPlans.constEnd()) {
        viewerSize = plan->viewportSize;
    }
    return _viewerImageCache.imageSources(
        ensureItem(row), viewerSize,
        [this](ImageDecodeRequest &request) {
            stabilizeViewerFitRequest(request);
        });
}

void ExternalCatalogModel::requestViewer(
    int row, const QSize &viewportSize) {
    if (_shutdown || !validRow(row) || !loadedEntry(row).image ||
        !viewportSize.isValid()) {
        clearViewer();
        return;
    }

    const QString entryId = loadedEntry(row).id;
    const bool entryChanged = _viewerEntryId != entryId;
    const bool viewportChanged = _viewerViewportSize != viewportSize;
    if (entryChanged || viewportChanged) {
        invalidateNativeDwell();
    }
    _viewerViewportSize = viewportSize;
    const bool nativeRequest = viewportSize.isEmpty();
    if (!nativeRequest) {
        _lastViewerFitViewportSize = viewportSize;
        _deferredNativeEntryId.clear();
    }
    if (entryChanged || viewportChanged) {
        // Supplemental swipe plans are meaningful only for the viewport and
        // presentation mode which created them.  In particular, a native
        // 0x0 plan must never make a later Fit swipe select level 2, and an
        // older Fit size must not advertise an undersized transition frame.
        _viewerPlans.clear();
    }
    if (entryChanged) {
        _viewerEntryId = entryId;
        notifyViewerImageUrlChanged();
    }
    const int prefetchCount = nativeRequest ? 5 : 16;
    _viewerPlans.insert(entryId, {viewportSize, prefetchCount});
    if (nativeRequest) {
        // First guarantee a usable Fit base for current +/-2. Native pixels
        // are admitted only after a separate dwell below, so a quick swipe or
        // resize cannot spend bandwidth on a frame the user never examines.
        _deferredNativeEntryId = entryId;
        scheduleViewerDecodeAt(
            row, _lastViewerFitViewportSize, prefetchCount);
        tryScheduleDeferredNative();
        return;
    }
    scheduleViewerDecodeAt(row, viewportSize, prefetchCount);
}

void ExternalCatalogModel::requestViewerAt(
    int row, const QSize &viewportSize) {
    if (_shutdown || !validRow(row) || !loadedEntry(row).image ||
        !viewportSize.isValid()) {
        return;
    }

    const QString entryId = loadedEntry(row).id;
    const bool nativeRequest = viewportSize.isEmpty();
    const int prefetchCount = entryId == _viewerEntryId
        ? (nativeRequest ? 5 : 16) : 1;
    if (!_viewerPlans.contains(entryId) &&
        _viewerPlans.size() >= 3) {
        const ViewerPlan activePlan = _viewerPlans.value(_viewerEntryId);
        _viewerPlans.clear();
        if (!_viewerEntryId.isEmpty() &&
            activePlan.viewportSize.isValid()) {
            _viewerPlans.insert(_viewerEntryId, activePlan);
        }
    }
    _viewerPlans.insert(entryId, {viewportSize, prefetchCount});
    if (nativeRequest && entryId == _viewerEntryId) {
        if (_deferredNativeEntryId != entryId) {
            invalidateNativeDwell();
        }
        _deferredNativeEntryId = entryId;
        scheduleViewerDecodeAt(
            row, _lastViewerFitViewportSize, prefetchCount);
        tryScheduleDeferredNative();
    }
    else {
        scheduleViewerDecodeAt(row, viewportSize, prefetchCount);
    }
}

void ExternalCatalogModel::setViewerIndex(int row) {
    if (_shutdown || !validRow(row) || !loadedEntry(row).image) {
        clearViewer();
        return;
    }

    const QString entryId = loadedEntry(row).id;
    if (_viewerEntryId == entryId) {
        scheduleViewerDecode();
        return;
    }

    invalidateNativeDwell();
    _viewerEntryId = entryId;
    _viewerPlans.clear();
    if (_viewerViewportSize.isValid()) {
        const int prefetchCount = _viewerViewportSize.isEmpty() ? 5 : 16;
        _viewerPlans.insert(
            entryId, {_viewerViewportSize, prefetchCount});
    }
    notifyViewerImageUrlChanged();
    scheduleViewerDecode();
}

void ExternalCatalogModel::clearViewer() {
    const bool hadViewer = !_viewerEntryId.isEmpty();
    _decodeManager->cancelViewerRequests(_sessionId);
    _pendingViewerRequests.clear();
    _deferredNativeEntryId.clear();
    invalidateNativeDwell();
    _viewerEntryId.clear();
    _viewerViewportSize = {};
    _viewerPlans.clear();
    if (hadViewer) {
        notifyViewerImageUrlChanged();
    }
}

qsizetype ExternalCatalogModel::viewerFitFrameCount() const {
    return _viewerImageCache.viewerImageCount();
}

qsizetype ExternalCatalogModel::viewerNativeFrameCount() const {
    return _viewerImageCache.fullSizeImageCount();
}

qint64 ExternalCatalogModel::viewerFitRetainedBytes() const {
    return _viewerImageCache.fitRetainedBytes();
}

qint64 ExternalCatalogModel::viewerNativeRetainedBytes() const {
    return _viewerImageCache.nativeRetainedBytes();
}

qint64 ExternalCatalogModel::viewerFitByteBudget() const {
    return _viewerImageCache.fitByteBudget();
}

qint64 ExternalCatalogModel::viewerNativeByteBudget() const {
    return _viewerImageCache.nativeByteBudget();
}

qsizetype ExternalCatalogModel::metadataPendingRequestCount() const {
    return _metadataPendingVersions.size();
}

qsizetype ExternalCatalogModel::metadataPeakPendingRequestCount() const {
    return _metadataPeakPending;
}

quint64 ExternalCatalogModel::metadataSubmittedBatchCount() const {
    return _metadataSubmittedBatches;
}

void ExternalCatalogModel::decodeImages(
    const QList<ImageDecodeRequest> &requests) {
    ExternalCatalogThumbnailPlanner planner(*this, requests);
    planner.run();
}

void ExternalCatalogModel::requestImageMetadata(
    const QList<int> &rows, bool highPriority, bool catalogWide) {
    if (_shutdown) {
        return;
    }

    const auto normalizedRows = [this](const QList<int> &requested) {
        QList<int> result;
        result.reserve(requested.size());
        QSet<int> seen;
        for (const int row : requested) {
            if (!validRow(row) || seen.contains(row)) {
                continue;
            }
            seen.insert(row);
            result.append(row);
        }
        return result;
    };

    const QList<int> normalized = normalizedRows(rows);

    // Masonry sends an additional empty catalogWide request after its
    // visible and overscan requests. Do not let that marker erase the active
    // overscan window.
    if (!catalogWide || !rows.isEmpty()) {
        if (highPriority) {
            _metadataVisibleRows = normalized;
            _metadataLastVisibleRows = QSet<int>(
                _metadataVisibleRows.cbegin(),
                _metadataVisibleRows.cend());
        }
        else {
            _metadataOverscanRows = normalized;
        }
        enqueueProbeRows(normalized, highPriority);
    }
    // Every renderer pass starts with its visible request. Fixed modes never
    // send the trailing catalogWide marker, so this pauses (without losing
    // the scan cursor) any Masonry background walk after a mode switch.
    // Masonry sends catalogWide synchronously before the zero-delay pump and
    // therefore re-enables the same generation without churn.
    _catalogMetadataRequested = catalogWide;
    if (catalogWide) {
        // A direct-local catalog keeps the historical viewport-first
        // metadata pipeline.  Starting an otherwise empty probe pass here
        // would call beginCatalogFitPass(), emit a catalog-wide dataChanged,
        // and let that follow-up layout pass revoke the Masonry metadata
        // lease.  The embedded-first barrier is exclusively a policy for
        // sources whose bytes are expensive to obtain.
        _catalogProbeRequested = std::any_of(
            _entries.cbegin(), _entries.cend(), [](const Entry &entry) {
                return entry.image && entry.source.isValid() &&
                    expensiveSource(entry.source);
            });
    }
    scheduleProbePump();
    const bool hasProbeEligibleRow = std::any_of(
        normalized.cbegin(), normalized.cend(), [this](int row) {
            return validRow(row) && probeResolvedFor(loadedEntry(row));
        });
    if (!_catalogProbeRequested || _probePassComplete ||
        hasProbeEligibleRow) {
        scheduleMetadataPump();
    }
}

void ExternalCatalogModel::cancelAllRunners() {
    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    resetProgressivePipeline();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _sourceDecodeRetryAttempts.clear();
    _sourceDecodeRetryScheduled.clear();
    _backgroundDecodeRetries.clear();
    _backgroundMetadataRetries.clear();
    _backgroundRetryTimer.stop();
    invalidateNativeDwell();
}

void ExternalCatalogModel::cancelAllDecodeRunners() {
    // MasonryLayout calls this before replacing DPR/geometry-specific tiles.
    // Expensive source reads/materializations remain reusable across a small
    // layout or DPR change. Their stale presentation subscriber is forgotten
    // below, while completion may still populate a covering tier. Local files
    // retain the historical aggressive cancellation policy.
    const bool hasExpensiveSource = std::any_of(
        _entries.cbegin(), _entries.cend(), [](const Entry &entry) {
            return entry.image && expensiveSource(entry.source);
        });
    if (!hasExpensiveSource) {
        _decodeManager->cancelThumbnailRequests(_sessionId);
        if (_thumbnailCache) {
            _thumbnailCache->cancelRequests(_sessionId);
        }
    }
    // When expensive work remains alive, its shared-cache Owner admission must
    // remain alive with it. Dropping only that admission would let the same
    // stable tier become Owner again and launch a duplicate decode after every
    // geometry/DPR lease change. This map is merely the stale local waiter;
    // the completing owner will still publish into ThumbnailMemoryCache.
    _pendingThumbnailRequests.clear();
}

bool ExternalCatalogModel::preserveViewStateOnReset() const {
    return true;
}

void ExternalCatalogModel::shutdown() {
    if (_shutdown) {
        return;
    }
    _shutdown = true;
    _decodeManager->cancelRequests(_sessionId);
    if (_thumbnailCache) {
        _thumbnailCache->cancelRequests(_sessionId);
    }
    resetMetadataPlanner();
    resetProgressivePipeline();
    _pendingViewerRequests.clear();
    _pendingThumbnailRequests.clear();
    _sourceDecodeRetryAttempts.clear();
    _sourceDecodeRetryScheduled.clear();
    _backgroundDecodeRetries.clear();
    _backgroundMetadataRetries.clear();
    _backgroundRetryTimer.stop();
    _viewerPlans.clear();
    clearViewer();
    _viewerImageCache.clear();
    deleteRetiredItems();
    for (Entry &entry : _entries) {
        clearPublishedImage(entry);
    }
}


} // namespace ZoinGallery
