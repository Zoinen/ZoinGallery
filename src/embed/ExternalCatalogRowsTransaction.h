#pragma once

#include "ExternalCatalogModel.h"

namespace ZoinGallery {

class ExternalCatalogRowsTransaction final {
public:
    ExternalCatalogRowsTransaction(ExternalCatalogModel &model,
                                   const QVariantList &values,
                                   bool metadataDeferred);

    bool run();

private:
    struct PendingRow {
        int row = -1;
        ExternalCatalogModel::Entry entry;
    };

    bool validate();
    ExternalCatalogModel::Entry *prepareTarget(PendingRow &update);
    void removeOldIndexes(ExternalCatalogModel::Entry &entry);
    void addIndexes(const ExternalCatalogModel::Entry &entry, int row);
    void applyRows();
    void resumePipelines();

    ExternalCatalogModel &m_model;
    const QVariantList &m_values;
    bool m_metadataDeferred = false;
    QList<PendingRow> m_pending;
    int m_firstChanged = -1;
    int m_lastChanged = -1;
};

class ExternalCatalogAppendTransaction final {
public:
    ExternalCatalogAppendTransaction(ExternalCatalogModel &model,
                                     const QVariantList &values,
                                     bool metadataDeferred);

    bool run();

private:
    bool validate();
    void indexEntry(const ExternalCatalogModel::Entry &entry, int row);
    void commit();

    ExternalCatalogModel &m_model;
    const QVariantList &m_values;
    bool m_metadataDeferred = false;
    int m_firstRow = 0;
    QList<ExternalCatalogModel::Entry> m_entries;
};

} // namespace ZoinGallery
