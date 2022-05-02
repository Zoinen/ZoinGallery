#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>

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

    struct ImageFile {
        QString text;
        QImage image;
        QString imageId;
        QSize fullSize;
        bool isFolder;
        int index;
    };

    FileListModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void prepareToClose();

    void setThumbnailResolution(QSize dimensions, qreal dpr);
    int cd(QString path, QString itemToSelect = QString());
    void updateThumbnails();
    QString rootPath() const;

    QString fullPath(QString fileName);
    const ImageFile *itemForImageId(QString imageId);

    Q_INVOKABLE void setNextRequestIndex(int index);

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

Q_DECLARE_METATYPE(FileListModel::ImageFile *)

#endif // FILELISTMODEL_H
