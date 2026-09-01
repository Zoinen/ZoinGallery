#ifndef ZOINGALLERY_GALLERYCATALOGMODEL_H
#define ZOINGALLERY_GALLERYCATALOGMODEL_H

#include <ZoinGallery/GalleryCatalogSource.h>

#include <QIdentityProxyModel>
#include <QPointer>
#include <QVariantList>

namespace ZoinGallery {

class GallerySession;

// Fixed-role façade shared by local, external, and filtered catalogs. Source
// models may keep their implementation roles; renderer/controller consumers
// see one stable contract and never decode ad-hoc QVariantMap rows.
class GalleryCatalogModel final : public QIdentityProxyModel,
                                  public GalleryCatalogSource {
    Q_OBJECT
    Q_PROPERTY(bool sparseCatalog READ sparseCatalog)
    Q_PROPERTY(QVariantList materializedRows READ materializedRows)

public:
    enum Role {
        // Keep the established renderer-facing role IDs while replacing
        // source-specific role names with one fixed contract. Local and
        // external sources can therefore flow through this proxy without a
        // translated dataChanged signal causing a second model update.
        IsImageRole = Qt::UserRole + 100,
        ImageIdUrlRole,
        IsDirectoryRole,
        KnownImageSizeRole,
        ImageFileRole,
        FolderViewRole,
        ExifRole,
        TimeToFlushRole,
        IsSelectedRole,
        SelectionGroupIdRole,
        SelectionGroupColorRole,
        LastModifiedRole,
        FileSizeRole,
        EntryIdRole,
        SourceIndexRole,
        LocalPathRole,
        VersionTokenRole,
        NameRole,
        VisualSnapshotRole = Qt::UserRole + 119,
        CachedMetadataBatchRole = Qt::UserRole + 200,
    };
    Q_ENUM(Role)

    explicit GalleryCatalogModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &proxyIndex,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setSourceModel(QAbstractItemModel *sourceModel) override;
    void setSession(GallerySession *session);
    bool sparseCatalog() const;
    QVariantList materializedRows() const;

    void decodeImages(
        const QList<ImageDecodeRequest> &requests) override;
    void requestImageMetadata(const QList<int> &rows,
                              bool highPriority,
                              bool catalogWide = false) override;
    void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;
    bool preserveViewStateOnReset() const override;
    ::ImageFile *rootItem() const override;

private:
    GalleryCatalogSource *catalogSource() const;
    ::ImageFile *imageForIndex(const QModelIndex &proxyIndex) const;
    QVariant derivedData(const QModelIndex &proxyIndex, int role,
                         ::ImageFile *image) const;
    QVariant visualSnapshotData(int row, ::ImageFile *image) const;
    void rebuildRoleMap();
    QHash<int, int> _sourceRoles;
    QPointer<GallerySession> _session;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYCATALOGMODEL_H
