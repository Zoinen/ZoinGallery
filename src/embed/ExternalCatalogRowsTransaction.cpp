#include "ExternalCatalogRowsTransaction.h"

#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

ExternalCatalogRowsTransaction::ExternalCatalogRowsTransaction(
    ExternalCatalogModel &model, const QVariantList &values,
    bool metadataDeferred)
    : m_model(model),
      m_values(values),
      m_metadataDeferred(metadataDeferred) {
}

bool ExternalCatalogRowsTransaction::validate() {
    if (m_model._shutdown || m_values.isEmpty()) {
        return false;
    }
    m_pending.reserve(m_values.size());
    QSet<QString> ids;
    QSet<int> rows;
    for (const QVariant &value : m_values) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            return false;
        }
        const QVariantMap map = value.toMap();
        bool indexOK = false;
        const int row = map.value(QStringLiteral("index")).toInt(&indexOK);
        const QString id = map.value(QStringLiteral("entryId")).toString();
        ExternalCatalogModel::Entry parsed;
        const ExternalCatalogModel::Entry *existing = m_model.entryAt(row);
        if (!indexOK || row < 0 || row >= m_model.logicalRowCount()
            || id.isEmpty() || rows.contains(row) || ids.contains(id)
            || (existing && existing->loaded && existing->id != id)
            || (m_model._idToRow.contains(id)
                && m_model._idToRow.value(id) != row)
            || !m_model.parseCatalogEntry(
                map, row, m_metadataDeferred, &parsed)) {
            return false;
        }
        rows.insert(row);
        ids.insert(id);
        if (existing && existing->loaded && sameInputs(*existing, parsed)) {
            continue;
        }
        m_pending.append({row, std::move(parsed)});
    }
    return true;
}

bool ExternalCatalogRowsTransaction::sameInputs(
    const ExternalCatalogModel::Entry &previous,
    const ExternalCatalogModel::Entry &incoming) const {
    // Compare authoritative inputs, not derived decoder/preview state.
    // Replaying one immutable row must retain its facade, pixels, vector
    // lease, decoded dimensions and definitive failure state.
    if (!previous.videoThumbnailFailed && incoming.imageInfo.isCached) {
        const auto &oldInfo = previous.imageInfo;
        const auto &newInfo = incoming.imageInfo;
        const bool geometryProgress = incoming.metadataSettled
            && (!previous.metadataSettled
                || previous.originalSize != incoming.originalSize
                || oldInfo.imageSize != newInfo.imageSize
                || oldInfo.orientation != newInfo.orientation
                || (!newInfo.exif.isEmpty() && oldInfo.exif != newInfo.exif));
        const bool fieldProgress = newInfo.fileFieldsRead
            && (!oldInfo.fileFieldsRead || oldInfo.typedFileFields != newInfo.typedFileFields);
        if (geometryProgress || fieldProgress) {
            return false;
        }
    }
    const auto &oldSource = previous.source;
    const auto &newSource = incoming.source;
    return previous.id == incoming.id
        && previous.sourceIndex == incoming.sourceIndex
        && previous.name == incoming.name
        && previous.localPath == incoming.localPath
        && previous.directory == incoming.directory
        && previous.image == incoming.image
        && previous.thumbnailKind == incoming.thumbnailKind
        && previous.selected == incoming.selected
        && previous.size == incoming.size
        && previous.mtimeNs == incoming.mtimeNs
        && previous.metadataDeferred == incoming.metadataDeferred
        && previous.contentVersion == incoming.contentVersion
        && previous.sourceIdentity == incoming.sourceIdentity
        && previous.directorySource == incoming.directorySource
        && oldSource.resourceId == newSource.resourceId
        && oldSource.sourceKey == newSource.sourceKey
        && oldSource.contentVersion == newSource.contentVersion
        && oldSource.versionStrength == newSource.versionStrength
        && oldSource.storageClass == newSource.storageClass
        && oldSource.accessProfile == newSource.accessProfile
        && oldSource.mimeType == newSource.mimeType
        && oldSource.displayName == newSource.displayName
        && oldSource.size == newSource.size
        && oldSource.catalogGeneration == newSource.catalogGeneration
        && previous.displayFields == incoming.displayFields
        && previous.highlightStyle == incoming.highlightStyle;
}

