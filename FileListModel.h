#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>

#include "ImageFile.h"

class ThreadedThumbnailGenerator;

class FileListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool generationFinished MEMBER _generationFinished NOTIFY generationFinishedChanged)

public:
    enum ItemUserRoles {
        ImageRole = Qt::UserRole + 100,
        ImageIdRole,
        FolderRole,
        ImageFullSizeRole,
        ImageFileRole
    };

    FileListModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void prepareToClose();

    int cd(QString path, QString itemToSelect = QString());
    void requestThumbnails(QSize preferredSize);
    QString rootPath() const;

    QString fullPath(QString fileName);
    const ImageFile *itemForImageId(QString imageId);

    void addRequestThumbnails(QList<ImageReadRequest> requests);

    static bool isImage(QString fileName);

    Q_INVOKABLE void requestViewer(int index, int width, int height);
    QImage viewerForImageId(QString imageId);
    Q_INVOKABLE void invalidateViewerImages();

    int fileIndex(QString fileName) const;

signals:
    void generationFinishedChanged();
    void thumbnailReadFinished();
    void viewerImageIdChanged(QString imageId);

private:
    QString generateNewId();
    void updateImageId(ImageFile *item);

    QString _root;
    QHash<QString, ImageFile *> _fileToItem;
    QHash<QString, ImageFile *> _imageIdToItem;
    QList<ImageFile *> _items;
    QStringList _imagePaths;
    int _lastRequestIndex;
    int _lastId;

    ThreadedThumbnailGenerator *_generator;
    bool _generationFinished;

    // Viewer
    struct ViewerImage {
        QImage image;
        QString imageId;
        int requestIndex;
    };
    QHash<QString, ViewerImage> _viewerImages;
    QHash<QString, QString> _imageIdToViewer;
    QSet<QString> _requestedViewerImages;
    int _currentViewIndex;
};

#endif // FILELISTMODEL_H
