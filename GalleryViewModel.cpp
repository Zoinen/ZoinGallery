#include "GalleryViewModel.h"

#include <QFileInfo>
#include <QSettings>

GalleryViewModel::GalleryViewModel(FileListModel *sourceModel, QObject *parent)
    : QSortFilterProxyModel(parent) {
    _collator.setNumericMode(true);
    _collator.setCaseSensitivity(Qt::CaseInsensitive);

    QSettings settings;
    _sortMode = normalizeSortMode(settings.value("gallerySortMode", NameAscending).toInt());

    setDynamicSortFilter(true);
    setSourceModel(sourceModel);
    sort(0);
}

void GalleryViewModel::setSourceModel(QAbstractItemModel *sourceModel) {
    if (fileListModel()) {
        disconnect(fileListModel(), nullptr, this, nullptr);
    }

    QSortFilterProxyModel::setSourceModel(sourceModel);

    if (fileListModel()) {
        connect(fileListModel(), &FileListModel::selectionChanged, this, [this]() {
            if (_selectedOnly) {
                refreshRowsFilter();
            }
        });
        connect(fileListModel(), &FileListModel::directOpenPathChanged, this, [this]() {
            if (_selectedOnly) {
                refreshRowsFilter();
            }
        });
    }
}

bool GalleryViewModel::selectedOnly() const {
    return _selectedOnly;
}

void GalleryViewModel::setSelectedOnly(bool selectedOnly) {
    if (_selectedOnly == selectedOnly) {
        return;
    }

    _selectedOnly = selectedOnly;
    refreshRowsFilter();
    emit selectedOnlyChanged();
}

int GalleryViewModel::sortMode() const {
    return _sortMode;
}

void GalleryViewModel::setSortMode(int sortMode) {
    const SortMode normalizedSortMode = normalizeSortMode(sortMode);
    if (_sortMode == normalizedSortMode) {
        return;
    }

    _sortMode = normalizedSortMode;
    QSettings settings;
    settings.setValue("gallerySortMode", static_cast<int>(_sortMode));

    // The comparator depends on _sortMode, but Qt only sees that we are still
    // sorting column 0. Clear the proxy sort first so the next sort rebuilds
    // the mapping and emits the layout-change signals the masonry view uses.
    sort(-1);
    sort(0);
    emit sortModeChanged();
}

QString GalleryViewModel::sortModeLabel() const {
    return labelForSortMode(_sortMode);
}

QVariantList GalleryViewModel::sortModeOptions() const {
    QVariantList options;
    const QList<SortMode> modes = {
        NameAscending,
        NameDescending,
        ModifiedAscending,
        ModifiedDescending,
        ExtensionAscending,
        ExtensionDescending,
        SizeAscending,
        SizeDescending,
    };

    for (SortMode mode : modes) {
        QVariantMap option;
        option["value"] = static_cast<int>(mode);
        option["label"] = labelForSortMode(mode);
        options.append(option);
    }
    return options;
}

int GalleryViewModel::mapToSourceRow(int viewRow) const {
    const QModelIndex sourceIndex = mapToSource(index(viewRow, 0));
    return sourceIndex.isValid() ? sourceIndex.row() : -1;
}

int GalleryViewModel::mapFromSourceRow(int sourceRow) const {
    if (!sourceModel()) {
        return -1;
    }

    const QModelIndex viewIndex = mapFromSource(sourceModel()->index(sourceRow, 0));
    return viewIndex.isValid() ? viewIndex.row() : -1;
}

QVariantList GalleryViewModel::mapToSourceRows(const QVariantList &viewRows) const {
    QVariantList sourceRows;
    sourceRows.reserve(viewRows.size());
    for (const QVariant &viewRowValue : viewRows) {
        bool ok = false;
        const int viewRow = viewRowValue.toInt(&ok);
        if (!ok) {
            continue;
        }

        const int sourceRow = mapToSourceRow(viewRow);
        if (sourceRow != -1) {
            sourceRows.append(sourceRow);
        }
    }
    return sourceRows;
}

QVariantList GalleryViewModel::sourceRowsForViewRange(int anchorViewRow, int targetViewRow, bool includeTarget) const {
    QVariantList sourceRows;
    if (anchorViewRow < 0 || targetViewRow < 0 || anchorViewRow >= rowCount() || targetViewRow >= rowCount()) {
        return sourceRows;
    }

    int first = qMin(anchorViewRow, targetViewRow);
    int last = qMax(anchorViewRow, targetViewRow);
    if (!includeTarget) {
        if (targetViewRow > anchorViewRow) {
            last = targetViewRow - 1;
        }
        else if (targetViewRow < anchorViewRow) {
            first = targetViewRow + 1;
        }
        else {
            return sourceRows;
        }
    }

    sourceRows.reserve(last - first + 1);
    for (int viewRow = first; viewRow <= last; viewRow++) {
        const int sourceRow = mapToSourceRow(viewRow);
        if (sourceRow != -1) {
            sourceRows.append(sourceRow);
        }
    }
    return sourceRows;
}

int GalleryViewModel::nearestVisibleRow(int sourceRow) const {
    const int exactViewRow = mapFromSourceRow(sourceRow);
    if (exactViewRow != -1) {
        return exactViewRow;
    }
    if (!sourceModel() || rowCount() == 0) {
        return -1;
    }

    for (int viewRow = 0; viewRow < rowCount(); viewRow++) {
        const int candidateSourceRow = mapToSourceRow(viewRow);
        if (candidateSourceRow >= sourceRow) {
            return viewRow;
        }
    }
    return rowCount() - 1;
}

