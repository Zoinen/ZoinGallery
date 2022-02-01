#include "QmlImageProvider.h"

#include "FileListModel.h"

#include <QPainter>

QmlImageProvider::QmlImageProvider(const QString &prefix, FileListModel *model)
    : QQuickImageProvider(QQuickImageProvider::Image),
    _prefix(prefix),
    _model(model) {
}

QImage QmlImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QStandardItem *item = _model->itemForImageId(id);
    if (item) {
        QImage img = item->data(FileListModel::ImageRole).value<QImage>();
        if (!img.isNull()) {
            return img;
        }
    }
//    for (int i = 0; i < _model->invisibleRootItem()->rowCount(); i++) {
//        QStandardItem *item = _model->invisibleRootItem()->child(i);
//        if (item->text() == id) {
//            ThumbnailLoader loader;
//            QImage img = loader.load(_model->rootPath() + "/" + id);
//            if (!img.isNull()) {
//                return img;
//            }
//            break;
//        }
//    }

    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    return empty;
}
