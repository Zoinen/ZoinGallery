#ifndef ZOINGALLERY_GALLERYPANELBACKEND_H
#define ZOINGALLERY_GALLERYPANELBACKEND_H

#include <QObject>
#include <QList>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QAbstractItemModel;

namespace ZoinGallery {

class GalleryCatalogModel;

struct GalleryDragPreviewItem {
    QUrl imageSource;
    QUrl iconSource;
    QString label;
    bool image = false;
    bool directory = false;
};

struct GalleryDragDescriptor {
    QVariantList urls;
    QList<GalleryDragPreviewItem> previewItems;
    int totalCount = 0;
};

struct GalleryFileOperationResult {
    bool success = false;
    Qt::DropAction action = Qt::IgnoreAction;
    QString title;
    QString message;
};

GalleryDragDescriptor galleryDragDescriptorFromVariants(
    const QVariantList &urls, const QVariantMap &preview);
GalleryFileOperationResult galleryFileOperationResultFromVariant(
    const QVariantMap &result);

class GalleryPanelBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(GalleryCatalogModel *catalogModel READ catalogModel CONSTANT)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(qulonglong catalogRevision READ catalogRevision
               NOTIFY catalogRevisionChanged)
    Q_PROPERTY(qulonglong selectionRevision READ selectionRevision
               NOTIFY selectionRevisionChanged)

public:
    explicit GalleryPanelBackend(QObject *parent = nullptr);
    ~GalleryPanelBackend() override;

    virtual GalleryCatalogModel *catalogModel() const = 0;
    virtual int currentIndex() const = 0;
    virtual qulonglong catalogRevision() const = 0;
    virtual qulonglong selectionRevision() const = 0;
    virtual QString entryIdAt(int index) const = 0;
    virtual int indexForEntryId(const QString &entryId) const = 0;
    virtual int sourceIndexAt(int index) const = 0;
    virtual bool isSelectedAt(int index) const = 0;
    virtual QString entryNameAt(int index) const;
    virtual bool isImageAt(int index) const;
    virtual QVariantMap highlightStyleAt(int index) const;
    virtual QString currentPath() const;
    virtual bool catalogReady() const;
    virtual qreal panelScrollOffset() const;
    virtual void setPanelScrollOffset(qreal offset);
    virtual QString panelViewportCursorEntryId() const;
    virtual void setPanelViewportCursorEntryId(const QString &entryId);
    virtual bool panelViewportStateAvailable() const;
    virtual void ensurePreviews();
    virtual bool canRemoveEntries() const;
    virtual bool canDragEntries() const;
    virtual bool canDropIntoDirectories() const;
    virtual bool canPreviewDirectories() const;
    virtual GalleryDragDescriptor prepareDrag(
        int index, bool singleItemOnly, int previewLimit) const;
    virtual GalleryFileOperationResult finalizeExternalDrag(
        const QVariantList &urls, Qt::DropAction action);
    virtual void configureNativeDragCursors(QObject *dragSource);
    virtual GalleryFileOperationResult dropUrlsIntoDirectory(
        const QVariantList &urls, int directoryIndex,
        Qt::DropAction action);
    virtual void removeEntry(int index);
    virtual QAbstractItemModel *directoryPreviewModel(int index);
    virtual bool remoteAuthoritative() const = 0;
    virtual void activateIndex(int index) = 0;
    virtual void applySelectionIntent(const QStringList &selectedEntryIds,
                                      const QStringList &deselectedEntryIds) = 0;

signals:
    void currentIndexChanged();
    void catalogRevisionChanged();
    void selectionRevisionChanged();
    void catalogChanged();
    void currentPathChanged();
    void catalogReadyChanged();
    void panelViewportChanged();
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYPANELBACKEND_H
