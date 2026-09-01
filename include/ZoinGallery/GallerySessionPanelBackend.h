#ifndef ZOINGALLERY_GALLERYSESSIONPANELBACKEND_H
#define ZOINGALLERY_GALLERYSESSIONPANELBACKEND_H

#include <ZoinGallery/GalleryPanelBackend.h>

#include <QPointer>

namespace ZoinGallery {

class GalleryCatalogModel;
class GallerySession;

// Typed adapter between a GallerySession and the reusable panel controller.
// Keeping this outside GalleryPanelController makes transport/session policy
// replaceable without changing cursor, selection, or quick-search logic.
class GallerySessionPanelBackend final : public GalleryPanelBackend {
    Q_OBJECT
    Q_PROPERTY(GallerySession *session READ session CONSTANT)

public:
    explicit GallerySessionPanelBackend(GallerySession *session,
                                        QObject *parent = nullptr);

    GallerySession *session() const;
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
    QString currentPath() const override;
    bool catalogReady() const override;
    qreal panelScrollOffset() const override;
    void setPanelScrollOffset(qreal offset) override;
    QString panelViewportCursorEntryId() const override;
    void setPanelViewportCursorEntryId(const QString &entryId) override;
    bool panelViewportStateAvailable() const override;
    void ensurePreviews() override;
    bool canDragEntries() const override;
    bool canDropIntoDirectories() const override;
    bool canPreviewDirectories() const override;
    GalleryDragDescriptor prepareDrag(
        int index, bool singleItemOnly, int previewLimit) const override;
    GalleryFileOperationResult finalizeExternalDrag(
        const QVariantList &urls, Qt::DropAction action) override;
    void configureNativeDragCursors(QObject *dragSource) override;
    GalleryFileOperationResult dropUrlsIntoDirectory(
        const QVariantList &urls, int directoryIndex,
        Qt::DropAction action) override;
    QAbstractItemModel *directoryPreviewModel(int index) override;
    bool remoteAuthoritative() const override;
    void activateIndex(int index) override;
    void applySelectionIntent(const QStringList &selectedEntryIds,
                              const QStringList &deselectedEntryIds) override;

private:
    QPointer<GallerySession> _session;
    GalleryCatalogModel *_catalog = nullptr;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_GALLERYSESSIONPANELBACKEND_H
