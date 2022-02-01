#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>

class BatchThumbnailGenerator;

class FileListModel : public QStandardItemModel {
    Q_OBJECT

public:
    enum ItemUserRoles {
        ImageRole = Qt::UserRole + 100,
        ImageIdRole
    };

    FileListModel(QObject *parent = nullptr);

    void cd(QString path);
    QString rootPath() const;

    QString fullPath(QString fileName);
    QStandardItem *itemForImageId(QString imageId);

private:
    QString generateNewId();
    void updateImageId(QStandardItem *item);

    QString _root;
    QHash<QString, QStandardItem *> _fileToItem;
    QHash<QString, QStandardItem *> _imageIdToItem;
    int _lastId;

    BatchThumbnailGenerator *_generator;
};

#endif // FILELISTMODEL_H
