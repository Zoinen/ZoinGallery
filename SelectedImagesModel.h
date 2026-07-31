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

class SelectedImagesModel : public QAbstractListModel, public ThumbnailsRequestInterface {
    Q_OBJECT
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY panelSelectionChanged)
    Q_PROPERTY(int totalPathCount READ totalPathCount NOTIFY activeGroupContentsChanged)
    Q_PROPERTY(int unavailableCount READ unavailableCount NOTIFY activeGroupContentsChanged)

public:
    SelectedImagesModel(
        FileListModel *sourceModel,
        QSharedPointer<ProviderImageStore> providerImageStore,
        QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    ImageFile *rootItem() const override;
    void decodeImages(const QList<ImageDecodeRequest> &requests) override;
    void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;
    Q_INVOKABLE void cancelAllDecodeViewerRunners();
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
    Q_INVOKABLE void removeFromCollection(int index);
    Q_INVOKABLE int mapToSourceRow(int viewRow) const;
    Q_INVOKABLE int mapFromSourceRow(int sourceRow) const;
    Q_INVOKABLE QVariantList mapToSourceRows(const QVariantList &viewRows) const;
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
    Q_INVOKABLE void requestViewer(int index, int width = -1, int height = -1);
    Q_INVOKABLE QString bestViewerImageUrlForIndex(int index) const;
    Q_INVOKABLE QColor selectionGroupColorForIndex(int index) const;

signals:
    void panelSelectionChanged();
    void activeGroupContentsChanged();
    void viewerImageIdUrlChanged(const QString &imageId, int level);
    void viewerImageCacheChanged(int index);
    void viewerReset();

private:
    void syncFromPersistentSelection();
    void syncPathsFromPersistentSelection(const QStringList &paths);
    void requestMissingImageInfo();
    ImageFile *applyImageInfo(const ImageInfo &info);
    void onImageInfoAvailable(const ImageInfo &info);
    void onImagesInfoAvailable(const QList<ImageInfo> &results);
    void onImageAvailable(const ImageDecodeRequest &request, const QImage &image,
                          const DecodedImageInfo &decodedInfo);
    void emitSelectionDataChanged();
    int indexForPath(const QString &path) const;

    FileListModel *_selectionSourceModel;
    DecodeManager *_decodeManager;
    QSharedPointer<ProviderImageStore> _providerImageStore;
    ViewerImageCache _viewerImageCache;
    QList<ImageFile *> _items;
    QHash<QString, ImageFile *> _pathToItem;
    QHash<QString, ImageFile *> _imageIdToItem;
    QHash<QString, QDateTime> _selectionAddedAt;
    QTimer _imageInfoRequestTimer;
    QSet<int> _selectionPreviewSnapshot;
    QSet<QString> _activeGroupPaths;
    bool _selectionPreviewActive = false;
    int _lastImageId = 0;
    int _totalPathCount = 0;
    int _unavailableCount = 0;
    QString _activeGroupId;
    QString _currentViewerPath;
};

#endif // SELECTEDIMAGESMODEL_H
