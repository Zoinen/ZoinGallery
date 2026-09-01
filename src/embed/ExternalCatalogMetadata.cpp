#include "ExternalCatalogModelPrivate.h"

namespace ZoinGallery {

class ExternalCatalogMetadataTransaction {
public:
    ExternalCatalogMetadataTransaction(
        ExternalCatalogModel &model, const QVariantList &values)
        : _model(model), _values(values) {
    }

    bool run();

private:
    struct RowUpdate {
        int row = -1;
        QVariantMap value;
    };

    struct RowState {
        bool image = false;
        QString localPath;
        QString sourceIdentity;
        ImageSourceDescriptor source;
        QString contentVersion;
        qint64 mtimeNs = 0;
        qint64 size = -1;
        QVariantMap displayFields;
        QVariantMap highlightStyle;
        QString iconPath;
    };

    bool validate();
    static RowState captureState(const ExternalCatalogModel::Entry &entry);
    static bool carriesSourceDescriptor(const QVariantMap &value);
    static bool sourceChanged(
        const ExternalCatalogModel::Entry &entry,
        const RowState &previous);
    void applyBaseFields(ExternalCatalogModel::Entry &entry,
                         const QVariantMap &value) const;
    void reconcileSource(ExternalCatalogModel::Entry &entry,
                         const QVariantMap &value,
                         const RowState &previous) const;
    void invalidateChangedSource(
        ExternalCatalogModel::Entry &entry, const RowState &previous);
    void updateLookupIndexes(int row, ExternalCatalogModel::Entry &entry,
                             const RowState &previous);
    void scheduleChangedSource(int row,
                               const ExternalCatalogModel::Entry &entry);
    void updateImageInfo(ExternalCatalogModel::Entry &entry,
                         const QVariantMap &value, bool changedSource);
    void updateAppearance(ExternalCatalogModel::Entry &entry,
                          const QVariantMap &value,
                          const RowState &previous) const;
    void applyRow(const RowUpdate &update);
    void emitChangedRows();

    ExternalCatalogModel &_model;
    const QVariantList &_values;
    QList<RowUpdate> _updates;
    QList<int> _changedRows;
    bool _viewerSourceInvalidated = false;
};

bool ExternalCatalogMetadataTransaction::validate() {
    _updates.reserve(_values.size());
    QSet<int> seenRows;
    for (const QVariant &value : _values) {
        const QVariantMap map = value.toMap();
        const QString entryId = map.value(
            QStringLiteral("entryId")).toString();
        const int row = _model.rowForEntryId(entryId);
        if (entryId.isEmpty() || !_model.validRow(row)
            || seenRows.contains(row)) {
            return false;
        }
        const ExternalCatalogModel::Entry *entry = _model.entryAt(row);
        if (map.contains(QStringLiteral("index"))
            && map.value(QStringLiteral("index")).toInt()
                != entry->sourceIndex) {
            return false;
        }
        seenRows.insert(row);
        _updates.append({row, map});
    }
    return true;
}

ExternalCatalogMetadataTransaction::RowState
ExternalCatalogMetadataTransaction::captureState(
    const ExternalCatalogModel::Entry &entry) {
    return {
        .image = entry.image,
        .localPath = entry.localPath,
        .sourceIdentity = entry.sourceIdentity,
        .source = entry.source,
        .contentVersion = entry.contentVersion,
        .mtimeNs = entry.mtimeNs,
        .size = entry.size,
        .displayFields = entry.displayFields,
        .highlightStyle = entry.highlightStyle,
        .iconPath = entry.iconPath,
    };
}

bool ExternalCatalogMetadataTransaction::carriesSourceDescriptor(
    const QVariantMap &value) {
    return value.contains(QStringLiteral("resourceId"))
        || value.contains(QStringLiteral("sourceKey"))
        || value.contains(QStringLiteral("contentVersion"))
        || value.contains(QStringLiteral("versionStrength"))
        || value.contains(QStringLiteral("storageClass"))
        || value.contains(QStringLiteral("accessProfile"))
        || value.contains(QStringLiteral("mimeType"));
}

void ExternalCatalogMetadataTransaction::applyBaseFields(
    ExternalCatalogModel::Entry &entry,
    const QVariantMap &value) const {
    if (value.contains(QStringLiteral("isImage"))) {
        entry.image = value.value(QStringLiteral("isImage")).toBool();
    }
    if (value.contains(QStringLiteral("localPath"))) {
        entry.localPath = value.value(
            QStringLiteral("localPath")).toString();
    }
    if (value.contains(QStringLiteral("mtimeNanos"))
        || value.contains(QStringLiteral("mtimeNs"))) {
        entry.mtimeNs = integerValue(
            value, QStringLiteral("mtimeNs"),
            QStringLiteral("mtimeNanos"), 0);
    }
    if (value.contains(QStringLiteral("size"))
        || value.contains(QStringLiteral("fileSize"))) {
        entry.size = integerValue(
            value, QStringLiteral("size"),
            QStringLiteral("fileSize"), -1);
    }
    if (carriesDisplayFields(value)) {
        const QVariantMap fields = displayFields(value);
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
            entry.displayFields.insert(it.key(), it.value());
        }
        entry.metadataDeferred = false;
    }
}

