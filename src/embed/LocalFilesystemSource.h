#ifndef ZOINGALLERY_LOCALFILESYSTEMSOURCE_H
#define ZOINGALLERY_LOCALFILESYSTEMSOURCE_H

#include <QObject>
#include <QList>
#include <QPair>
#include <QSharedPointer>
#include <QSize>
#include <QUrl>

class QAbstractItemModel;
class DecodeManager;
class FileListModel;
class GalleryViewModel;
class ImageFile;
class ImageModel;
class ProviderImageStore;
class SelectedImagesModel;

namespace ZoinGallery {

// Adapts the standalone filesystem catalog to the windowless GallerySession
// contract. The underlying models remain the single implementation of local
// scanning, watching, sorting, selection persistence, and file operations.
class LocalFilesystemSource final : public QObject {
    Q_OBJECT

public:
    LocalFilesystemSource(
        const QString &sessionId,
        const QString &thumbnailProviderName,
        const QString &asyncProviderName,
        const QSharedPointer<::ProviderImageStore> &store,
        ::DecodeManager *decodeManager,
        qint64 viewerFitCacheByteBudget,
        qint64 viewerNativeCacheByteBudget,
        QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    FileListModel *fileListModel() const;
    GalleryViewModel *galleryViewModel() const;
    SelectedImagesModel *selectedImagesModel() const;
    ImageModel *imageModel() const;

    QString currentPath() const;
    int cd(const QString &path, const QString &itemToSelect = QString());

    QString entryIdAt(int viewIndex) const;
    QString entryNameAt(int viewIndex) const;
    QString localPathAt(int viewIndex) const;
    bool isImageAt(int viewIndex) const;
    bool isDirectoryAt(int viewIndex) const;
    int sourceIndexAt(int viewIndex) const;
    QSize imageOriginalSizeAt(int viewIndex) const;
    int rowForEntryId(const QString &entryId) const;

    void toggleSelection(int viewIndex);
    void ensurePreviews();
    void requestViewer(int viewIndex, const QSize &size);
    void requestViewerAt(int targetViewIndex, const QSize &size);
    void clearViewer();
    QUrl viewerSource() const;
    int viewerSourceLevel() const;
    QUrl viewerSourceAt(int viewIndex) const;
    QList<QPair<QString, int>> viewerImageSourcesAt(int viewIndex) const;
    void shutdown();

signals:
    void currentPathChanged();
    void catalogChanged();
    void selectionChanged();
    void viewerSourceChanged();
    void viewerSourceAtChanged(int viewIndex);

private:
    ImageFile *itemAt(int viewIndex) const;

    QString _sessionId;
    QString _currentPath;
    FileListModel *_fileListModel = nullptr;
    GalleryViewModel *_galleryViewModel = nullptr;
    SelectedImagesModel *_selectedImagesModel = nullptr;
    ImageModel *_imageModel = nullptr;
    QUrl _viewerSource;
    int _viewerSourceLevel = -1;
    QSize _viewerViewportSize;
    bool _shutdown = false;
};

} // namespace ZoinGallery

#endif // ZOINGALLERY_LOCALFILESYSTEMSOURCE_H
