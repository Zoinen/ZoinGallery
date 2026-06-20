#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>
#include <QAbstractProxyModel>
#include <QSet>

#include "ImageFile.h"

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

public:
    enum ItemUserRoles {
        IsImageRole = Qt::UserRole + 100,
        ImageIdUrlRole,
        FolderRole,
        ImageFullSizeRole,
        ImageFileRole,
        FolderViewRole,
        ExifRole,
        TimeToFlushRole
    };

    FileListModel(QObject *parent = nullptr);

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
    Q_INVOKABLE void requestViewer(int index, int width = -1, int height = -1); // -1 means full size
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

    bool runningTasksDebug() const;
    void setRunningTasksDebug(bool isRunningTasksDebug);

signals:
    void viewerImageIdUrlChanged(const QString &imageId, int level); // 0 is thumbnail, 1 is viewer, 2 is full resolution
    void viewerReset();
    void directOpenReady(int index);

    void runningTasksChanged(const QString &tasks, const QStringList &tasksInfo);
    void runningTasksDebugChanged();

private:
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
    ImageFile *createFileItem(const QString &folderPath, const QString &fileName, const QDateTime &lastModified = QDateTime());
    void cleanupModelBeforeCd();
    void clearModelData(bool clearViewerData);
    int populateFolderItems(const QString &path, const QString &itemToSelect = QString());
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
    struct ViewerImage {
        QImage image;
        QString imageId;
        QSize requestedSize;
        DecodedImageInfo decodedInfo;
    };
    QHash<QString, ViewerImage> _viewerImages;
    QHash<QString, ViewerImage> _fullSizeViewerImages;
    QHash<QString, QString> _imageIdToViewer;
    QHash<QString, QString> _imageIdToFullSizeViewer;
    QSet<QString> _requestedViewerImages;
    int _currentViewIndex;

    QHash<int, RootProxyModel *> _folderModels;
    QSize _folderViewImageSize;
    int _folderViewImageCount = 16;

    DirectOpenState _directOpen;
};

#endif // FILELISTMODEL_H
