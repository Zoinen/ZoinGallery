#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>
#include <QAbstractProxyModel>
#include <QSet>

#include "CacheUsageMode.h"
#include "ImageFile.h"
#include "PersistentSelectionCache.h"
#include "ViewerImageCache.h"

class DecodeManager;
class FileListModel;

class ThumbnailsRequestInterface {
public:
    virtual void decodeImages(const QList<ImageDecodeRequest> &requests) = 0;
    virtual void cancelAllRunners() = 0;
    virtual void cancelAllDecodeRunners() = 0;

    virtual ImageFile *rootItem() const { return nullptr; }
};

class RootProxyModel : public QAbstractProxyModel, public ThumbnailsRequestInterface {
    Q_OBJECT

public:
    explicit RootProxyModel(QObject *parent = nullptr);

    virtual void setRoot(ImageFile *root);
    void setSourceModel(QAbstractItemModel *sourceModel) override;

    QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent) const override;

    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    FileListModel *sourceModel() const;

    // ThumbnailsRequestInterface interface
    ImageFile *rootItem() const override;
    void decodeImages(const QList<ImageDecodeRequest> &requests) override;

    void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;

    void resetModel();

private:
    ImageFile *_sourceRoot;
};


class FileListModel : public QAbstractItemModel, public ThumbnailsRequestInterface {
    Q_OBJECT
    Q_PROPERTY(bool runningTasksDebug READ runningTasksDebug WRITE setRunningTasksDebug NOTIFY runningTasksDebugChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int imageCacheMode READ imageCacheMode WRITE setImageCacheMode NOTIFY imageCacheModeChanged)
    Q_PROPERTY(int fileListCacheMode READ fileListCacheMode WRITE setFileListCacheMode NOTIFY fileListCacheModeChanged)
    Q_PROPERTY(qint64 imageCacheSize READ imageCacheSize NOTIFY cacheInfoChanged)
    Q_PROPERTY(QString imageCacheLocation READ imageCacheLocation CONSTANT)
    Q_PROPERTY(qint64 fileListCacheSize READ fileListCacheSize NOTIFY cacheInfoChanged)
    Q_PROPERTY(QString fileListCacheLocation READ fileListCacheLocation CONSTANT)
    Q_PROPERTY(QVariantList selectionGroups READ selectionGroups NOTIFY selectionGroupsChanged)
    Q_PROPERTY(QString activeSelectionGroupId READ activeSelectionGroupId NOTIFY activeSelectionGroupChanged)
    Q_PROPERTY(QString activeSelectionGroupName READ activeSelectionGroupName NOTIFY activeSelectionGroupChanged)
    Q_PROPERTY(QColor activeSelectionGroupColor READ activeSelectionGroupColor NOTIFY activeSelectionGroupChanged)
    Q_PROPERTY(int totalSelectedCount READ totalSelectedCount NOTIFY selectionGroupsChanged)
    Q_PROPERTY(bool canAddSelectionGroup READ canAddSelectionGroup NOTIFY selectionGroupsChanged)

public:
    enum ItemUserRoles {
        IsImageRole = Qt::UserRole + 100,
        ImageIdUrlRole,
        FolderRole,
        ImageFullSizeRole,
        ImageFileRole,
        FolderViewRole,
        ExifRole,
        TimeToFlushRole,
        SelectedRole,
        SelectionGroupIdRole,
        SelectionGroupColorRole,
        LastModifiedRole,
        FileSizeRole
    };

