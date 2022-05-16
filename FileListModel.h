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

    Q_INVOKABLE void setNextRequestIndex(int index);

    void addRequestThumbnails(QList<ThumbnailReadRequest> requests);

signals:
    void generationFinishedChanged();

private:
    QString generateNewId();
    void updateImageId(ImageFile *item);
    bool isImage(QString fileName);

    QString _root;
    QHash<QString, ImageFile *> _fileToItem;
    QHash<QString, ImageFile *> _imageIdToItem;
    QList<ImageFile *> _items;
    QStringList _imagePaths;
    int _lastRequestIndex;
    int _lastId;

    ThreadedThumbnailGenerator *_generator;
    bool _generationFinished;
};

#endif // FILELISTMODEL_H