void ExternalCatalogMetadataTransaction::reconcileSource(
    ExternalCatalogModel::Entry &entry, const QVariantMap &value,
    const RowState &previous) const {
    const bool previousSourceUsedLocalPath =
        !previous.localPath.isEmpty()
        && previous.source.resourceId == previous.localPath;
    if (carriesSourceDescriptor(value)
        || (!entry.source.isValid() && !entry.localPath.isEmpty())
        || (previousSourceUsedLocalPath
            && previous.localPath != entry.localPath)) {
        QVariantMap sourceMap = value;
        sourceMap.insert(QStringLiteral("localPath"), entry.localPath);
        entry.source = sourceDescriptor(
            sourceMap, entry.name, entry.size, entry.mtimeNs);
        entry.contentVersion = entry.source.contentVersion;
    } else {
        entry.source.size = entry.size;
        entry.source.displayName = entry.name;
        if (entry.contentVersion.isEmpty() && entry.mtimeNs != 0) {
            entry.contentVersion = QString::number(entry.mtimeNs);
            entry.source.contentVersion = entry.contentVersion;
            if (entry.source.versionStrength.isEmpty()
                && !entry.localPath.isEmpty()) {
                entry.source.versionStrength =
                    QStringLiteral("local-stat");
            }
        }
    }
    entry.sourceIdentity = entry.source.isValid()
        ? entry.source.runtimeIdentity()
        : ThumbnailMemoryCache::canonicalSourceIdentity(entry.localPath);
}

bool ExternalCatalogMetadataTransaction::sourceChanged(
    const ExternalCatalogModel::Entry &entry,
    const RowState &previous) {
    return previous.image != entry.image
        || previous.localPath != entry.localPath
        || previous.sourceIdentity != entry.sourceIdentity
        || previous.contentVersion != entry.contentVersion
        || previous.source.resourceId != entry.source.resourceId
        || previous.source.sourceKey != entry.source.sourceKey
        || previous.mtimeNs != entry.mtimeNs
        || previous.size != entry.size;
}

void ExternalCatalogMetadataTransaction::invalidateChangedSource(
    ExternalCatalogModel::Entry &entry, const RowState &previous) {
    for (const QString &identity :
         {previous.sourceIdentity, entry.sourceIdentity}) {
        if (!identity.isEmpty()) {
            _model._metadataPendingVersions.remove(identity);
            _model._metadataResolvedPaths.remove(identity);
        }
    }
    _model._viewerImageCache.remove(previous.sourceIdentity);
    _model.clearPublishedImage(entry);
    entry.originalSize = {};
    if (entry.item) {
        entry.item->setFullSize({});
    }
    _viewerSourceInvalidated =
        _viewerSourceInvalidated || entry.id == _model._viewerEntryId;
}

void ExternalCatalogMetadataTransaction::updateLookupIndexes(
    int row, ExternalCatalogModel::Entry &entry,
    const RowState &previous) {
    if (previous.localPath != entry.localPath) {
        if (!previous.localPath.isEmpty()) {
            _model._pathToRow.remove(QDir::cleanPath(previous.localPath));
        }
        if (!entry.localPath.isEmpty()) {
            _model._pathToRow.insert(QDir::cleanPath(entry.localPath), row);
        }
        if (entry.item) {
            const QFileInfo pathInfo(entry.localPath);
            entry.item->setFolderPath(entry.localPath.isEmpty()
                ? QString() : pathInfo.absolutePath());
            entry.item->setFileName(!entry.name.isEmpty()
                ? entry.name : pathInfo.fileName());
        }
    }
    if (previous.sourceIdentity == entry.sourceIdentity) {
        return;
    }
    if (!previous.sourceIdentity.isEmpty()) {
        _model._sourceEntryIds.remove(previous.sourceIdentity, entry.id);
        if (_model._sourceToRow.value(previous.sourceIdentity, -1) == row) {
            _model._sourceToRow.remove(previous.sourceIdentity);
        }
    }
    if (!entry.sourceIdentity.isEmpty()) {
        _model._sourceEntryIds.insert(entry.sourceIdentity, entry.id);
        _model._sourceToRow.insert(entry.sourceIdentity, row);
    }
}