    FileListModel(QSharedPointer<ProviderImageStore> providerImageStore,
                  QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int columnCount(const QModelIndex &parent) const override;

    void prepareToClose();

    int cd(const QString &path, const QString &itemToSelect = QString());
    QString rootPath() const;

    const ImageFile *itemForImageId(const QString &imageId);

    // ThumbnailsRequestInterface
    void decodeImages(const QList<ImageDecodeRequest> &requests) override;

    Q_INVOKABLE void cancelAllRunners() override;
    void cancelAllDecodeRunners() override;
    Q_INVOKABLE void cancelAllDecodeViewerRunners();

    static bool isImage(const QString &fileName);

    Q_INVOKABLE void openImageDirectly(const QString &path, int width = -1, int height = -1);
    QString directOpenPath() const;
    Q_INVOKABLE void requestViewer(int index, int width = -1, int height = -1); // -1 means full size
    Q_INVOKABLE QString bestViewerImageUrlForIndex(int index) const;
    QImage viewerForImageId(const QString &imageId);
    QImage fullSizeViewerForImageId(const QString &imageId);

    int fileIndex(const QString &fileName) const;

    static ImageFile *itemFromIndex(const QModelIndex &index);
    QModelIndex indexFromItem(const ImageFile *item) const;

    Q_INVOKABLE QAbstractItemModel *folderModel(int index);

    void enterRecursiveView();

    Q_INVOKABLE void setFolderViewImageSize(int width, int height);
    Q_INVOKABLE void setFolderViewImageCount(int count);

    Q_INVOKABLE void startScanner();

    Q_INVOKABLE void dumpCurrentImage();

    int selectedCount() const;
    Q_INVOKABLE bool isIndexSelected(int index) const;
    Q_INVOKABLE QColor selectionGroupColorForIndex(int index) const;
    Q_INVOKABLE void toggleSelection(int index);
    Q_INVOKABLE void setSelection(int index, bool selected);
    Q_INVOKABLE void setPathSelection(const QString &path, bool selected);
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE void setAllSelection(bool selected);
    Q_INVOKABLE void setSameKindSelection(int index, bool selected);
    Q_INVOKABLE QVariantList dragIndexesForIndex(int index, bool singleItemOnly = false) const;
    Q_INVOKABLE QVariantList dragUrlsForIndex(int index, bool singleItemOnly = false) const;
    Q_INVOKABLE QVariantMap dragPreviewItemsForIndex(int index, int limit, bool singleItemOnly = false) const;
    Q_INVOKABLE void beginSelectionPreview();
    Q_INVOKABLE void previewSelectionRange(int anchorIndex, int targetIndex, bool selected, bool includeTarget);
    Q_INVOKABLE void previewSelectionIndexes(const QVariantList &indexes, int mode);
    Q_INVOKABLE void commitSelectionPreview(const QString &description);
    Q_INVOKABLE void cancelSelectionPreview();
    Q_INVOKABLE QVariantList selectionHistoryForIndex(int index) const;
    Q_INVOKABLE int selectionHistoryIndexForIndex(int index) const;
    Q_INVOKABLE QString selectionContainerForIndex(int index) const;
    Q_INVOKABLE void selectionHistoryBack(int index);
    Q_INVOKABLE void selectionHistoryForward(int index);
    Q_INVOKABLE void jumpSelectionHistory(int index, int historyIndex);

    QVariantList selectionGroups() const;
    QString activeSelectionGroupId() const;
    QString activeSelectionGroupName() const;
    QColor activeSelectionGroupColor() const;
    int totalSelectedCount() const;
    bool canAddSelectionGroup() const;
    Q_INVOKABLE QString addSelectionGroup();
    Q_INVOKABLE void activateSelectionGroup(const QString &groupId);
    Q_INVOKABLE bool renameSelectionGroup(const QString &groupId, const QString &name);
    Q_INVOKABLE bool removeSelectionGroup(const QString &groupId);
    Q_INVOKABLE int copyActiveSelectionGroupPaths() const;

    bool runningTasksDebug() const;
    void setRunningTasksDebug(bool isRunningTasksDebug);

    int imageCacheMode() const;
    void setImageCacheMode(int mode);
    int fileListCacheMode() const;
    void setFileListCacheMode(int mode);
    bool imageSourceAccessEnabled() const;
    bool fileListSourceAccessEnabled() const;
    qint64 imageCacheSize() const;
    QString imageCacheLocation() const;
    qint64 fileListCacheSize() const;
    QString fileListCacheLocation() const;
    Q_INVOKABLE void clearImageCache();
    Q_INVOKABLE void clearFileListCache();
    Q_INVOKABLE void refreshCacheInfo();

signals:
    void viewerImageIdUrlChanged(const QString &imageId, int level); // 0 is thumbnail, 1 is viewer, 2 is full resolution
    void viewerImageCacheChanged(int index);
    void viewerReset();
    void directOpenReady(int index);
    void directOpenPathChanged();

    void runningTasksChanged(const QString &tasks, const QStringList &tasksInfo);
    void runningTasksDebugChanged();
    void selectionChanged();
    void selectionHistoryChanged();
    void selectionGroupsChanged();
    void activeSelectionGroupChanged();
    void imageCacheModeChanged();
    void fileListCacheModeChanged();
    void cacheInfoChanged();
    void panelReloaded(int sourceIndex);

private:
    enum SelectionPreviewMode {
        SelectionPreviewSelect = 0,
        SelectionPreviewDeselect = 1,
        SelectionPreviewReplace = 2,
        SelectionPreviewToggle = 3
    };

    enum class DirectOpenStage {
        None,
        WaitingInfo,
        WaitingFitDecode,
        WaitingFullDecode,
        WaitingNeighborInfo,
        WaitingNeighborDecode
    };

    struct DirectOpenState {
        int generation = 0;
        DirectOpenStage stage = DirectOpenStage::None;
        QString path;
        QString folderPath;
        QString fileName;
        QSize viewerSize;
        int currentIndex = -1;
        bool sameFolder = false;
        ImageInfo info;
        QSet<QString> pendingNeighborInfoPaths;
        QSet<QString> pendingNeighborDecodePaths;
    };

    QString generateNewId();
    void updateImageId(ImageFile *item);
    ImageFile *createFileItem(const QString &folderPath, const QString &fileName,
                              const QDateTime &lastModified = QDateTime(), qint64 fileSize = -1);
    void cleanupModelBeforeCd();
    void clearModelData(bool clearViewerData);
    int populateFolderItems(const QString &path, const QString &itemToSelect = QString());
    bool folderEntries(const QString &path, QList<FileInfo> &entries);
    QList<FileInfo> readFolderEntries(const QString &path) const;
    QString itemNameToPreserve() const;
    void reloadPanelForCacheModeChange();
    void startRegularFolderWork();
    ImageDecodeRequest imageDecodeRequestFromEmbeddedImageInfo(const ImageInfo &info) const;
    void handleDirectOpenImageInfo(const ImageInfo &result);
    bool handleDirectOpenImageReady(const ImageDecodeRequest &request, const QImage &image,
                                    const DecodedImageInfo &decodedInfo);
    void requestDirectOpenFitDecode();
    void requestDirectOpenFullSizeDecode();
    void populateFolderAfterDirectOpenFullDecode();
    void requestDirectOpenNeighbors();
    void requestDirectOpenNeighborDecodes();
    void finishDirectOpenPriorityWork();
    QList<int> directOpenNeighborIndexes() const;
    QList<ImageDecodeRequest> directOpenViewerRequestsForIndexes(const QList<int> &indexes, QSet<QString> *queuedPaths);
    void emitViewerImagesForCurrentIndex();
    bool isActiveDirectOpenInfo(const ImageInfo &info) const;
    bool isActiveDirectOpenRequest(const ImageDecodeRequest &request) const;
    QString selectionContainerForItem(const ImageFile *item) const;
    QString selectionItemKey(const ImageFile *item) const;
    QString selectionGroupForItem(const ImageFile *item) const;
    void ensureSelectionStateLoaded(const QString &containerKey);
    void loadSelectionStatesForVisibleItems();
    void syncVisibleItemSelection();
    void emitSelectionDataChanged(int firstIndex = -1, int lastIndex = -1);
    void pushSelectionHistory(const QString &containerKey, const QString &description,
                              const QHash<QString, QString> &previousSelectedGroups);
    void mutateSelectionForIndexes(const QList<int> &indexes, bool selected);
    bool setSelectionInState(int index, bool selected);
    void applySelectionHistoryState(const QString &containerKey, int historyIndex);
    QString sameKindDescription(int index, bool selected) const;

    QString _root;
    QHash<QString, ImageFile *> _fileToItem;
    QHash<QString, ImageFile *> _imageIdToItem;
    QList<ImageFile *> _items;
    QStringList _imagePaths;
    QStringList _folderImagePaths;
    int _lastRequestIndex;
    int _lastId;

    DecodeManager *_decodeManager;

    // Viewer
    QSharedPointer<ProviderImageStore> _providerImageStore;
    ViewerImageCache _viewerImageCache;
    QSet<QString> _requestedViewerImages;
    int _currentViewIndex;

    QHash<int, RootProxyModel *> _folderModels;
    QSize _folderViewImageSize;
    int _folderViewImageCount = 16;

    DirectOpenState _directOpen;
    QHash<QString, PersistentSelectionCache::ContainerState> _selectionStates;
    bool _selectionPreviewActive = false;
    QHash<QString, QHash<QString, QString>> _selectionPreviewSnapshot;
    CacheUsageMode _imageCacheMode = CacheUsageMode::On;
    CacheUsageMode _fileListCacheMode = CacheUsageMode::On;
    qint64 _imageCacheSize = 0;
    qint64 _fileListCacheSize = 0;
    bool _isClosing = false;
};

#endif // FILELISTMODEL_H
