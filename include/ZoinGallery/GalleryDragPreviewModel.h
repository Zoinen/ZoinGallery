#ifndef ZOINGALLERY_GALLERYDRAGPREVIEWMODEL_H
#define ZOINGALLERY_GALLERYDRAGPREVIEWMODEL_H

#include <ZoinGallery/GalleryPanelBackend.h>

#include <QAbstractListModel>

namespace ZoinGallery {

// Small, bounded model used only while a native drag is being prepared. It
// replaces the old list-of-maps QML contract and never reflects the catalog.
class GalleryDragPreviewModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        ImageSourceRole = Qt::UserRole + 1,
        IconSourceRole,
        LabelRole,
        IsImageRole,
        IsDirectoryRole,
    };

    explicit GalleryDragPreviewModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QList<GalleryDragPreviewItem> &items);
    void clear();

private:
    QList<GalleryDragPreviewItem> _items;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYDRAGPREVIEWMODEL_H
