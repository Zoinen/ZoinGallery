#include "ExternalCatalogModelPrivate.h"
#include "ExternalCatalogResetTransaction.h"
#include "ExternalCatalogRowsTransaction.h"
#include "ExternalCatalogSparseExtension.h"

namespace ZoinGallery {

bool ExternalCatalogModel::tryExtendSparseCatalog(
    const QVariantList &values, bool metadataDeferred, int totalCount) {
    ExternalCatalogSparseExtension extension(
        *this, values, metadataDeferred, totalCount);
    return extension.run();
}

bool ExternalCatalogModel::applySparseCatalog(
    const QVariantList &values, bool metadataDeferred, int totalCount) {
    if (_shutdown || totalCount < 0 || values.size() > totalCount) {
        return false;
    }

    QList<Entry> next;
    next.reserve(values.size());
    QHash<int, int> nextRows;
    nextRows.reserve(values.size());
    QSet<QString> seenIds;
    seenIds.reserve(values.size());
    for (const QVariant &value : values) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            return false;
        }
        const QVariantMap map = value.toMap();
        bool indexOK = false;
        const int row = map.value(QStringLiteral("index")).toInt(&indexOK);
        const QString id = map.value(QStringLiteral("entryId")).toString();
        Entry parsed;
        if (!indexOK || row < 0 || row >= totalCount || id.isEmpty()
            || seenIds.contains(id) || nextRows.contains(row)
            || !parseCatalogEntry(map, row, metadataDeferred, &parsed)) {
            return false;
        }
        seenIds.insert(id);
        nextRows.insert(row, next.size());
        next.push_back(std::move(parsed));
    }

    cancelAllRunners();
    clearViewer();
    beginResetModel();
    for (Entry &old : _entries) {
        if (!old.loaded) {
            continue;
        }
        clearPublishedImage(old);
        if (old.item) {
            retireItemAfterReset(old.item);
            old.item = nullptr;
        }
    }
    _entries = std::move(next);
    _virtualRowCount = totalCount;
    _sparseRowToOffset = std::move(nextRows);
    _idToRow.clear();
    _pathToRow.clear();
    _sourceToRow.clear();
    _sourceEntryIds.clear();
    _providerEntryIds.clear();
    for (const Entry &entry : std::as_const(_entries)) {
        const int row = entry.sourceIndex;
        _idToRow.insert(entry.id, row);
        if (!entry.localPath.isEmpty()) {
            _pathToRow.insert(QDir::cleanPath(entry.localPath), row);
        }
        if (!entry.sourceIdentity.isEmpty()) {
            _sourceToRow.insert(entry.sourceIdentity, row);
            _sourceEntryIds.insert(entry.sourceIdentity, entry.id);
        }
        if (!entry.thumbnailProviderId.isEmpty()) {
            _providerEntryIds.insert(entry.thumbnailProviderId, entry.id);
        }
    }
    _cursorRow = totalCount > 0 ? qBound(0, _cursorRow, totalCount - 1) : -1;
    endResetModel();
    return true;
}

bool ExternalCatalogModel::applyCatalogRows(
    const QVariantList &values, bool metadataDeferred) {
    ExternalCatalogRowsTransaction transaction(
        *this, values, metadataDeferred);
    return transaction.run();
}

