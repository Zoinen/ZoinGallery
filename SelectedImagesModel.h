#ifndef SELECTEDIMAGESMODEL_H
#define SELECTEDIMAGESMODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QTimer>

#include "FileListModel.h"
#include "ViewerImageCache.h"

class DecodeManager;

class SelectedImagesModel : public QAbstractListModel,
                            public ZoinGallery::GalleryCatalogSource {
    Q_OBJECT
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY panelSelectionChanged)
    Q_PROPERTY(int totalPathCount READ totalPathCount NOTIFY activeGroupContentsChanged)
    Q_PROPERTY(int unavailableCount READ unavailableCount NOTIFY activeGroupContentsChanged)

public:
    SelectedImagesModel(
        FileListModel *sourceModel,
        QSharedPointer<ProviderImageStore> providerImageStore,
        QObject *parent = nullptr);
    SelectedImagesModel(
        FileListModel *sourceModel,
        QSharedPointer<ProviderImageStore> providerImageStore,
        DecodeManager *sharedDecodeManager,
        const QString &requestNamespace,
        const QString &imageIdPrefix,
        const QString &thumbnailProviderName,
        const QString &asyncProviderName,
        QObject *parent = nullptr);
    SelectedImagesModel(
        FileListModel *sourceModel,
        QSharedPointer<ProviderImageStore> providerImageStore,
        DecodeManager *sharedDecodeManager,
        const QString &requestNamespace,
        const QString &imageIdPrefix,
        const QString &thumbnailProviderName,
        const QString &asyncProviderName,
        qint64 viewerFitCacheByteBudget,
        qint64 viewerNativeCacheByteBudget,
        QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    ImageFile *rootItem() const override;
    void decodeImages(const QList<ImageDecodeRequest> &requests) override;
    void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;
    bool preserveViewStateOnReset() const override;
    Q_INVOKABLE void cancelAllDecodeViewerRunners();
    Q_INVOKABLE void cancelAllDecodeViewerRunnersForViewerClose();
    void prepareToClose();

    const ImageFile *itemForImageId(const QString &imageId) const;
    QImage viewerForImageId(const QString &imageId) const;
    QImage fullSizeViewerForImageId(const QString &imageId) const;
    bool containsPath(const QString &path) const;

    int selectedCount() const;
    int totalPathCount() const;
    int unavailableCount() const;
    Q_INVOKABLE bool isIndexSelected(int index) const;
    Q_INVOKABLE void setSelection(int index, bool selected);
    Q_INVOKABLE void toggleSelection(int index);
    Q_INVOKABLE void replaceSelection(int index);
    Q_INVOKABLE void selectRange(int anchorIndex, int targetIndex, bool preserveExisting);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setAllSelection(bool selected);
    void applySelectionChanges(const QList<int> &selectedIndexes,
                               const QList<int> &deselectedIndexes);
    Q_INVOKABLE void removeFromCollection(int index);
    Q_INVOKABLE int mapToSourceRow(int viewRow) const;
    Q_INVOKABLE int mapFromSourceRow(int sourceRow) const;
    Q_INVOKABLE QVariantList mapToSourceRows(const QVariantList &viewRows) const;
    Q_INVOKABLE QVariantList viewerPrefetchSourceRows(
        int currentViewRow, int imageCount = 16) const;
    Q_INVOKABLE QVariantList sourceRowsForViewRange(int anchorViewRow, int targetViewRow,
                                                    bool includeTarget) const;
    Q_INVOKABLE void beginSelectionPreview();
    Q_INVOKABLE void previewSelectionIndexes(const QVariantList &indexes, int mode);
    Q_INVOKABLE void commitSelectionPreview(const QString &description);
    Q_INVOKABLE void cancelSelectionPreview();

    Q_INVOKABLE QVariantList dragIndexesForIndex(int index, bool singleItemOnly = false) const;
    Q_INVOKABLE QVariantList dragUrlsForIndex(int index, bool singleItemOnly = false) const;
    Q_INVOKABLE QVariantMap dragPreviewItemsForIndex(int index, int limit,
                                                     bool singleItemOnly = false) const;
    QVariantMap finalizeExternalDrag(
        const QVariantList &urls, int dropAction);
    void configureNativeDragCursors(QObject *dragSource);
    Q_INVOKABLE void requestViewer(int index, int width = -1, int height = -1);
    Q_INVOKABLE void requestViewerInOrder(
        int index, const QVariantList &orderedSourceRows,
        int width = -1, int height = -1);
    Q_INVOKABLE void requestViewerAt(
        int index, int width = -1, int height = -1);
    Q_INVOKABLE QString bestViewerImageUrlForIndex(int index) const;
    Q_INVOKABLE QString preparedViewerImageUrlForIndex(
        int index, int width = -1, int height = -1) const;
    Q_INVOKABLE QSize viewerImageOriginalSizeForIndex(int index) const;
    Q_INVOKABLE QColor selectionGroupColorForIndex(int index) const;

signals:
    void panelSelectionChanged();
    void activeGroupContentsChanged();
    void viewerImageIdUrlChanged(const QString &imageId, int level);
    void viewerImageCacheChanged(int index);
    void viewerReset();
    void thumbnailReloadRequested();

private:
    void syncFromPersistentSelection(bool preserveTransientState = false);
    void syncPathsFromPersistentSelection(const QStringList &paths);
    void refreshWatchedImageMetadata(const QStringList &paths);
    void requestMissingImageInfo();
    void emitThumbnailInfoFlush();
    ImageFile *applyImageInfo(const ImageInfo &info);
    bool isCurrentFileVersion(const ImageFile *item,
                              const ImageInfo &info) const;
    void refreshCurrentViewerAfterMetadata(ImageFile *item);
    void rememberFailedImageInfo(const ImageInfo &info);
    void rememberFailedDecodeRequest(const ImageDecodeRequest &request);
    void scheduleFailedImageWorkRetry();
    void retryFailedImageWork();
    void onImageInfoAvailable(const ImageInfo &info);
    void onImagesInfoAvailable(const QList<ImageInfo> &results);
    void onImageAvailable(const ImageDecodeRequest &request, const QImage &image,
                          const DecodedImageInfo &decodedInfo);
    void emitSelectionDataChanged();
    int indexForPath(const QString &path) const;
    void readImagesInfo(const QList<QString> &paths,
                        bool isFromEmbeddedView);
    void cancelSessionRequests();
    bool acceptsRequestNamespace(const QString &requestNamespace) const;
    void configureImageFile(ImageFile *item) const;

    FileListModel *_selectionSourceModel;
    DecodeManager *_decodeManager;
    bool _ownsDecodeManager = false;
    QString _requestNamespace;
    QString _imageIdPrefix;
    QString _thumbnailProviderName = QStringLiteral("zoingallery-thumbnails");
    QString _asyncProviderName = QStringLiteral("zoingallery-async");
    QSharedPointer<ProviderImageStore> _providerImageStore;
    ViewerImageCache _viewerImageCache;
    QList<ImageFile *> _items;
    QHash<QString, ImageFile *> _pathToItem;
    QHash<QString, ImageFile *> _imageIdToItem;
    QHash<QString, QDateTime> _selectionAddedAt;
    QTimer _imageInfoRequestTimer;
    QTimer _failedImageWorkRetryTimer;
    QHash<QString, ImageInfo> _failedImageInfoRequests;
    QHash<QString, ImageDecodeRequest> _failedImageDecodeRequests;
    QHash<QString, int> _failedImageInfoRetryAttempts;
    QHash<QString, int> _failedImageDecodeRetryAttempts;
    QSet<QString> _selectionPreviewSnapshot;
    QSet<QString> _activeGroupPaths;
    bool _selectionPreviewActive = false;
    int _lastImageId = 0;
    int _totalPathCount = 0;
    int _unavailableCount = 0;
    QString _activeGroupId;
    QString _currentViewerPath;
    QSize _currentViewerRequestSize;
    int _failedImageWorkRetryDelayMs = 250;
    bool _hasCurrentViewerRequest = false;
    bool _preserveViewStateOnReset = false;
    bool _isClosing = false;
};

#endif // SELECTEDIMAGESMODEL_H
