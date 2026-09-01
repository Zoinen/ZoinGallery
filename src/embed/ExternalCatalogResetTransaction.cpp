#include "ExternalCatalogResetTransaction.h"

#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

ExternalCatalogResetTransaction::ExternalCatalogResetTransaction(
    ExternalCatalogModel &model, const QVariantList &values,
    bool metadataDeferred)
    : m_model(model), m_values(values),
      m_metadataDeferred(metadataDeferred) {
    m_result.traceEnabled = qEnvironmentVariableIsSet(
        "F4_NAV_BENCHMARK_TRACE");
}

ExternalCatalogResetTransaction::Result
ExternalCatalogResetTransaction::run() {
    if (m_result.traceEnabled) {
        m_timer.start();
    }
    captureStableState();
    prepareNextCatalog();

    m_model.beginResetModel();
    capturePreviousCatalog();
    rebuildRows();
    if (m_result.traceEnabled) {
        m_result.rowsCompletedNs = m_timer.nsecsElapsed();
    }

    resolveSourceRetention();
    retireRemovedEntries();
    if (m_result.traceEnabled) {
        m_result.leftoversCompletedNs = m_timer.nsecsElapsed();
    }

    commitCatalogAndIndexes();
    pruneVersionedPipelineState();
    buildRetentionKeys();
    pruneViewerRequestState();
    pruneFitAndRetryState();
    pruneDecodeRetryState();
    resetPipelinePlanners();
    normalizeCursorAndDeferredNative();
    finishModelReset();
    resumeRequestedPipelines();
    restoreViewer();

    m_result.outputEntries = m_model._entries.size();
    m_result.retainedSources = m_retainedSourceIdentities.size();
    m_result.invalidatedSources = m_invalidatedSourceIdentities.size();
    return m_result;
}

void ExternalCatalogResetTransaction::captureStableState() {
    m_activeViewerEntryId = m_model._viewerEntryId;
    m_activeViewerViewportSize = m_model._viewerViewportSize;
    const int viewerRow = m_model.rowForEntryId(m_activeViewerEntryId);
    if (m_model.validRow(viewerRow)) {
        m_activeViewerWasImage = m_model.entryAt(viewerRow)->image;
    }
    m_model.invalidateNativeDwell();

    for (const Entry &entry : std::as_const(m_model._entries)) {
        if (!entry.image || entry.sourceIdentity.isEmpty()) {
            continue;
        }
        m_oldSourceWorkKeys.insert(sourceWorkKey(entry.source, entry.size));
        m_oldSourceIdentities.insert(entry.sourceIdentity);
    }
    m_catalogProbeWasRequested = m_model._catalogProbeRequested;
    m_catalogMetadataWasRequested = m_model._catalogMetadataRequested;
    m_catalogFitWasStarted = m_model._catalogFitStarted;
}

void ExternalCatalogResetTransaction::prepareNextCatalog() {
    m_next.resize(m_values.size());
    m_seenIds.reserve(m_values.size());
    m_nextViewerSources.reserve(m_values.size());
    m_nextIdToRow.reserve(m_values.size());
    m_nextPathToRow.reserve(m_values.size());
    m_nextSourceEntryIds.reserve(m_values.size());
    m_nextProviderEntryIds.reserve(m_values.size());
}

void ExternalCatalogResetTransaction::capturePreviousCatalog() {
    QList<Entry> oldEntries = std::move(m_model._entries);
    m_model._entries.clear();
    m_previous.reserve(oldEntries.size());
    for (Entry &entry : oldEntries) {
        m_previous.insert(entry.id, std::move(entry));
    }
}

void ExternalCatalogResetTransaction::rebuildRows() {
    for (int row = 0; row < m_values.size(); ++row) {
        const QVariantMap map = m_values.at(row).toMap();
        Entry &entry = m_next[row];
        initializeRow(entry, map, row);

        Entry old = m_previous.take(entry.id);
        const bool sourceChanged = adoptPreviousState(entry, old);
        updateMaterializedItem(entry, map, row, sourceChanged);
        indexRow(entry, row);
    }
}

