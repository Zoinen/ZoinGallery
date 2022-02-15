#ifndef STANDARDFILELISTMODEL_H
#define STANDARDFILELISTMODEL_H

#include <QStandardItemModel>
#include <QHash>

class ThreadedThumbnailGenerator;

class StandardFileListModel : public QStandardItemModel {
    Q_OBJECT
    Q_PROPERTY(bool generationFinished MEMBER _generationFinished NOTIFY generationFinishedChanged)

public:
    enum ItemUserRoles {
        ImageRole = Qt::UserRole + 100,
        ImageIdRole,
        FolderRole,
        ImageResolutionRole
    };

    StandardFileListModel(QObject *parent = nullptr);
    void prepareToClose();

    void setThumbnailResolution(QSize dimensions, qreal dpr);
    void cd(QString path);
    void updateThumbnails();
    QString rootPath() const;

    QString fullPath(QString fileName);
    QStandardItem *itemForImageId(QString imageId);

    Q_INVOKABLE void setNextRequestIndex(int index);

signals:
    void generationFinishedChanged();

private:
    QString generateNewId();
    void updateImageId(QStandardItem *item);
    bool isImage(QString fileName);

    QString _root;
    QHash<QString, QStandardItem *> _fileToItem;
    QHash<QString, QStandardItem *> _imageIdToItem;
    QStringList _imagePaths;
    int _lastRequestIndex;
    int _lastId;

    ThreadedThumbnailGenerator *_generator;
    bool _generationFinished;
};

#endif // STANDARDFILELISTMODEL_H
