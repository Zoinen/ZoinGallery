#include <ZoinGallery/GalleryDragPreviewModel.h>

namespace ZoinGallery {

GalleryDragPreviewModel::GalleryDragPreviewModel(QObject *parent)
    : QAbstractListModel(parent) {}

int GalleryDragPreviewModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : _items.size();
}

QVariant GalleryDragPreviewModel::data(
    const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= _items.size()) {
        return {};
    }
    const GalleryDragPreviewItem &item = _items.at(index.row());
    switch (role) {
    case ImageSourceRole: return item.imageSource;
    case IconSourceRole: return item.iconSource;
    case LabelRole: return item.label;
    case IsImageRole: return item.image;
    case IsDirectoryRole: return item.directory;
    default: return {};
    }
}

QHash<int, QByteArray> GalleryDragPreviewModel::roleNames() const {
    return {
        {ImageSourceRole, QByteArrayLiteral("imageSource")},
        {IconSourceRole, QByteArrayLiteral("iconSource")},
        {LabelRole, QByteArrayLiteral("label")},
        {IsImageRole, QByteArrayLiteral("isImage")},
        {IsDirectoryRole, QByteArrayLiteral("isDirectory")},
    };
}

void GalleryDragPreviewModel::setItems(
    const QList<GalleryDragPreviewItem> &items) {
    beginResetModel();
    _items = items;
    endResetModel();
}

void GalleryDragPreviewModel::clear() {
    if (_items.isEmpty()) {
        return;
    }
    beginResetModel();
    _items.clear();
    endResetModel();
}

} // namespace ZoinGallery
