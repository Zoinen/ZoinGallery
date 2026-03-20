#include "QmlAsyncImageProvider.h"

#include "FileListModel.h"

#include <QPainter>

// TODO: Not the cleanest way...
FileListModel *_model;

QmlAsyncImageProvider::QmlAsyncImageProvider(const QString &prefix, FileListModel *model) {
    _model = model;
}

QQuickImageResponse *QmlAsyncImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    return new AsyncImageResponse(id, requestedSize);
}

AsyncImageResponseRunnable::AsyncImageResponseRunnable(const QString &id, const QSize &requestedSize)
    : _id(id), _requestedSize(requestedSize) {
}

AsyncImageResponse::AsyncImageResponse(const QString &id, const QSize &requestedSize) {
    auto runnable = new AsyncImageResponseRunnable(id, requestedSize);
    connect(runnable, &AsyncImageResponseRunnable::done, this, &AsyncImageResponse::handleDone);
    QThreadPool::globalInstance()->start(runnable);
}

void AsyncImageResponse::handleDone(const QImage &image) {
    _image = image;
    emit finished();
}

QQuickTextureFactory *AsyncImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(_image);
}


void AsyncImageResponseRunnable::run() {
    if (_model) {
        const ImageFile *item = _model->itemForImageId(_id);
        if (item) {
            QImage img = item->image();
            if (!img.isNull()) {
                emit done(img);
                return;
            }
        }
        else {
            QImage img = _model->viewerForImageId(_id);
            if (!img.isNull()) {
                emit done(img);
                return;
            }
            else {
                if (_id.contains("/")) {
                    QStringList idAndSize = _id.split("/");
                    qDebug() << "idAndSize" << idAndSize;
                    QImage img = _model->fullSizeViewerForImageId(idAndSize[0]);
                    QStringList sizeStrings = idAndSize[1].split(",");
                    if (sizeStrings.size() == 4) {
                        if (!img.isNull()) {
                            emit done(img.copy(sizeStrings[0].toInt(), sizeStrings[1].toInt(),
                                               sizeStrings[2].toInt(), sizeStrings[3].toInt()));
                            return;
                        }
                    }
                }
                else {
                    QImage img = _model->fullSizeViewerForImageId(_id);
                    if (!img.isNull()) {
                        emit done(img);
                        return;
                    }
                }
            }
        }
    }

    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    // *size = empty.size();
    emit done(empty);
}