void ExternalCatalogResetTransaction::initializeRow(
    Entry &entry, const QVariantMap &map, int row) {
    entry.loaded = true;
    entry.sourceIndex = map.value(QStringLiteral("index"), row).toInt();
    entry.name = map.value(QStringLiteral("name")).toString();
    entry.localPath = map.value(
        QStringLiteral("localPath"), map.value(QStringLiteral("path")))
                              .toString();
    entry.id = map.value(QStringLiteral("entryId")).toString();
    if (entry.id.isEmpty()) {
        const QString fallbackSource =
            map.value(QStringLiteral("sourceKey")).toString();
        entry.id = !fallbackSource.isEmpty()
            ? fallbackSource
            : !entry.localPath.isEmpty()
                ? entry.localPath
                : QStringLiteral("row:%1:%2")
                      .arg(entry.sourceIndex)
                      .arg(entry.name);
    }
    if (m_seenIds.contains(entry.id)) {
        entry.id += QStringLiteral("#%1").arg(row);
    }
    m_seenIds.insert(entry.id);

    entry.directory = map.value(
        QStringLiteral("isDir"), map.value(QStringLiteral("directory")))
                              .toBool();
    entry.image = map.contains(QStringLiteral("isImage"))
        ? map.value(QStringLiteral("isImage")).toBool()
        : (!m_metadataDeferred && !entry.directory
           && FileListModel::isImage(entry.name));
    entry.selected = map.value(QStringLiteral("selected")).toBool();
    if (m_metadataDeferred) {
        entry.mtimeNs = 0;
        entry.size = -1;
    } else {
        entry.mtimeNs = integerValue(
            map, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
        entry.size = sourceSizeValue(map);
    }
    entry.source = sourceDescriptor(
        map, entry.name, entry.size, entry.mtimeNs);
    entry.contentVersion = entry.source.contentVersion;
    entry.sourceIdentity = entry.source.runtimeIdentity();
    entry.displayFields = catalogDisplayFields(map, m_metadataDeferred);
    entry.metadataDeferred = m_metadataDeferred;

    entry.imageInfo.path = entry.sourceIdentity;
    entry.imageInfo.source = entry.source;
    entry.imageInfo.requestNamespace = m_model._sessionId;
    entry.imageInfo.sourceVersionToken = entry.contentVersion;
    entry.imageInfo.lastModified = entry.mtimeNs != 0
        ? QDateTime::fromMSecsSinceEpoch(
              entry.mtimeNs / 1000000, QTimeZone::UTC)
        : QDateTime{};
    entry.imageInfo.fileSize = entry.size;
}

bool ExternalCatalogResetTransaction::adoptPreviousState(
    Entry &entry, Entry &old) {
    const bool hadOldEntry = !old.id.isEmpty();
    if (old.item) {
        old.imageInfo = old.item->info();
        if (old.item->fullSize().isValid()) {
            old.originalSize = old.item->fullSize();
        }
    }
    entry.item = old.item;
    entry.highlightStyle = old.highlightStyle;

    const bool sourceChanged = hadOldEntry &&
        (old.source.resourceId != entry.source.resourceId
         || old.source.sourceKey != entry.source.sourceKey
         || old.contentVersion != entry.contentVersion
         || old.source.versionStrength != entry.source.versionStrength
         || old.source.accessProfile != entry.source.accessProfile
         || old.source.storageClass != entry.source.storageClass
         || old.source.mimeType != entry.source.mimeType
         || old.localPath != entry.localPath
         || old.size != entry.size || old.image != entry.image);
    if (hadOldEntry && !sourceChanged) {
        entry.imageInfo = old.imageInfo;
        entry.originalSize = old.originalSize;
    }
    if (sourceChanged) {
        m_model._viewerImageCache.remove(old.sourceIdentity);
        m_model.clearPublishedImage(old);
        entry.originalSize = {};
        if (entry.item) {
            entry.item->setFullSize({});
        }
    } else if (old.item) {
        entry.thumbnailProviderId = old.thumbnailProviderId;
        entry.thumbnailRequestedSize = old.thumbnailRequestedSize;
        entry.thumbnailTransformKey = old.thumbnailTransformKey;
    }
    return sourceChanged;
}

void ExternalCatalogResetTransaction::updateMaterializedItem(
    Entry &entry, const QVariantMap &map, int row, bool sourceChanged) {
    QString folder;
    QString fileName = entry.name;
    if (!entry.localPath.isEmpty()) {
        const QFileInfo pathInfo(entry.localPath);
        folder = pathInfo.absolutePath();
        if (fileName.isEmpty()) {
            fileName = pathInfo.fileName();
        }
    }
    if (map.contains(QStringLiteral("highlightStyle"))) {
        entry.highlightStyle = map.value(
            QStringLiteral("highlightStyle")).toMap();
    }
    m_model.setEntryHighlightStyle(entry, entry.highlightStyle);

    ImageInfo info = sourceChanged ? ImageInfo{} : entry.imageInfo;
    info.path = entry.sourceIdentity;
    info.source = entry.source;
    info.requestNamespace = m_model._sessionId;
    info.sourceVersionToken = entry.contentVersion;
    info.lastModified = entry.mtimeNs != 0
        ? QDateTime::fromMSecsSinceEpoch(
              entry.mtimeNs / 1000000, QTimeZone::UTC)
        : QDateTime{};
    info.fileSize = entry.size;
    entry.imageInfo = info;
    if (!entry.item) {
        return;
    }

    entry.item->setFolderPath(folder);
    entry.item->setFileName(fileName);
    entry.item->setIndex(row);
    entry.item->setIsFolder(entry.directory);
    entry.item->setIsImage(entry.image);
    entry.item->setHighlightStyle(entry.highlightStyle);
    entry.item->setIconPath(entry.iconPath);
    entry.item->setIsSelected(entry.selected);
    entry.item->setImageProviderName(m_model._thumbnailProviderName);
    entry.item->setInfo(entry.imageInfo);
    entry.item->setFullSize(entry.originalSize);
    entry.item->setDisplayFields(entry.displayFields);
}

void ExternalCatalogResetTransaction::indexRow(
    const Entry &entry, int row) {
    if (entry.image && entry.source.isValid()) {
        m_nextViewerSources.insert(sourceRevisionKey(
            entry.sourceIdentity, entry.contentVersion, entry.size));
    }
    m_nextIdToRow.insert(entry.id, row);
    if (!entry.localPath.isEmpty()) {
        m_nextPathToRow.insert(QDir::cleanPath(entry.localPath), row);
    }
    if (!entry.sourceIdentity.isEmpty()) {
        m_nextSourceEntryIds.insert(entry.sourceIdentity, entry.id);
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        m_nextProviderEntryIds.insert(entry.thumbnailProviderId, entry.id);
    }
}

void ExternalCatalogResetTransaction::resolveSourceRetention() {
    for (const Entry &entry : std::as_const(m_next)) {
        if (entry.image && m_oldSourceWorkKeys.contains(
                sourceWorkKey(entry.source, entry.size))) {
            m_retainedSourceIdentities.insert(entry.sourceIdentity);
        }
    }
    m_invalidatedSourceIdentities = m_oldSourceIdentities;
    m_invalidatedSourceIdentities.subtract(m_retainedSourceIdentities);
    m_model._decodeManager->cancelSourceRequests(
        m_model._sessionId, m_invalidatedSourceIdentities);
    if (m_model._thumbnailCache) {
        m_model._thumbnailCache->cancelRequests(
            m_model._sessionId, m_invalidatedSourceIdentities);
    }
}

void ExternalCatalogResetTransaction::retireRemovedEntries() {
    m_retiredAfterReset.reserve(m_previous.size());
    for (Entry &entry : m_previous) {
        const QString viewerSource = sourceRevisionKey(
            entry.sourceIdentity, entry.contentVersion, entry.size);
        const bool sourceStillPresent = entry.image
            && m_nextViewerSources.contains(viewerSource);
        if (!sourceStillPresent) {
            m_model._viewerImageCache.remove(entry.sourceIdentity);
        }
        m_model.clearPublishedImage(entry);
        if (entry.item) {
            m_retiredAfterReset.append(entry.item);
            entry.item = nullptr;
        }
    }
}

void ExternalCatalogResetTransaction::commitCatalogAndIndexes() {
    m_model._entries = std::move(m_next);
    m_model._virtualRowCount = -1;
    m_model._sparseRowToOffset.clear();
    m_model._idToRow = std::move(m_nextIdToRow);
    m_model._pathToRow = std::move(m_nextPathToRow);
    m_model._sourceEntryIds = std::move(m_nextSourceEntryIds);
    m_model._providerEntryIds = std::move(m_nextProviderEntryIds);
    m_model._sourceToRow.clear();
    for (int row = 0; row < m_model._entries.size(); ++row) {
        const Entry &entry = m_model.loadedEntry(row);
        if (!entry.sourceIdentity.isEmpty()) {
            m_model._sourceToRow.insert(entry.sourceIdentity, row);
        }
    }
}

bool ExternalCatalogResetTransaction::retainedVersion(
    const QString &sourceIdentity, const QString &version) const {
    if (!m_retainedSourceIdentities.contains(sourceIdentity)) {
        return false;
    }
    const int row = m_model._sourceToRow.value(sourceIdentity, -1);
    return m_model.validRow(row)
        && m_model.loadedEntry(row).contentVersion == version;
}

void ExternalCatalogResetTransaction::pruneVersionedPipelineState() {
    for (auto it = m_model._metadataPendingVersions.begin();
         it != m_model._metadataPendingVersions.end();) {
        it = retainedVersion(it.key(), it.value())
            ? std::next(it) : m_model._metadataPendingVersions.erase(it);
    }
    for (auto it = m_model._metadataResolvedPaths.begin();
         it != m_model._metadataResolvedPaths.end();) {
        it = m_retainedSourceIdentities.contains(*it)
            ? std::next(it) : m_model._metadataResolvedPaths.erase(it);
    }
    for (auto it = m_model._probePendingVersions.begin();
         it != m_model._probePendingVersions.end();) {
        it = retainedVersion(it.key(), it.value())
            ? std::next(it) : m_model._probePendingVersions.erase(it);
    }
    for (auto it = m_model._probeResolvedVersions.begin();
         it != m_model._probeResolvedVersions.end();) {
        it = retainedVersion(it.key(), it.value())
            ? std::next(it) : m_model._probeResolvedVersions.erase(it);
    }
}

void ExternalCatalogResetTransaction::buildRetentionKeys() {
    for (const Entry &entry : std::as_const(m_model._entries)) {
        m_retainedEntryIds.insert(entry.id);
        if (!m_retainedSourceIdentities.contains(entry.sourceIdentity)) {
            continue;
        }
        m_retainedProbeKeys.insert(entry.source.cacheKey());
        m_retainedRetryKeys.insert(
            entry.sourceIdentity + QChar(0x1f) + entry.contentVersion);
        m_retainedRequestPrefixes.append(sourceRevisionKey(
            entry.sourceIdentity, entry.contentVersion, entry.size)
            + QChar(0x1f));
    }
}

bool ExternalCatalogResetTransaction::requestRetained(
    const QString &key) const {
    return std::any_of(
        m_retainedRequestPrefixes.cbegin(), m_retainedRequestPrefixes.cend(),
        [&key](const QString &prefix) { return key.startsWith(prefix); });
}

bool ExternalCatalogResetTransaction::decodeRetryRetained(
    const QString &key) const {
    const qsizetype separator = key.indexOf(QLatin1Char(':'));
    return separator >= 0 && requestRetained(key.mid(separator + 1));
}

void ExternalCatalogResetTransaction::pruneViewerRequestState() {
    for (auto it = m_model._pendingViewerRequests.begin();
         it != m_model._pendingViewerRequests.end();) {
        it = requestRetained(*it)
            ? std::next(it) : m_model._pendingViewerRequests.erase(it);
    }
    for (auto it = m_model._pendingThumbnailRequests.begin();
         it != m_model._pendingThumbnailRequests.end();) {
        it = requestRetained(it.key())
            ? std::next(it) : m_model._pendingThumbnailRequests.erase(it);
    }
    for (auto it = m_model._viewerPlans.begin();
         it != m_model._viewerPlans.end();) {
        it = m_retainedEntryIds.contains(it.key())
            ? std::next(it) : m_model._viewerPlans.erase(it);
    }
}

void ExternalCatalogResetTransaction::pruneFitAndRetryState() {
    for (auto it = m_model._catalogFitPendingKeys.begin();
         it != m_model._catalogFitPendingKeys.end();) {
        it = requestRetained(*it)
            ? std::next(it) : m_model._catalogFitPendingKeys.erase(it);
    }
    for (auto it = m_model._catalogFitResolvedSources.begin();
         it != m_model._catalogFitResolvedSources.end();) {
        it = m_retainedSourceIdentities.contains(*it)
            ? std::next(it) : m_model._catalogFitResolvedSources.erase(it);
    }
    for (auto it = m_model._catalogFitWaitingMetadata.begin();
         it != m_model._catalogFitWaitingMetadata.end();) {
        it = m_retainedSourceIdentities.contains(*it)
            ? std::next(it) : m_model._catalogFitWaitingMetadata.erase(it);
    }
    for (auto it = m_model._probeRetryableSources.begin();
         it != m_model._probeRetryableSources.end();) {
        it = m_retainedProbeKeys.contains(*it)
            ? std::next(it) : m_model._probeRetryableSources.erase(it);
    }
    for (auto it = m_model._probeRetryAttempts.begin();
         it != m_model._probeRetryAttempts.end();) {
        it = m_retainedProbeKeys.contains(it.key())
            ? std::next(it) : m_model._probeRetryAttempts.erase(it);
    }
    for (auto it = m_model._probeRetryNotBeforeMs.begin();
         it != m_model._probeRetryNotBeforeMs.end();) {
        it = m_retainedProbeKeys.contains(it.key())
            ? std::next(it) : m_model._probeRetryNotBeforeMs.erase(it);
    }
    for (auto it = m_model._metadataRetryAttempts.begin();
         it != m_model._metadataRetryAttempts.end();) {
        it = m_retainedRetryKeys.contains(it.key())
            ? std::next(it) : m_model._metadataRetryAttempts.erase(it);
    }
    for (auto it = m_model._metadataRetryScheduled.begin();
         it != m_model._metadataRetryScheduled.end();) {
        it = m_retainedRetryKeys.contains(*it)
            ? std::next(it) : m_model._metadataRetryScheduled.erase(it);
    }
}

void ExternalCatalogResetTransaction::pruneDecodeRetryState() {
    for (auto it = m_model._sourceDecodeRetryAttempts.begin();
         it != m_model._sourceDecodeRetryAttempts.end();) {
        it = decodeRetryRetained(it.key())
            ? std::next(it) : m_model._sourceDecodeRetryAttempts.erase(it);
    }
    for (auto it = m_model._sourceDecodeRetryScheduled.begin();
         it != m_model._sourceDecodeRetryScheduled.end();) {
        it = decodeRetryRetained(*it)
            ? std::next(it) : m_model._sourceDecodeRetryScheduled.erase(it);
    }
}

void ExternalCatalogResetTransaction::resetPipelinePlanners() {
    m_model._metadataVisibleRows.clear();
    m_model._metadataLastVisibleRows.clear();
    m_model._metadataOverscanRows.clear();
    m_model._metadataUrgentRows.clear();
    m_model._metadataAdHocRows.clear();
    m_model._catalogMetadataCursor = 0;
    m_model._metadataPumpScheduled = false;
    m_model._catalogMetadataRequested = m_catalogMetadataWasRequested;

    m_model._probeVisibleRows.clear();
    m_model._probeOverscanRows.clear();
    m_model._probeUrgentRows.clear();
    m_model._catalogProbeCursor = 0;
    m_model._probePumpScheduled = false;
    m_model._catalogProbeRequested = m_catalogProbeWasRequested;
    m_model._probePassComplete = m_catalogProbeWasRequested
        && m_model._probePendingVersions.isEmpty()
        && m_model.probeBarrierReached();

    m_model._catalogFitRows.clear();
    m_model._catalogFitPumpScheduled = false;
    m_model._catalogFitStarted = false;
    m_resumeCatalogFit = m_catalogFitWasStarted
        || m_model._probePassComplete;
}

void ExternalCatalogResetTransaction::normalizeCursorAndDeferredNative() {
    if (!m_retainedEntryIds.contains(m_model._deferredNativeEntryId)) {
        m_model._deferredNativeEntryId.clear();
        m_model.invalidateNativeDwell();
    }
    if (m_model.logicalRowCount() == 0) {
        m_model._cursorRow = -1;
    } else {
        m_model._cursorRow = qBound(
            0, m_model._cursorRow, m_model.logicalRowCount() - 1);
    }
}

void ExternalCatalogResetTransaction::finishModelReset() {
    if (m_result.traceEnabled) {
        m_result.resetStartedNs = m_timer.nsecsElapsed();
    }
    m_model.endResetModel();
    for (ImageFile *item : std::as_const(m_retiredAfterReset)) {
        m_model.retireItemAfterReset(item);
    }
    if (m_result.traceEnabled) {
        m_result.completedNs = m_timer.nsecsElapsed();
    }
}

void ExternalCatalogResetTransaction::resumeRequestedPipelines() {
    if (m_model._catalogProbeRequested && !m_model._probePassComplete) {
        m_model.scheduleProbePump();
    }
    if (m_model._catalogMetadataRequested) {
        m_model.scheduleMetadataPump();
    }
    if (m_resumeCatalogFit
        && (!m_model._catalogProbeRequested || m_model._probePassComplete)) {
        m_model.beginCatalogFitPass();
    }
}

void ExternalCatalogResetTransaction::restoreViewer() {
    const int refreshedRow = m_model.rowForEntryId(m_activeViewerEntryId);
    const bool stillAvailable = m_activeViewerWasImage
        && m_model.validRow(refreshedRow)
        && m_model.loadedEntry(refreshedRow).image;
    if (!stillAvailable) {
        m_model.clearViewer();
        return;
    }
    m_model._viewerEntryId = m_activeViewerEntryId;
    m_model._viewerViewportSize = m_activeViewerViewportSize;
    m_model.notifyViewerImageUrlChanged();
    m_model.scheduleViewerDecode();
}

} // namespace ZoinGallery