void ExternalCatalogRowsTransaction::removeOldIndexes(
    ExternalCatalogModel::Entry &entry) {
    m_model._idToRow.remove(entry.id);
    if (!entry.localPath.isEmpty()) {
        m_model._pathToRow.remove(QDir::cleanPath(entry.localPath));
    }
    if (!entry.sourceIdentity.isEmpty()) {
        m_model._sourceToRow.remove(entry.sourceIdentity);
        m_model._sourceEntryIds.remove(entry.sourceIdentity, entry.id);
    }
}

ExternalCatalogModel::Entry *ExternalCatalogRowsTransaction::prepareTarget(
    PendingRow &update) {
    ExternalCatalogModel::Entry *target = m_model.entryAt(update.row);
    if (!target || !target->loaded) {
        m_model._sparseRowToOffset.insert(
            update.row, m_model._entries.size());
        m_model._entries.push_back(ExternalCatalogModel::Entry{});
        return &m_model._entries.last();
    }
    m_model.clearPublishedImage(*target);
    // dataChanged() is synchronous. Keep the old facade alive until every
    // materialized delegate has rebound to the replacement row.
    ImageFile *previousItem = target->item;
    target->item = nullptr;
    m_model.retireItemAfterReset(previousItem);
    removeOldIndexes(*target);
    return target;
}

void ExternalCatalogRowsTransaction::addIndexes(
    const ExternalCatalogModel::Entry &entry, int row) {
    m_model._idToRow.insert(entry.id, row);
    if (!entry.localPath.isEmpty()) {
        m_model._pathToRow.insert(QDir::cleanPath(entry.localPath), row);
    }
    if (!entry.sourceIdentity.isEmpty()) {
        m_model._sourceToRow.insert(entry.sourceIdentity, row);
        m_model._sourceEntryIds.insert(entry.sourceIdentity, entry.id);
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        m_model._providerEntryIds.insert(
            entry.thumbnailProviderId, entry.id);
    }
}

bool ExternalCatalogRowsTransaction::collectChangedRoles(
    const ExternalCatalogModel::Entry *previous,
    const ExternalCatalogModel::Entry &incoming, QList<int> &roles) const {
    if (!previous || !previous->loaded) {
        // Installing a previously absent sparse row publishes every role.
        return true;
    }
    const auto changed = [&roles](int role, bool differs) {
        if (differs && !roles.contains(role)) {
            roles.append(role);
        }
    };
    changed(ExternalCatalogModel::EntryNameRole, previous->name != incoming.name);
    changed(ExternalCatalogModel::SourceIndexRole,
            previous->sourceIndex != incoming.sourceIndex);
    changed(ExternalCatalogModel::LocalPathRole, previous->localPath != incoming.localPath);
    changed(ExternalCatalogModel::VersionTokenRole,
            previous->contentVersion != incoming.contentVersion);
    changed(FileListModel::FolderRole, previous->directory != incoming.directory);
    changed(FileListModel::IsImageRole,
            previous->thumbnailPresentation() != incoming.thumbnailPresentation());
    changed(FileListModel::SelectedRole, previous->selected != incoming.selected);
    changed(FileListModel::LastModifiedRole, previous->mtimeNs != incoming.mtimeNs);
    changed(FileListModel::FileSizeRole, previous->size != incoming.size);
    const QSize previousSize = previous->videoThumbnailFailed ? QSize{} : previous->originalSize;
    const QSize incomingSize = incoming.videoThumbnailFailed ? QSize{} : incoming.originalSize;
    // Settling an unknown-size image releases the complete-row geometry
    // barrier even when its final size remains empty (file-icon fallback).
    const bool geometryChanged = previousSize != incomingSize
        || previous->metadataSettled != incoming.metadataSettled;
    changed(FileListModel::ImageFullSizeRole, geometryChanged);
    changed(ExternalCatalogModel::KnownImageSizeRole, geometryChanged);
    changed(ExternalCatalogModel::MetadataSettledRole,
            previous->metadataSettled != incoming.metadataSettled);
    return false;
}