bool ExternalCatalogModel::applyCatalog(
    const QVariantList &values, bool metadataDeferred,
    bool checkEquivalentCatalog, int totalCount) {
    MediaTimingTrace::Span timingSpan(
        QStringLiteral("qt.gallery.model.catalog_apply"), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("inputEntries"), values.size()},
            {QStringLiteral("previousEntries"), _entries.size()},
        });
    if (_shutdown) {
        timingSpan.set(QStringLiteral("outcome"), QStringLiteral("shutdown"));
        return false;
    }
    if (totalCount >= 0 && totalCount != values.size()) {
        if (checkEquivalentCatalog && _virtualRowCount >= 0
            && tryExtendSparseCatalog(
                values, metadataDeferred, totalCount)) {
            timingSpan.set(QStringLiteral("outcome"),
                           QStringLiteral("sparse-extended"));
            timingSpan.set(QStringLiteral("outputEntries"), totalCount);
            return true;
        }
        const bool applied = applySparseCatalog(
            values, metadataDeferred, totalCount);
        timingSpan.set(QStringLiteral("outcome"),
                       applied ? QStringLiteral("sparse-reset")
                               : QStringLiteral("invalid-sparse"));
        timingSpan.set(QStringLiteral("outputEntries"), totalCount);
        return applied;
    }
    // A revision may advance for fields that the gallery does not consume.
    // Keep stable rows, QObjects, decode work, textures, and viewer state when
    // only the authoritative revision (or lightweight appearance) changed.
    bool hasAppearance = false;
    if (checkEquivalentCatalog
        && catalogMatches(values, &hasAppearance, metadataDeferred)) {
        const bool applied = !hasAppearance || applyAppearance(values);
        timingSpan.set(QStringLiteral("outcome"),
                       hasAppearance ? QStringLiteral("appearance-only")
                                     : QStringLiteral("unchanged"));
        timingSpan.set(QStringLiteral("applied"), applied);
        return applied;
    }

    ExternalCatalogResetTransaction transaction(
        *this, values, metadataDeferred);
    const ExternalCatalogResetTransaction::Result result = transaction.run();

    if (result.traceEnabled) {
        timingSpan.set(QStringLiteral("rowsNs"), result.rowsCompletedNs);
        timingSpan.set(QStringLiteral("leftoversNs"),
                       result.leftoversCompletedNs
                           - result.rowsCompletedNs);
        timingSpan.set(QStringLiteral("lookupCommitNs"),
                       result.resetStartedNs
                           - result.leftoversCompletedNs);
        timingSpan.set(QStringLiteral("endResetNs"),
                       result.completedNs - result.resetStartedNs);
        timingSpan.set(QStringLiteral("totalNs"), result.completedNs);
        qInfo().nospace()
            << "F4_NAV_BENCHMARK_TRACE catalog.model rowsNs="
            << result.rowsCompletedNs << " leftoversNs="
            << (result.leftoversCompletedNs - result.rowsCompletedNs)
            << " lookupCommitNs="
            << (result.resetStartedNs - result.leftoversCompletedNs)
            << " endResetNs="
            << (result.completedNs - result.resetStartedNs)
            << " totalNs=" << result.completedNs;
    }

    timingSpan.set(QStringLiteral("outcome"), QStringLiteral("reset"));
    timingSpan.set(QStringLiteral("outputEntries"), result.outputEntries);
    timingSpan.set(QStringLiteral("retainedSources"),
                   result.retainedSources);
    timingSpan.set(QStringLiteral("invalidatedSources"),
                   result.invalidatedSources);
    return true;
}

bool ExternalCatalogModel::appendCatalog(const QVariantList &values,
                                         bool metadataDeferred) {
    MediaTimingTrace::Span timingSpan(
        QStringLiteral("qt.gallery.model.catalog_append"), {
            {QStringLiteral("sessionId"), _sessionId},
            {QStringLiteral("inputEntries"), values.size()},
            {QStringLiteral("previousEntries"), _entries.size()},
        });
    if (_shutdown || _virtualRowCount >= 0 || values.isEmpty()) {
        timingSpan.set(QStringLiteral("outcome"),
                       _shutdown ? QStringLiteral("shutdown")
                       : _virtualRowCount >= 0 ? QStringLiteral("sparse")
                                               : QStringLiteral("empty"));
        return false;
    }
    ExternalCatalogAppendTransaction transaction(
        *this, values, metadataDeferred);
    if (!transaction.run()) {
        timingSpan.set(QStringLiteral("outcome"),
                       QStringLiteral("invalid-row"));
        return false;
    }
    timingSpan.set(QStringLiteral("outcome"), QStringLiteral("inserted"));
    timingSpan.set(QStringLiteral("outputEntries"), _entries.size());
    return true;
}