void ExternalCatalogMetadataTransaction::scheduleChangedSource(
    int row, const ExternalCatalogModel::Entry &entry) {
    if (!entry.image
        || (!entry.source.isValid() && entry.localPath.isEmpty())) {
        return;
    }
    if (_model._metadataLastVisibleRows.contains(row)
        || entry.id == _model._viewerEntryId) {
        _model.requestImageMetadataForRow(row, true);
    }
    if (_model._catalogMetadataRequested) {
        _model._catalogMetadataCursor = qMin(
            _model._catalogMetadataCursor, row);
        _model.scheduleMetadataPump();
    }
}

void ExternalCatalogMetadataTransaction::updateImageInfo(
    ExternalCatalogModel::Entry &entry, const QVariantMap &value,
    bool changedSource) {
    ImageInfo imageInfo = changedSource ? ImageInfo{} : entry.imageInfo;
    imageInfo.path = entry.sourceIdentity;
    imageInfo.source = entry.source;
    imageInfo.requestNamespace = _model._sessionId;
    imageInfo.sourceVersionToken = entry.contentVersion;
    imageInfo.lastModified = entry.mtimeNs != 0
        ? QDateTime::fromMSecsSinceEpoch(
              entry.mtimeNs / 1000000, QTimeZone::UTC)
        : QDateTime{};
    imageInfo.fileSize = entry.size;
    entry.imageInfo = imageInfo;
    if (!entry.item) {
        return;
    }
    entry.item->setIsImage(entry.image);
    entry.item->setInfo(entry.imageInfo);
    if (carriesDisplayFields(value)) {
        entry.item->setDisplayFields(entry.displayFields);
    }
}

void ExternalCatalogMetadataTransaction::updateAppearance(
    ExternalCatalogModel::Entry &entry, const QVariantMap &value,
    const RowState &previous) const {
    if (value.contains(QStringLiteral("highlightStyle"))) {
        _model.setEntryHighlightStyle(
            entry, value.value(QStringLiteral("highlightStyle")).toMap());
    } else if (previous.image != entry.image) {
        _model.setEntryHighlightStyle(entry, entry.highlightStyle);
    }
}

void ExternalCatalogMetadataTransaction::applyRow(
    const RowUpdate &update) {
    ExternalCatalogModel::Entry &entry = *_model.entryAt(update.row);
    const RowState previous = captureState(entry);
    applyBaseFields(entry, update.value);
    reconcileSource(entry, update.value, previous);
    const bool changedSource = sourceChanged(entry, previous);
    if (changedSource) {
        invalidateChangedSource(entry, previous);
    }
    updateLookupIndexes(update.row, entry, previous);
    if (changedSource) {
        scheduleChangedSource(update.row, entry);
    }
    updateImageInfo(entry, update.value, changedSource);
    updateAppearance(entry, update.value, previous);
    if (changedSource || previous.displayFields != entry.displayFields
        || previous.highlightStyle != entry.highlightStyle
        || previous.iconPath != entry.iconPath) {
        _changedRows.append(update.row);
    }
}

void ExternalCatalogMetadataTransaction::emitChangedRows() {
    std::sort(_changedRows.begin(), _changedRows.end());
    for (qsizetype offset = 0; offset < _changedRows.size();) {
        const int first = _changedRows.at(offset);
        int last = first;
        ++offset;
        while (offset < _changedRows.size()
               && _changedRows.at(offset) == last + 1) {
            last = _changedRows.at(offset++);
        }
        emit _model.dataChanged(
            _model.index(first, 0), _model.index(last, 0),
            {FileListModel::ImageFileRole,
             FileListModel::ImageIdUrlRole,
             FileListModel::IsImageRole,
             FileListModel::ImageFullSizeRole,
             FileListModel::LastModifiedRole,
             FileListModel::FileSizeRole,
             ExternalCatalogModel::LocalPathRole,
             ExternalCatalogModel::VersionTokenRole,
             ExternalCatalogModel::KnownImageSizeRole,
             ExternalCatalogModel::VisualSnapshotRole});
    }
}

bool ExternalCatalogMetadataTransaction::run() {
    if (_model._shutdown || !validate()) {
        return false;
    }
    _changedRows.reserve(_updates.size());
    for (const RowUpdate &update : std::as_const(_updates)) {
        applyRow(update);
    }
    emitChangedRows();
    if (_viewerSourceInvalidated) {
        _model.notifyViewerImageUrlChanged();
    }
    return true;
}

bool ExternalCatalogModel::applyMetadata(const QVariantList &values) {
    ExternalCatalogMetadataTransaction transaction(*this, values);
    return transaction.run();
}

} // namespace ZoinGallery
