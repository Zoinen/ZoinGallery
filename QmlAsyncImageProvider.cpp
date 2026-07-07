#include "QmlAsyncImageProvider.h"

#include "FileListModel.h"

#include <QDebug>
#include <QMetaObject>
#include <QPainter>
#include <QThreadPool>

#include <atomic>

// TODO: Not the cleanest way...
namespace
{
FileListModel *model = nullptr;
std::atomic_bool closing = false;

QImage emptyImage()
{
    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    return empty;
}
}

QmlAsyncImageProvider::QmlAsyncImageProvider(const QString &prefix, FileListModel *model) {
    Q_UNUSED(prefix);
    ::model = model;
    closing.store(false);
}

void QmlAsyncImageProvider::prepareToClose() {
    qInfo() << "[Shutdown] QmlAsyncImageProvider::prepareToClose begin"
            << "alreadyClosing" << closing.load()
            << "hasModel" << (model != nullptr);
    closing.store(true);

    QThreadPool *pool = QThreadPool::globalInstance();
    qInfo() << "[Shutdown] QmlAsyncImageProvider::prepareToClose clearing global thread pool"
            << "activeThreads" << pool->activeThreadCount()
            << "maxThreads" << pool->maxThreadCount();
    pool->clear();
    const bool stopped = pool->waitForDone(3000);
    qInfo() << "[Shutdown] QmlAsyncImageProvider::prepareToClose waitForDone returned"
            << stopped
            << "activeThreads" << pool->activeThreadCount();
    if (!stopped) {
        qWarning() << "Async image provider did not stop all tasks before shutdown";
    }

    model = nullptr;
    qInfo() << "[Shutdown] QmlAsyncImageProvider::prepareToClose end";
}

QQuickImageResponse *QmlAsyncImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    return new AsyncImageResponse(id, requestedSize);
}

AsyncImageResponseRunnable::AsyncImageResponseRunnable(const QString &id, const QSize &requestedSize)
    : _id(id), _requestedSize(requestedSize) {
}

AsyncImageResponse::AsyncImageResponse(const QString &id, const QSize &requestedSize) {
    if (closing.load()) {
        qInfo() << "[Shutdown] AsyncImageResponse ignored request during shutdown"
                << "id" << id
                << "requestedSize" << requestedSize;
        _image = emptyImage();
        QMetaObject::invokeMethod(this, [this]() {
            emit finished();
        }, Qt::QueuedConnection);
        return;
    }

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
    if (!closing.load() && model) {
        FileListModel *fileListModel = model;
        const ImageFile *item = fileListModel->itemForImageId(_id);
        if (item) {
            QImage img = item->image();
            if (!img.isNull()) {
                emit done(img);
                return;
            }
        }
        else {
            QImage img = fileListModel->viewerForImageId(_id);
            if (!img.isNull()) {
                emit done(img);
                return;
            }
            else {
                if (_id.contains("/")) {
                    QStringList idAndSize = _id.split("/");
                    qDebug() << "idAndSize" << idAndSize;
                    QImage img = fileListModel->fullSizeViewerForImageId(idAndSize[0]);
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
                    QImage img = fileListModel->fullSizeViewerForImageId(_id);
                    if (!img.isNull()) {
                        emit done(img);
                        return;
                    }
                }
            }
        }
    }

    // *size = empty.size();
    emit done(emptyImage());
}
