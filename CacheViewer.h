#ifndef CACHEVIEWER_H
#define CACHEVIEWER_H

#include <QAbstractListModel>
#include <QImage>
#include <QTemporaryFile>
#include <QUrl>
#include <QPointer>
#include "PersistentImageCache.h"

class ImageInfoModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum ImageRoles {
        PathRole = Qt::UserRole + 1,
        LastModifiedRole,
        ThumbnailSizeRole,
        ImageSizeRole
    };

    ImageInfoModel(QObject *parent = nullptr);
    ~ImageInfoModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setFilter(const QString &filter);
    Q_INVOKABLE QUrl retrieveImage(int index);

private:
    void applyFilter();

    QList<QString> m_imagePaths;
    QList<QString> m_filteredPaths;
    PersistentImageCache m_cache;
    QString m_filter;
    QList<QPointer<QTemporaryFile>> m_tempFiles;
};

#endif // CACHEVIEWER_H