bool ExternalCatalogModel::applyAppearance(const QVariantList &values) {
    if (_shutdown) {
        return false;
    }
    QList<int> changedRows;
    changedRows.reserve(values.size());
    for (const QVariant &value : values) {
        const QVariantMap map = value.toMap();
        const int row = rowForEntryId(map.value(QStringLiteral("entryId")).toString());
        if (!validRow(row)) {
            continue;
        }
        Entry &entry = *entryAt(row);
        const QVariantMap style = map.value(
            QStringLiteral("highlightStyle")).toMap();
        if (!setEntryHighlightStyle(entry, style)) {
            continue;
        }
        changedRows.append(row);
    }

    // Host appearance snapshots normally cover the whole catalog. Emitting
    // one signal per row made every QML proxy/layout observer repeat its own
    // work thousands of times. Preserve precise ranges for sparse updates,
    // but collapse adjacent changed rows into one notification.
    std::sort(changedRows.begin(), changedRows.end());
    changedRows.erase(std::unique(changedRows.begin(), changedRows.end()),
                      changedRows.end());
    for (qsizetype offset = 0; offset < changedRows.size();) {
        const int first = changedRows.at(offset);
        int last = first;
        ++offset;
        while (offset < changedRows.size()
               && changedRows.at(offset) == last + 1) {
            last = changedRows.at(offset);
            ++offset;
        }
        emit dataChanged(index(first, 0), index(last, 0),
                         {FileListModel::ImageFileRole,
                          VisualSnapshotRole});
    }
    return true;
}

bool ExternalCatalogModel::applyState(
    const QString &cursorEntryId, int cursorIndex,
    const QStringList &selectedEntryIds, bool updateSelection) {
    if (_shutdown) {
        return false;
    }
    if (updateSelection) {
        const QSet<QString> selected(selectedEntryIds.begin(),
                                     selectedEntryIds.end());
        int firstChanged = -1;
        int lastChanged = -1;
        for (Entry &entry : _entries) {
            const int row = entry.sourceIndex;
            const bool shouldSelect = selected.contains(entry.id);
            if (entry.selected == shouldSelect) {
                continue;
            }
            entry.selected = shouldSelect;
            if (entry.item) {
                entry.item->setIsSelected(shouldSelect);
            }
            firstChanged = firstChanged < 0 ? row : firstChanged;
            lastChanged = row;
        }
        if (firstChanged >= 0) {
            emit dataChanged(index(firstChanged), index(lastChanged),
                             {FileListModel::SelectedRole});
        }
    }

    int newCursor = rowForEntryId(cursorEntryId);
    if (newCursor < 0 && validRow(cursorIndex)) {
        newCursor = cursorIndex;
    }
    if (newCursor < 0 && logicalRowCount() > 0) {
        newCursor = 0;
    }
    _cursorRow = newCursor;
    return true;
}

