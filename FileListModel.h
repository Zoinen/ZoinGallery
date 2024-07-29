#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>
#include <QAbstractProxyModel>

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
    Q_PROPERTY(int uiTargetHeight READ uiTargetHeight WRITE setUiTargetHeight NOTIFY uiTargetHeightChanged FINAL)

public:
    enum ItemUserRoles {
        ImageRole = Qt::UserRole + 100,
        ImageIdRole,
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

    Q_INVOKABLE void requestViewer(int index, int width = -1, int height = -1); // -1 means full size
    QImage viewerForImageId(const QString &imageId);

    int fileIndex(const QString &fileName) const;

    static ImageFile *itemFromIndex(const QModelIndex &index);
    QModelIndex indexFromItem(const ImageFile *item) const;

    Q_INVOKABLE QAbstractItemModel *folderModel(int index);

    void enterRecursiveView();

    int uiTargetHeight() const;
    void setUiTargetHeight(int newUiTargetHeight);

    Q_INVOKABLE void startScanner();

signals:
    void viewerImageIdChanged(const QString &imageId);

    void uiTargetHeightChanged();

    void runningTasksChanged(const QString &tasks, const QStringList &tasksInfo);

private:
    QString generateNewId();
    void updateImageId(ImageFile *item);
    ImageFile *createFileItem(const QString &folderPath, const QString &fileName, const QDateTime &lastModified = QDateTime());
    void cleanupModelBeforeCd();

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
        bool isFromCache = false;
    };
    QHash<QString, ViewerImage> _viewerImages;
    QHash<QString, QString> _imageIdToViewer;
    QSet<QString> _requestedViewerImages;
    int _currentViewIndex;

    QHash<int, RootProxyModel *> _folderModels;
    int _uiTargetHeight;
};

#endif // FILELISTMODEL_H
