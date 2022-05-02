#include "QmlImageProvider.h"

#include "FileListModel.h"
#include "StandardFileListModel.h"

#include <QPainter>

QmlImageProvider::QmlImageProvider(const QString &prefix, FileListModel *model)
    : QQuickImageProvider(QQuickImageProvider::Image),
    _prefix(prefix),
    _model(model) {
    _standardModel = nullptr;
}

QmlImageProvider::QmlImageProvider(const QString &prefix, StandardFileListModel *model)
    : QQuickImageProvider(QQuickImageProvider::Image),
    _prefix(prefix),
    _standardModel(model) {
    _model = nullptr;
}

QImage QmlImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
//    qDebug() << "requesting image" << id;
    if (_model) {
        const FileListModel::ImageFile *item = _model->itemForImageId(id);
        if (item) {
            QImage img = item->image;
            if (!img.isNull()) {
                return img;
            }
        }
    }
    else if (_standardModel) {
        QStandardItem *item = _standardModel->itemForImageId(id);
        if (item) {
            QImage img = item->data(StandardFileListModel::ImageRole).value<QImage>();
            if (!img.isNull()) {
                *size = img.size();
                return img;
            }
        }
    }

    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    *size = empty.size();
    return empty;
}