bool ExternalCatalogModel::applyStateDelta(
    const QString &cursorEntryId, int cursorIndex,
    const QVariantList &selectionChanges) {
    if (_shutdown) {
        return false;
    }

    QList<QPair<int, bool>> validatedChanges;
    validatedChanges.reserve(selectionChanges.size());
    QSet<int> changedRows;
    for (const QVariant &changeValue : selectionChanges) {
        if (changeValue.metaType().id() != QMetaType::QVariantMap) {
            return false;
        }
        const QVariantMap change = changeValue.toMap();
        if (change.size() != 3
            || !change.contains(QStringLiteral("index"))
            || !change.contains(QStringLiteral("entryId"))
            || !change.contains(QStringLiteral("selected"))) {
            return false;
        }
        bool indexOK = false;
        const int sourceIndex = change.value(QStringLiteral("index"))
                                    .toInt(&indexOK);
        const QVariant entryIdValue = change.value(
            QStringLiteral("entryId"));
        const QVariant selectedValue = change.value(
            QStringLiteral("selected"));
        const QString entryId = entryIdValue.toString();
        const int row = rowForEntryId(entryId);
        if (!indexOK || entryIdValue.metaType().id() != QMetaType::QString
            || entryId.isEmpty()
            || selectedValue.metaType().id() != QMetaType::Bool
            || !validRow(row) || entryAt(row)->sourceIndex != sourceIndex
            || changedRows.contains(row)) {
            return false;
        }
        changedRows.insert(row);
        validatedChanges.push_back({row, selectedValue.toBool()});
    }

    QList<int> updatedRows;
    updatedRows.reserve(validatedChanges.size());
    for (const auto &[row, selected] : validatedChanges) {
        Entry &entry = *entryAt(row);
        if (entry.selected == selected) {
            continue;
        }
        entry.selected = selected;
        if (entry.item) {
            entry.item->setIsSelected(selected);
        }
        updatedRows.push_back(row);
    }
    std::sort(updatedRows.begin(), updatedRows.end());
    for (qsizetype offset = 0; offset < updatedRows.size();) {
        const int first = updatedRows.at(offset);
        int last = first;
        ++offset;
        while (offset < updatedRows.size()
               && updatedRows.at(offset) == last + 1) {
            last = updatedRows.at(offset);
            ++offset;
        }
        emit dataChanged(index(first), index(last),
                         {FileListModel::SelectedRole});
    }

    int newCursor = rowForEntryId(cursorEntryId);
    if (newCursor < 0 && validRow(cursorIndex)) {
        newCursor = cursorIndex;
    }
    if (newCursor < 0 && logicalRowCount() > 0) {
        newCursor = 0;
    }
    _cursorRow = newCursor;
    return true;
}

QString ExternalCatalogModel::entryIdAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded ? entry->id : QString();
}

QString ExternalCatalogModel::entryNameAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded ? entry->name : QString();
}

QString ExternalCatalogModel::localPathAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded ? entry->localPath : QString();
}

bool ExternalCatalogModel::isImageAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded && entry->image;
}

bool ExternalCatalogModel::isDirectoryAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded && entry->directory;
}

int ExternalCatalogModel::sourceIndexAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded ? entry->sourceIndex : -1;
}

QSize ExternalCatalogModel::imageOriginalSizeAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded ? entry->originalSize : QSize();
}

QVariantMap ExternalCatalogModel::highlightStyleAt(int row) const {
    const Entry *entry = entryAt(row);
    return entry && entry->loaded ? entry->highlightStyle : QVariantMap();
}

int ExternalCatalogModel::rowForEntryId(const QString &entryId) const {
    return _idToRow.value(entryId, -1);
}

int ExternalCatalogModel::cursorRow() const {
    return _cursorRow;
}

void ExternalCatalogModel::ensurePreviews() {
    if (_shutdown) {
        return;
    }
    const bool hasExpensiveSource = std::any_of(
        _entries.cbegin(), _entries.cend(), [](const Entry &entry) {
            return entry.image && entry.source.isValid() &&
                expensiveSource(entry.source);
        });
    if (!hasExpensiveSource) {
        // Direct-local catalogs retain the existing viewport-driven path.
        // A catalog-wide embedded probe has no network latency to hide and
        // would only contend with visible metadata/thumbnail work.
        return;
    }
    // Probe admission remains bounded; setting the catalog flag does not
    // enqueue thousands of runners. Renderer-supplied visible/overscan rows
    // are drained ahead of this cursor by pumpProbeRequests().
    _catalogProbeRequested = true;
    scheduleProbePump();
}

void ExternalCatalogModel::resetExternalSource() {
    if (_shutdown) {
        return;
    }
    applyCatalog({});
    _cursorRow = -1;
}


} // namespace ZoinGallery
