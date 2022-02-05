#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>

class BatchThumbnailGenerator;
class ThreadedThumbnailGenerator;
class ThreadPoolThumbnailGenerator;

class FileListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum ItemUserRoles {
        ImageRole = Qt::UserRole + 100,
        ImageIdRole
    };

    struct MyItem {
        QString text;
        QImage image;
        QString imageId;
    };

    FileListModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void cd(QString path);
    QString rootPath() const;

    QString fullPath(QString fileName);
    const MyItem *itemForImageId(QString imageId);

private:
    QString generateNewId();
    void updateImageId(MyItem *item);

    QString _root;

    QHash<QString, MyItem *> _fileToItem;
    QHash<QString, MyItem *> _imageIdToItem;
    QList<MyItem *> _items;
    int _lastId;

    BatchThumbnailGenerator *_generator;
    ThreadedThumbnailGenerator *_generator2;
    ThreadPoolThumbnailGenerator *_generator3;
};

#endif // FILELISTMODEL_H
