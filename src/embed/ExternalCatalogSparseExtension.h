#pragma once

#include "ExternalCatalogModel.h"

namespace ZoinGallery {

// Extends an already-published sparse catalog without resetting delegates.
// Validation is completed before the model transaction starts, so a rejected
// payload cannot leave the logical row count or lookup indexes half-updated.
class ExternalCatalogSparseExtension final {
public:
    ExternalCatalogSparseExtension(ExternalCatalogModel &model,
                                   const QVariantList &values,
                                   bool metadataDeferred,
                                   int totalCount);

    bool run();

private:
    using Entry = ExternalCatalogModel::Entry;

    bool validateEnvelope() const;
    bool parseValues();
    bool parseValue(const QVariant &value);
    bool currentCatalogRepresented() const;
    static bool sameBaseEntry(const Entry &current,
                              const Entry &incoming);

    void commit();
    void indexEntry(Entry &&entry);
    void emitMaterializedRows();

    ExternalCatalogModel &m_model;
    const QVariantList &m_values;
    bool m_metadataDeferred = false;
    int m_totalCount = 0;
    int m_previousCount = 0;
    QList<Entry> m_appended;
    QList<int> m_newlyMaterializedOldRows;
    QSet<int> m_seenRows;
    QSet<QString> m_seenIds;
};

} // namespace ZoinGallery