void ExternalCatalogRowsTransaction::applyRows() {
    m_firstChanged = m_model.logicalRowCount();
    m_lastChanged = -1;
    // Row replacement changes facade-backed roles even when their current
    // values are empty. Keep their notifications and decode replanning, but
    // do not invalidate geometry, names, or caption metrics without a change.
    QList<int> changedRoles = {FileListModel::ImageFileRole,
                      ExternalCatalogModel::VisualSnapshotRole,
                      FileListModel::ImageIdUrlRole,
                      FileListModel::SelectionGroupIdRole,
                      FileListModel::SelectionGroupColorRole,
                      FileListModel::FolderViewRole};
    bool allRolesChanged = false;
    for (PendingRow &update : m_pending) {
        allRolesChanged |= collectChangedRoles(
            m_model.entryAt(update.row), update.entry, changedRoles);
        ExternalCatalogModel::Entry *target = prepareTarget(update);
        *target = std::move(update.entry);
        addIndexes(*target, update.row);
        m_firstChanged = qMin(m_firstChanged, update.row);
        m_lastChanged = qMax(m_lastChanged, update.row);
    }
    if (m_lastChanged >= m_firstChanged) {
        emit m_model.dataChanged(
            m_model.index(m_firstChanged), m_model.index(m_lastChanged),
            allRolesChanged ? QList<int>{} : changedRoles);
    }
}

void ExternalCatalogRowsTransaction::resumePipelines() {
    if (m_model._catalogProbeRequested) {
        m_model._probePassComplete = false;
        m_model.scheduleProbePump();
    }
    if (m_model._catalogMetadataRequested) {
        m_model.scheduleMetadataPump();
    }
}

bool ExternalCatalogRowsTransaction::run() {
    if (!validate()) {
        return false;
    }
    applyRows();
    resumePipelines();
    return true;
}

ExternalCatalogAppendTransaction::ExternalCatalogAppendTransaction(
    ExternalCatalogModel &model, const QVariantList &values,
    bool metadataDeferred)
    : m_model(model),
      m_values(values),
      m_metadataDeferred(metadataDeferred),
      m_firstRow(model._entries.size()) {
}

bool ExternalCatalogAppendTransaction::validate() {
    m_entries.reserve(m_values.size());
    QSet<QString> newIds;
    newIds.reserve(m_values.size());
    for (int offset = 0; offset < m_values.size(); ++offset) {
        const QVariant &value = m_values.at(offset);
        if (value.metaType().id() != QMetaType::QVariantMap) {
            return false;
        }
        const QVariantMap map = value.toMap();
        const int row = m_firstRow + offset;
        const QString id = map.value(QStringLiteral("entryId")).toString();
        ExternalCatalogModel::Entry entry;
        if (id.isEmpty() || m_model._idToRow.contains(id)
            || newIds.contains(id)
            || !m_model.parseCatalogEntry(
                map, row, m_metadataDeferred, &entry)) {
            return false;
        }
        newIds.insert(id);
        m_entries.append(std::move(entry));
    }
    return true;
}

void ExternalCatalogAppendTransaction::indexEntry(
    const ExternalCatalogModel::Entry &entry, int row) {
    m_model._idToRow.insert(entry.id, row);
    if (!entry.localPath.isEmpty()) {
        m_model._pathToRow.insert(QDir::cleanPath(entry.localPath), row);
    }
    if (!entry.sourceIdentity.isEmpty()) {
        m_model._sourceToRow.insert(entry.sourceIdentity, row);
        m_model._sourceEntryIds.insert(entry.sourceIdentity, entry.id);
    }
    if (!entry.thumbnailProviderId.isEmpty()) {
        m_model._providerEntryIds.insert(
            entry.thumbnailProviderId, entry.id);
    }
}

void ExternalCatalogAppendTransaction::commit() {
    const int lastRow = m_firstRow + m_entries.size() - 1;
    m_model.beginInsertRows(QModelIndex(), m_firstRow, lastRow);
    for (ExternalCatalogModel::Entry &entry : m_entries) {
        const int row = m_model._entries.size();
        indexEntry(entry, row);
        m_model._entries.append(std::move(entry));
    }
    m_model.endInsertRows();
    if (m_model._cursorRow < 0 && !m_model._entries.isEmpty()) {
        m_model._cursorRow = 0;
    }
}

bool ExternalCatalogAppendTransaction::run() {
    if (m_model._shutdown || m_model._virtualRowCount >= 0
        || m_values.isEmpty() || !validate()) {
        return false;
    }
    commit();
    return true;
}

} // namespace ZoinGallery
