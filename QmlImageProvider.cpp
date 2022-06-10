#include "QmlImageProvider.h"

#include "FileListModel.h"

#include <QPainter>

QmlImageProvider::QmlImageProvider(const QString &prefix, FileListModel *model)
    : QQuickImageProvider(QQuickImageProvider::Image),
    _prefix(prefix),
    _model(model) {
}

QImage QmlImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
//    qDebug() << "requesting image" << id;
    if (_model) {
        const ImageFile *item = _model->itemForImageId(id);
        if (item) {
            QImage img = item->image;
            if (!img.isNull()) {
                return img;
            }
        }
        else {
            QImage img = _model->viewerForImageId(id);
            if (!img.isNull()) {
                return img;
            }
        }
    }

    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    *size = empty.size();
    return empty;
}
