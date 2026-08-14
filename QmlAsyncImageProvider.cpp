#include "QmlAsyncImageProvider.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>

#include <utility>

namespace
{
QImage emptyImage()
{
    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    return empty;
}

QRect parseCrop(const QString &cropDescription)
{
    const QStringList values = cropDescription.split(',');
    if (values.size() != 4) {
        return QRect();
    }

    bool okX = false;
    bool okY = false;
    bool okWidth = false;
    bool okHeight = false;
    const int x = values[0].toInt(&okX);
    const int y = values[1].toInt(&okY);
    const int width = values[2].toInt(&okWidth);
    const int height = values[3].toInt(&okHeight);
    return okX && okY && okWidth && okHeight && width > 0 && height > 0
        ? QRect(x, y, width, height)
        : QRect();
}
}

QmlAsyncImageProvider::QmlAsyncImageProvider(
    const QString &prefix,
    QSharedPointer<ProviderImageStore> providerImageStore,
    std::function<void()> destructionCallback,
    int maxThreads)
    : _providerImageStore(std::move(providerImageStore)),
      _destructionCallback(std::move(destructionCallback)) {
    Q_UNUSED(prefix);
    if (maxThreads > 0) {
        _threadPool.setMaxThreadCount(maxThreads);
    }
}

QmlAsyncImageProvider::~QmlAsyncImageProvider() {
    stopThreadPool();
    if (_destructionCallback) {
        std::exchange(_destructionCallback, {})();
    }
}

void QmlAsyncImageProvider::shutdown() {
    stopThreadPool();
}

QQuickImageResponse *QmlAsyncImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    QMutexLocker locker(&_requestMutex);
    return new AsyncImageResponse(
        id, requestedSize, _providerImageStore,
        _acceptingRequests ? &_threadPool : nullptr);
}

void QmlAsyncImageProvider::stopThreadPool() {
    QMutexLocker locker(&_requestMutex);
    if (!_acceptingRequests) {
        return;
    }
    _acceptingRequests = false;
    qInfo() << "[Shutdown] QmlAsyncImageProvider stopping private thread pool"
            << "activeThreads" << _threadPool.activeThreadCount()
            << "maxThreads" << _threadPool.maxThreadCount();
    const bool stopped = _threadPool.waitForDone(3000);
    if (!stopped) {
        qWarning() << "Async image provider did not stop all tasks before shutdown";
    }
}

AsyncImageResponseRunnable::AsyncImageResponseRunnable(
    QImage image, QRect crop)
    : _image(std::move(image)),
      _crop(crop) {
}

AsyncImageResponse::AsyncImageResponse(
    const QString &id, const QSize &requestedSize,
    const QSharedPointer<ProviderImageStore> &providerImageStore,
    QThreadPool *threadPool) {
    Q_UNUSED(requestedSize)
    if (!threadPool) {
        qInfo() << "[Shutdown] AsyncImageResponse ignored request during shutdown"
                << "id" << id
                << "requestedSize" << requestedSize;
        _image = emptyImage();
        QMetaObject::invokeMethod(this, [this]() {
            emit finished();
        }, Qt::QueuedConnection);
        return;
    }

    const qsizetype separator = id.indexOf('/');
    const QString imageId =
        separator == -1 ? id : id.left(separator);
    const QRect crop =
        separator == -1 ? QRect() : parseCrop(id.mid(separator + 1));
    auto runnable = new AsyncImageResponseRunnable(
        providerImageStore->snapshot(imageId), crop);
    connect(runnable, &AsyncImageResponseRunnable::done, this, &AsyncImageResponse::handleDone);
    threadPool->start(runnable);
}

void AsyncImageResponse::handleDone(const QImage &image) {
    _image = image;
    emit finished();
}

QQuickTextureFactory *AsyncImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(_image);
}


void AsyncImageResponseRunnable::run() {
    if (!_image.isNull()) {
        emit done(_crop.isValid() ? _image.copy(_crop) : _image);
        return;
    }

    emit done(emptyImage());
}
