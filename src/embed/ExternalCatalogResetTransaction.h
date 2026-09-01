#pragma once

#include "ExternalCatalogModel.h"

#include <QElapsedTimer>

namespace ZoinGallery {

// Performs a full catalog replacement as one model-reset transaction.  The
// individual stages deliberately keep row construction, source retention,
// request pruning, planner reset, and viewer restoration independent.
class ExternalCatalogResetTransaction final {
public:
    struct Result {
        bool traceEnabled = false;
        qint64 rowsCompletedNs = 0;
        qint64 leftoversCompletedNs = 0;
        qint64 resetStartedNs = 0;
        qint64 completedNs = 0;
        int outputEntries = 0;
        int retainedSources = 0;
        int invalidatedSources = 0;
    };

    ExternalCatalogResetTransaction(ExternalCatalogModel &model,
                                    const QVariantList &values,
                                    bool metadataDeferred);

    Result run();

private:
    using Entry = ExternalCatalogModel::Entry;

    void captureStableState();
    void prepareNextCatalog();
    void capturePreviousCatalog();
    void rebuildRows();
    void initializeRow(Entry &entry, const QVariantMap &map, int row);
    bool adoptPreviousState(Entry &entry, Entry &old);
    void updateMaterializedItem(Entry &entry, const QVariantMap &map,
                                int row, bool sourceChanged);
    void indexRow(const Entry &entry, int row);

    void resolveSourceRetention();
    void retireRemovedEntries();
    void commitCatalogAndIndexes();
    bool retainedVersion(const QString &sourceIdentity,
                         const QString &version) const;
    void pruneVersionedPipelineState();
    void buildRetentionKeys();
    bool requestRetained(const QString &key) const;
    bool decodeRetryRetained(const QString &key) const;
    void pruneViewerRequestState();
    void pruneFitAndRetryState();
    void pruneDecodeRetryState();
    void resetPipelinePlanners();
    void normalizeCursorAndDeferredNative();
    void finishModelReset();
    void resumeRequestedPipelines();
    void restoreViewer();

    ExternalCatalogModel &m_model;
    const QVariantList &m_values;
    bool m_metadataDeferred = false;
    Result m_result;
    QElapsedTimer m_timer;

    QString m_activeViewerEntryId;
    QSize m_activeViewerViewportSize;
    bool m_activeViewerWasImage = false;
    bool m_catalogProbeWasRequested = false;
    bool m_catalogMetadataWasRequested = false;
    bool m_catalogFitWasStarted = false;
    bool m_resumeCatalogFit = false;

    QSet<QString> m_oldSourceWorkKeys;
    QSet<QString> m_oldSourceIdentities;
    QSet<QString> m_seenIds;
    QSet<QString> m_nextViewerSources;
    QList<Entry> m_next;
    QHash<QString, Entry> m_previous;
    QHash<QString, int> m_nextIdToRow;
    QHash<QString, int> m_nextPathToRow;
    QMultiHash<QString, QString> m_nextSourceEntryIds;
    QMultiHash<QString, QString> m_nextProviderEntryIds;

    QSet<QString> m_retainedSourceIdentities;
    QSet<QString> m_invalidatedSourceIdentities;
    QList<ImageFile *> m_retiredAfterReset;
    QSet<QString> m_retainedProbeKeys;
    QSet<QString> m_retainedRetryKeys;
    QStringList m_retainedRequestPrefixes;
    QSet<QString> m_retainedEntryIds;
};

} // namespace ZoinGallery
