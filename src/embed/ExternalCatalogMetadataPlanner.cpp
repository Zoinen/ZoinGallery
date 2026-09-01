#include "ExternalCatalogMetadataPlanner.h"

#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

ExternalCatalogMetadataPlanner::ExternalCatalogMetadataPlanner(
    ExternalCatalogModel &model)
    : m_model(model) {
}

bool ExternalCatalogMetadataPlanner::rowNeedsMetadata(int row) const {
    if (!m_model.validRow(row)) {
        return false;
    }
    const ExternalCatalogModel::Entry &entry = m_model.loadedEntry(row);
    if (!entry.image || !entry.source.isValid()
        || entry.originalSize.isValid()
        || (entry.item && entry.item->fullSize().isValid())) {
        return false;
    }
    return !m_model._metadataPendingVersions.contains(entry.sourceIdentity)
        && !m_model._metadataResolvedPaths.contains(entry.sourceIdentity);
}

bool ExternalCatalogMetadataPlanner::hasEligibleRow(
    const QList<int> &rows) const {
    return std::any_of(rows.cbegin(), rows.cend(),
                       [this](int row) { return rowNeedsMetadata(row); });
}

bool ExternalCatalogMetadataPlanner::prepareCapacity() {
    if (m_model._shutdown) {
        return false;
    }
    const qsizetype pending = m_model._metadataPendingVersions.size();
    const bool foregroundWaiting =
        hasEligibleRow(m_model._metadataUrgentRows)
        || hasEligibleRow(m_model._metadataVisibleRows);
    if (!foregroundWaiting
        && pending > ExternalCatalogModel::metadataRefillLowWatermark()) {
        return false;
    }
    m_available = ExternalCatalogModel::metadataRequestLimit() - pending;
    if (m_available <= 0) {
        return false;
    }
    m_highRequests.reserve(m_available);
    m_backgroundRequests.reserve(m_available);
    return true;
}

void ExternalCatalogMetadataPlanner::appendRow(
    int row, QList<Request> &requests) {
    if (m_available <= 0 || !rowNeedsMetadata(row)) {
        return;
    }
    const ExternalCatalogModel::Entry &entry = m_model.loadedEntry(row);
    m_model._metadataPendingVersions.insert(
        entry.sourceIdentity, entry.contentVersion);
    requests.append({entry.sourceIdentity, entry.contentVersion,
                     entry.source});
    --m_available;
}

void ExternalCatalogMetadataPlanner::drainRows(
    QList<int> &rows, QList<Request> &requests) {
    while (m_available > 0 && !rows.isEmpty()) {
        appendRow(rows.takeFirst(), requests);
    }
}

void ExternalCatalogMetadataPlanner::drainCurrentViewer(
    QList<int> &rows, QList<Request> &requests) {
    int candidates = rows.size();
    while (m_available > 0 && candidates-- > 0 && !rows.isEmpty()) {
        const int row = rows.takeFirst();
        if (!m_model.validRow(row)
            || m_model.loadedEntry(row).id != m_model._viewerEntryId
            || !m_model.probeResolvedFor(m_model.loadedEntry(row))) {
            rows.append(row);
            continue;
        }
        appendRow(row, requests);
    }
}

bool ExternalCatalogMetadataPlanner::serviceProbeBarrier() {
    if (!m_model._catalogProbeRequested || m_model._probePassComplete) {
        return false;
    }
    drainCurrentViewer(m_model._metadataUrgentRows, m_highRequests);
    if (!m_highRequests.isEmpty()) {
        ++m_model._metadataSubmittedBatches;
        m_model._decodeManager->readVersionedImagesInfo(
            m_highRequests, false, true, m_model._sessionId);
    }
    return true;
}

void ExternalCatalogMetadataPlanner::collectCatalogRows() {
    if (!m_model._catalogMetadataRequested) {
        return;
    }
    while (m_available > 0
           && m_model._catalogMetadataCursor < m_model._entries.size()) {
        const int row = m_model._entries.at(
            m_model._catalogMetadataCursor++).sourceIndex;
        appendRow(row, m_backgroundRequests);
    }
}

void ExternalCatalogMetadataPlanner::submitRequests() {
    m_model._metadataPeakPending = qMax(
        m_model._metadataPeakPending,
        static_cast<qsizetype>(m_model._metadataPendingVersions.size()));
    if (m_highRequests.isEmpty() && m_backgroundRequests.isEmpty()) {
        return;
    }
    for (Request &request : m_highRequests) {
        request.highPriority = true;
    }
    m_highRequests.append(std::move(m_backgroundRequests));
    ++m_model._metadataSubmittedBatches;
    m_model._decodeManager->readVersionedImagesInfo(
        m_highRequests, false, false, m_model._sessionId);
}

void ExternalCatalogMetadataPlanner::run() {
    if (!prepareCapacity() || serviceProbeBarrier()) {
        return;
    }
    drainRows(m_model._metadataUrgentRows, m_highRequests);
    drainRows(m_model._metadataVisibleRows, m_highRequests);
    drainRows(m_model._metadataAdHocRows, m_backgroundRequests);
    drainRows(m_model._metadataOverscanRows, m_backgroundRequests);
    collectCatalogRows();
    submitRequests();
}

} // namespace ZoinGallery
