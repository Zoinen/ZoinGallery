#ifndef ZOINGALLERY_SELECTEDIMAGESPANELBACKEND_H
#define ZOINGALLERY_SELECTEDIMAGESPANELBACKEND_H

#include <ZoinGallery/GalleryPanelBackend.h>

#include <QHash>
#include <QPointer>

class QAbstractItemModel;
class ImageFile;
class SelectedImagesModel;

namespace ZoinGallery {

class GalleryCatalogModel;

// Panel adapter for the standalone Selected Images filtered catalog. The
// adapter owns cursor/viewport identity while the source model remains the
// sole owner of image metadata, selection history, and decode scheduling.
class SelectedImagesPanelBackend : public GalleryPanelBackend {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel
               WRITE setSourceModel NOTIFY sourceModelChanged)

public:
    explicit SelectedImagesPanelBackend(QObject *parent = nullptr);

    QAbstractItemModel *sourceModel() const;
    void setSourceModel(QAbstractItemModel *model);

    GalleryCatalogModel *catalogModel() const override;
    int currentIndex() const override;
    qulonglong catalogRevision() const override;
    qulonglong selectionRevision() const override;
    QString entryIdAt(int index) const override;
    int indexForEntryId(const QString &entryId) const override;
    int sourceIndexAt(int index) const override;
    bool isSelectedAt(int index) const override;
    QString entryNameAt(int index) const override;
    bool isImageAt(int index) const override;
    QVariantMap highlightStyleAt(int index) const override;
    qreal panelScrollOffset() const override;
    void setPanelScrollOffset(qreal offset) override;
    QString panelViewportCursorEntryId() const override;
    void setPanelViewportCursorEntryId(const QString &entryId) override;
    bool panelViewportStateAvailable() const override;
    bool canRemoveEntries() const override;
    bool canDragEntries() const override;
    GalleryDragDescriptor prepareDrag(
        int index, bool singleItemOnly, int previewLimit) const override;
    GalleryFileOperationResult finalizeExternalDrag(
        const QVariantList &urls, Qt::DropAction action) override;
    void configureNativeDragCursors(QObject *dragSource) override;
    void removeEntry(int index) override;
    bool remoteAuthoritative() const override;
    void activateIndex(int index) override;
    void applySelectionIntent(const QStringList &selectedEntryIds,
                              const QStringList &deselectedEntryIds) override;

signals:
    void sourceModelChanged();

private:
    ImageFile *itemAt(int index) const;
    void disconnectSource();
    void connectSource();
    void rebuildIdentityIndex();
    void handleCatalogChange();
    void setCurrentIndex(int index);

    QPointer<SelectedImagesModel> _source;
    GalleryCatalogModel *_catalog = nullptr;
    QList<QMetaObject::Connection> _connections;
    QHash<QString, int> _rowByEntryId;
    QString _cursorEntryId;
    QString _viewportCursorEntryId;
    int _currentIndex = -1;
    qulonglong _catalogRevision = 0;
    qulonglong _selectionRevision = 0;
    qreal _scrollOffset = 0;
    bool _viewportStateAvailable = false;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_SELECTEDIMAGESPANELBACKEND_H
