#include "ExternalCatalogSparseExtension.h"

#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

ExternalCatalogSparseExtension::ExternalCatalogSparseExtension(
    ExternalCatalogModel &model, const QVariantList &values,
    bool metadataDeferred, int totalCount)
    : m_model(model),
      m_values(values),
      m_metadataDeferred(metadataDeferred),
      m_totalCount(totalCount),
      m_previousCount(model._virtualRowCount) {
}

bool ExternalCatalogSparseExtension::validateEnvelope() const {
    return !m_model._shutdown && m_previousCount >= 0
        && m_totalCount >= m_previousCount
        && m_values.size() <= m_totalCount;
}

bool ExternalCatalogSparseExtension::sameBaseEntry(
    const Entry &current, const Entry &incoming) {
    return current.loaded && incoming.loaded
        && current.id == incoming.id
        && current.sourceIndex == incoming.sourceIndex
        && current.name == incoming.name
        && current.localPath == incoming.localPath
        && current.directory == incoming.directory
        && current.image == incoming.image
        && current.source.resourceId == incoming.source.resourceId
        && current.source.sourceKey == incoming.source.sourceKey
        && (incoming.contentVersion.isEmpty()
            || current.contentVersion == incoming.contentVersion)
        && (incoming.source.versionStrength.isEmpty()
            || current.source.versionStrength
                == incoming.source.versionStrength)
        && current.source.storageClass == incoming.source.storageClass
        && current.source.accessProfile == incoming.source.accessProfile
        && current.source.mimeType == incoming.source.mimeType;
}

bool ExternalCatalogSparseExtension::parseValue(const QVariant &value) {
    if (value.metaType().id() != QMetaType::QVariantMap) {
        return false;
    }
    const QVariantMap map = value.toMap();
    bool indexOK = false;
    const int row = map.value(QStringLiteral("index")).toInt(&indexOK);
    const QString id = map.value(QStringLiteral("entryId")).toString();
    Entry parsed;
    if (!indexOK || row < 0 || row >= m_totalCount || id.isEmpty()
        || m_seenRows.contains(row) || m_seenIds.contains(id)
        || !m_model.parseCatalogEntry(
            map, row, m_metadataDeferred, &parsed)) {
        return false;
    }
    m_seenRows.insert(row);
    m_seenIds.insert(id);

    if (row < m_previousCount) {
        const Entry *current = m_model.entryAt(row);
        if (current && !sameBaseEntry(*current, parsed)) {
            return false;
        }
        if (current) {
            return true;
        }
        if (m_model._idToRow.contains(id)) {
            return false;
        }
        m_newlyMaterializedOldRows.append(row);
    } else if (m_model._idToRow.contains(id)) {
        return false;
    }
    m_appended.push_back(std::move(parsed));
    return true;
}

bool ExternalCatalogSparseExtension::parseValues() {
    m_appended.reserve(m_values.size());
    m_newlyMaterializedOldRows.reserve(m_values.size());
    m_seenRows.reserve(m_values.size());
    m_seenIds.reserve(m_values.size());
    for (const QVariant &value : m_values) {
        if (!parseValue(value)) {
            return false;
        }
    }
    return true;
}

bool ExternalCatalogSparseExtension::currentCatalogRepresented() const {
    // A reset payload is authoritative for its materialized window. Extending
    // is safe only when it still represents every previously loaded row.
    for (const Entry &current : std::as_const(m_model._entries)) {
        if (current.loaded
            && (!m_seenRows.contains(current.sourceIndex)
                || !m_seenIds.contains(current.id))) {
            return false;
        }
    }
    return true;
}

void ExternalCatalogSparseExtension::indexEntry(Entry &&entry) {
    const int row = entry.sourceIndex;
    const int offset = m_model._entries.size();
    m_model._entries.push_back(std::move(entry));
    const Entry &stored = m_model._entries.constLast();
    m_model._sparseRowToOffset.insert(row, offset);
    m_model._idToRow.insert(stored.id, row);
    if (!stored.localPath.isEmpty()) {
        m_model._pathToRow.insert(QDir::cleanPath(stored.localPath), row);
    }
    if (!stored.sourceIdentity.isEmpty()) {
        m_model._sourceToRow.insert(stored.sourceIdentity, row);
        m_model._sourceEntryIds.insert(stored.sourceIdentity, stored.id);
    }
    if (!stored.thumbnailProviderId.isEmpty()) {
        m_model._providerEntryIds.insert(
            stored.thumbnailProviderId, stored.id);
    }
}

void ExternalCatalogSparseExtension::emitMaterializedRows() {
    std::sort(m_newlyMaterializedOldRows.begin(),
              m_newlyMaterializedOldRows.end());
    for (qsizetype offset = 0;
         offset < m_newlyMaterializedOldRows.size();) {
        const int first = m_newlyMaterializedOldRows.at(offset);
        int last = first;
        ++offset;
        while (offset < m_newlyMaterializedOldRows.size()
               && m_newlyMaterializedOldRows.at(offset) == last + 1) {
            last = m_newlyMaterializedOldRows.at(offset++);
        }
        emit m_model.dataChanged(
            m_model.index(first, 0), m_model.index(last, 0));
    }
}

void ExternalCatalogSparseExtension::commit() {
    m_model._entries.reserve(
        m_model._entries.size() + m_appended.size());
    const bool grows = m_totalCount > m_previousCount;
    if (grows) {
        m_model.beginInsertRows(
            QModelIndex(), m_previousCount, m_totalCount - 1);
        m_model._virtualRowCount = m_totalCount;
    }
    for (Entry &entry : m_appended) {
        indexEntry(std::move(entry));
    }
    m_model._cursorRow = m_totalCount > 0
        ? qBound(0, m_model._cursorRow, m_totalCount - 1) : -1;
    if (grows) {
        m_model.endInsertRows();
    }
    emitMaterializedRows();
}

bool ExternalCatalogSparseExtension::run() {
    if (!validateEnvelope() || !parseValues()
        || !currentCatalogRepresented()) {
        return false;
    }
    commit();
    return true;
}

} // namespace ZoinGallery
