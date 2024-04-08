#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>
#include <QAbstractProxyModel>

#include "ImageFile.h"

class ThreadedThumbnailGenerator;
class FileListModel;
class ThumbnailCache;

class ThumbnailsRequestInterface {
public:
    ThumbnailsRequestInterface();
    virtual void requestThumbnails(QStringList files, QSize preferredSize) = 0;
    virtual void requestThumbnails(QSize preferredSize, bool reset = false, int imageCount = 0) = 0;
    virtual void addRequestThumbnails(QList<ImageReadRequest> requests) = 0;
    virtual ImageFile *rootItem() const { return nullptr; }

    virtual void requestRender();
    void renderRequestComplete();
    bool isRenderRequested() const;

protected:
    bool _renderQueued;
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
    void requestThumbnails(QStringList files, QSize preferredSize) override {}
    void requestThumbnails(QSize preferredSize, bool reset = false, int imageCount = 0) override;
    void addRequestThumbnails(QList<ImageReadRequest> requests) override;
    ImageFile *rootItem() const override;

    void resetModel();

signals:
    void thumbnailReadFinished(ImageFile *root);

private:
    ImageFile *_sourceRoot;
};


class FileListModel : public QAbstractItemModel, public ThumbnailsRequestInterface {
    Q_OBJECT
    Q_PROPERTY(bool generationFinished MEMBER _generationFinished NOTIFY generationFinishedChanged)

public:
    enum ItemUserRoles {
        ImageRole = Qt::UserRole + 100,
        ImageIdRole,
        FolderRole,
        ImageFullSizeRole,
        ImageFileRole,
        FolderViewRole
    };

    FileListModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    QModelIndex index(int row, int column = 0, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int columnCount(const QModelIndex &parent) const override;

    void prepareToClose();

    int cd(QString path, QString itemToSelect = QString());
    QString rootPath() const;

    const ImageFile *itemForImageId(QString imageId);

    // ThumbnailsRequestInterface
    void requestThumbnails(QStringList files, QSize preferredSize) override;
    void requestThumbnails(QSize preferredSize, bool reset = false, int imageCount = 0) override;
    void addRequestThumbnails(QList<ImageReadRequest> requests) override;

    Q_INVOKABLE void requestRender() override;

    static bool isImage(QString fileName);

    Q_INVOKABLE void requestViewer(int index, int width, int height);
    QImage viewerForImageId(QString imageId);
    Q_INVOKABLE void invalidateViewerImages();

    int fileIndex(QString fileName) const;

    static ImageFile *itemFromIndex(const QModelIndex &index);
    QModelIndex indexFromItem(const ImageFile *item) const;

    Q_INVOKABLE QAbstractItemModel *folderModel(int index);

signals:
    void generationFinishedChanged();
    void thumbnailReadFinished(ImageFile *root);
    void viewerImageIdChanged(QString imageId);
    void addToCache(const QString &path, const QDateTime &lastModified, const QImage &thumbnail);
    void requestThumbnailFromCache(const QString &path, const QDateTime &lastModified);

private:
    QString generateNewId();
    void updateImageId(ImageFile *item);
    ImageFile *createFileItem(const QString &folderPath, const QString &fileName, const QDateTime &lastModified = QDateTime());

    QString _root;
    QHash<QString, ImageFile *> _fileToItem;
    QHash<QString, ImageFile *> _imageIdToItem;
    QList<ImageFile *> _items;
    QStringList _imagePaths;
    QStringList _folderImagePaths;
    int _lastRequestIndex;
    int _lastId;

    ThumbnailCache *_thumbnailCache;
    ThreadedThumbnailGenerator *_generator;
    bool _generationFinished;

    // Viewer
    struct ViewerImage {
        QImage image;
        QString imageId;
        int requestIndex;
        QSize requestedSize;
    };
    QHash<QString, ViewerImage> _viewerImages;
    QHash<QString, QString> _imageIdToViewer;
    QSet<QString> _requestedViewerImages;
    int _currentViewIndex;

    QHash<int, RootProxyModel *> _folderModels;
};

#endif // FILELISTMODEL_H
