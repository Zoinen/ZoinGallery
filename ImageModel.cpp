#include "ImageModel.h"

#include "FileListModel.h"

ImageModel::ImageModel(FileListModel *sourceModel)
    : QSortFilterProxyModel(sourceModel) {
    setSourceModel(sourceModel);
    _sourceModel = sourceModel;
}

int ImageModel::mapToSourceRow(int proxyRow) const {
    QModelIndex sourceIndex = mapToSource(index(proxyRow, 0));
    if (sourceIndex.isValid()) {
        return sourceIndex.row();
    }
    return -1;
}

int ImageModel::mapFromSourceRow(int sourceRow) const {
    QModelIndex proxyIndex = mapFromSource(sourceModel()->index(sourceRow, 0));
    if (proxyIndex.isValid()) {
        return proxyIndex.row();
    }
    return -1;
}

bool ImageModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    return !index.data(FileListModel::ImageRole).value<QImage>().isNull();
}