ImageFile *GalleryViewModel::rootItem() const {
    return nullptr;
}

void GalleryViewModel::decodeImages(const QList<ImageDecodeRequest> &requests) {
    if (ThumbnailsRequestInterface *requestModel = dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())) {
        requestModel->decodeImages(requests);
    }
}

void GalleryViewModel::cancelAllRunners() {
    if (ThumbnailsRequestInterface *requestModel = dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())) {
        requestModel->cancelAllRunners();
    }
}

void GalleryViewModel::cancelAllDecodeRunners() {
    if (ThumbnailsRequestInterface *requestModel = dynamic_cast<ThumbnailsRequestInterface *>(sourceModel())) {
        requestModel->cancelAllDecodeRunners();
    }
}

bool GalleryViewModel::preserveViewStateOnReset() const {
    const auto *requestModel =
        dynamic_cast<const ThumbnailsRequestInterface *>(sourceModel());
    return requestModel && requestModel->preserveViewStateOnReset();
}

bool GalleryViewModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (!_selectedOnly) {
        return true;
    }
    if (sourceParent.isValid() || !sourceModel()) {
        return true;
    }

    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!sourceIndex.isValid()) {
        return false;
    }

    const ImageFile *item = sourceIndex.data(FileListModel::ImageFileRole).value<ImageFile *>();
    if (item && !fileListModel()->directOpenPath().isEmpty() && item->fullPath() == fileListModel()->directOpenPath()) {
        return true;
    }

    return sourceIndex.data(FileListModel::FolderRole).toBool() ||
           sourceIndex.data(FileListModel::SelectedRole).toBool();
}

bool GalleryViewModel::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const ImageFile *leftItem = left.data(FileListModel::ImageFileRole).value<ImageFile *>();
    const ImageFile *rightItem = right.data(FileListModel::ImageFileRole).value<ImageFile *>();
    if (!leftItem || !rightItem) {
        return QSortFilterProxyModel::lessThan(left, right);
    }

    const bool leftFolder = leftItem->isFolder();
    const bool rightFolder = rightItem->isFolder();
    if (leftFolder != rightFolder) {
        return leftFolder;
    }

    const int primaryComparison = compareItems(leftItem, rightItem);
    if (primaryComparison != 0) {
        return isReverse(_sortMode) ? primaryComparison > 0 : primaryComparison < 0;
    }

    int fallbackComparison = compareNatural(leftItem->fileName(), rightItem->fileName());
    if (fallbackComparison != 0) {
        return fallbackComparison < 0;
    }

    fallbackComparison = QString::compare(leftItem->fullPath(), rightItem->fullPath(), Qt::CaseSensitive);
    if (fallbackComparison != 0) {
        return fallbackComparison < 0;
    }

    return left.row() < right.row();
}

FileListModel *GalleryViewModel::fileListModel() const {
    return qobject_cast<FileListModel *>(QSortFilterProxyModel::sourceModel());
}

GalleryViewModel::SortMode GalleryViewModel::normalizeSortMode(int sortMode) {
    if (sortMode < NameAscending || sortMode > SizeDescending) {
        return NameAscending;
    }
    return static_cast<SortMode>(sortMode);
}

bool GalleryViewModel::isReverse(SortMode sortMode) {
    return sortMode == NameDescending ||
           sortMode == ModifiedDescending ||
           sortMode == ExtensionDescending ||
           sortMode == SizeDescending;
}

QString GalleryViewModel::labelForSortMode(SortMode sortMode) {
    switch (sortMode) {
    case NameAscending:
        return QStringLiteral("Name A-Z");
    case NameDescending:
        return QStringLiteral("Name Z-A");
    case ModifiedAscending:
        return QStringLiteral("Modified oldest first");
    case ModifiedDescending:
        return QStringLiteral("Modified newest first");
    case ExtensionAscending:
        return QStringLiteral("Extension A-Z");
    case ExtensionDescending:
        return QStringLiteral("Extension Z-A");
    case SizeAscending:
        return QStringLiteral("Size smallest first");
    case SizeDescending:
        return QStringLiteral("Size largest first");
    }

    return QStringLiteral("Name A-Z");
}

void GalleryViewModel::refreshRowsFilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

int GalleryViewModel::compareItems(const ImageFile *leftItem, const ImageFile *rightItem) const {
    int comparison = 0;
    switch (_sortMode) {
    case NameAscending:
    case NameDescending:
        comparison = compareNatural(leftItem->fileName(), rightItem->fileName());
        break;
    case ModifiedAscending:
    case ModifiedDescending:
        comparison = compareDateTime(leftItem->lastModified(), rightItem->lastModified());
        break;
    case ExtensionAscending:
    case ExtensionDescending:
        comparison = compareNatural(QFileInfo(leftItem->fileName()).suffix(),
                                    QFileInfo(rightItem->fileName()).suffix());
        break;
    case SizeAscending:
    case SizeDescending:
        comparison = compareInteger(leftItem->fileSize(), rightItem->fileSize());
        break;
    }

    return comparison;
}

int GalleryViewModel::compareNatural(const QString &left, const QString &right) const {
    const int comparison = _collator.compare(left, right);
    if (comparison != 0) {
        return comparison;
    }
    return QString::compare(left, right, Qt::CaseSensitive);
}

int GalleryViewModel::compareDateTime(const QDateTime &left, const QDateTime &right) const {
    if (left == right) {
        return 0;
    }
    if (!left.isValid()) {
        return -1;
    }
    if (!right.isValid()) {
        return 1;
    }
    return left < right ? -1 : 1;
}

int GalleryViewModel::compareInteger(qint64 left, qint64 right) const {
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}
