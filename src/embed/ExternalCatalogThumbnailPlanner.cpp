#include "ExternalCatalogThumbnailPlanner.h"

#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

ExternalCatalogThumbnailPlanner::ExternalCatalogThumbnailPlanner(
    ExternalCatalogModel &model,
    const QList<ImageDecodeRequest> &requests)
    : m_model(model), m_requests(requests) {
}

bool ExternalCatalogThumbnailPlanner::resolveRequest(
    ImageDecodeRequest &request, int &row, Entry *&entry,
    ImageFile *&item) {
    if (!request.targetSize.isValid() || request.targetSize.width() <= 0
        || request.targetSize.height() <= 0) {
        return false;
    }
    row = m_model._sourceToRow.value(request.info.sourceIdentity(), -1);
    if (!m_model.validRow(row) || !m_model.loadedEntry(row).image) {
        return false;
    }
    entry = &m_model.loadedEntry(row);
    item = m_model.ensureItem(row);
    return item != nullptr;
}

void ExternalCatalogThumbnailPlanner::serviceProbeBarrier(
    int row, const Entry &entry, const ImageDecodeRequest &request) {
    if (expensiveSource(entry.source)
        && m_model._probeRetryableSources.contains(
            entry.source.cacheKey())) {
        m_model.enqueueProbeRows({row}, request.highPriority);
        m_model.scheduleProbePump();
    }
    if (!m_model._catalogProbeRequested || m_model._probePassComplete
        || !expensiveSource(entry.source)
        || m_model.probeResolvedFor(entry)) {
        return;
    }
    traceAdmission(request, row, QStringLiteral("probe-barrier"));
    m_model.enqueueProbeRows({row}, request.highPriority);
    m_model.scheduleProbePump();
}

void ExternalCatalogThumbnailPlanner::configureRequest(
    ImageDecodeRequest &request, const Entry &entry) const {
    request.requestNamespace = m_model._sessionId;
    request.info.requestNamespace = m_model._sessionId;
    request.info.source = entry.source;
    request.info.path = entry.sourceIdentity;
    request.info.sourceVersionToken = entry.contentVersion;
    request.checkCache = true;
    request.expandToCacheResolution = false;
    request.storeInPersistentCache = true;
    request.thumbnailTransformKey = thumbnailTransformKey(request);
    request.targetSize = stableDecodeTarget(
        request.targetSize,
        entry.originalSize.isValid()
            ? entry.originalSize
            : entry.item ? entry.item->fullSize() : QSize(),
        entry.thumbnailRequestedSize, DecodeSizeFamily::Thumbnail,
        expensiveSource(entry.source));
}

void ExternalCatalogThumbnailPlanner::clearSupersededTarget(
    const Entry &entry, ImageFile *item,
    const ImageDecodeRequest &request) {
    if (!entry.thumbnailRequestedSize.isValid()
        || (entry.thumbnailRequestedSize == request.targetSize
            && entry.thumbnailTransformKey
                == request.thumbnailTransformKey)) {
        return;
    }
    ImageDecodeRequest superseded;
    superseded.info = item->info();
    superseded.targetSize = entry.thumbnailRequestedSize;
    superseded.thumbnailTransformKey = entry.thumbnailTransformKey;
    m_model._pendingThumbnailRequests.remove(
        m_model.thumbnailRequestKey(superseded));
}

void ExternalCatalogThumbnailPlanner::traceAdmission(
    const ImageDecodeRequest &request, int row, const QString &outcome,
    const QString &providerId) const {
    QVariantMap fields{{QStringLiteral("row"), row},
                       {QStringLiteral("outcome"), outcome}};
    if (!providerId.isEmpty()) {
        fields.insert(QStringLiteral("providerId"), providerId);
    }
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.thumbnail.admission"),
        MediaTimingTrace::mergedFields(
            decodeRequestTimingFields(request), fields));
}

void ExternalCatalogThumbnailPlanner::admitRequest(
    ImageDecodeRequest request) {
    int row = -1;
    Entry *entry = nullptr;
    ImageFile *item = nullptr;
    if (!resolveRequest(request, row, entry, item)) {
        return;
    }
    serviceProbeBarrier(row, *entry, request);
    configureRequest(request, *entry);
    clearSupersededTarget(*entry, item, request);
    entry->thumbnailRequestedSize = request.targetSize;
    entry->thumbnailTransformKey = request.thumbnailTransformKey;

    const QString key = m_model.thumbnailRequestKey(request);
    if (m_model._pendingThumbnailRequests.contains(key)) {
        traceAdmission(request, row, QStringLiteral("local-pending"));
        return;
    }
    const ThumbnailMemoryCache::AcquireResult cached =
        m_model._thumbnailCache->acquire(
            m_model._sessionId, entry->sourceIdentity,
            entry->contentVersion, entry->size, request.targetSize,
            request.thumbnailTransformKey);
    if (cached.state == ThumbnailMemoryCache::AcquireState::Hit) {
        traceAdmission(request, row, QStringLiteral("hit"),
                       cached.handle.providerId);
        m_model.attachThumbnail(row, cached.handle.providerId);
        return;
    }
    m_model._pendingThumbnailRequests.insert(
        key, ExternalCatalogModel::PendingThumbnailRequest{
            .owner = cached.state
                == ThumbnailMemoryCache::AcquireState::Owner,
            .admittedTargetSize = cached.pendingTargetSize.isValid()
                ? cached.pendingTargetSize : request.targetSize,
            .admittedTransformKey = cached.pendingTransformKey.isEmpty()
                ? request.thumbnailTransformKey
                : cached.pendingTransformKey,
        });
    if (cached.state == ThumbnailMemoryCache::AcquireState::Owner) {
        m_submitted.append(request);
    }
    traceAdmission(
        request, row,
        cached.state == ThumbnailMemoryCache::AcquireState::Owner
            ? QStringLiteral("owner")
            : QStringLiteral("shared-pending"));
}

void ExternalCatalogThumbnailPlanner::submit() {
    if (m_submitted.isEmpty()) {
        return;
    }
    MediaTimingTrace::event(
        QStringLiteral("qt.gallery.thumbnail.batch_submitted"), {
            {QStringLiteral("sessionId"), m_model._sessionId},
            {QStringLiteral("requested"), m_requests.size()},
            {QStringLiteral("submitted"), m_submitted.size()},
        });
    m_model._decodeManager->decodeImages(m_submitted);
}

void ExternalCatalogThumbnailPlanner::run() {
    if (m_model._shutdown) {
        return;
    }
    m_submitted.reserve(m_requests.size());
    for (const ImageDecodeRequest &request : m_requests) {
        admitRequest(request);
    }
    submit();
}

} // namespace ZoinGallery
