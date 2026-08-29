#include "QmlAsyncImageProvider.h"

#include <ZoinGallery/MediaTimingTrace.h>

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

QVariantMap providerFields(const QString &providerId,
                           const QSize &requestedSize = {})
{
    return {
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("requestedWidth"), requestedSize.width()},
        {QStringLiteral("requestedHeight"), requestedSize.height()},
    };
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
    const qint64 requestStartedNs =
        ZoinGallery::MediaTimingTrace::enabled()
        ? ZoinGallery::MediaTimingTrace::monotonicNanoseconds() : 0;
    QMutexLocker locker(&_requestMutex);
    QVariantMap fields = providerFields(id, requestedSize);
    fields.insert(QStringLiteral("acceptingRequests"), _acceptingRequests);
    if (requestStartedNs != 0) {
        fields.insert(
            QStringLiteral("mutexWaitNs"),
            ZoinGallery::MediaTimingTrace::monotonicNanoseconds()
                - requestStartedNs);
    }
    ZoinGallery::MediaTimingTrace::eventAt(
        QStringLiteral("qt.gallery.qml_provider.request"),
        requestStartedNs, fields);
    return new AsyncImageResponse(
        id, requestedSize, _providerImageStore,
        _acceptingRequests ? &_threadPool : nullptr,
        requestStartedNs);
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
    QString providerId, QImage image, QRect crop)
    : _providerId(std::move(providerId)),
      _image(std::move(image)),
      _crop(crop) {
}

AsyncImageResponse::AsyncImageResponse(
    const QString &id, const QSize &requestedSize,
    const QSharedPointer<ProviderImageStore> &providerImageStore,
    QThreadPool *threadPool, qint64 requestStartedNs)
    : _providerId(id),
      _requestedSize(requestedSize),
      _requestStartedNs(requestStartedNs) {
    if (!threadPool) {
        qInfo() << "[Shutdown] AsyncImageResponse ignored request during shutdown"
                << "id" << id
                << "requestedSize" << requestedSize;
        _image = emptyImage();
        ZoinGallery::MediaTimingTrace::event(
            QStringLiteral("qt.gallery.qml_provider.finished"),
            ZoinGallery::MediaTimingTrace::mergedFields(
                providerFields(id, requestedSize), {
                    {QStringLiteral("outcome"),
                     QStringLiteral("shutdown")},
                    {QStringLiteral("null"), false},
                    {QStringLiteral("imageWidth"), _image.width()},
                    {QStringLiteral("imageHeight"), _image.height()},
                }));
        QMetaObject::invokeMethod(this, [this]() {
            emit finished();
        }, Qt::QueuedConnection);
        return;
    }

    const qsizetype separator = id.indexOf('/');
    const QString imageId =
        separator == -1 ? id : id.left(separator);
    _providerId = imageId;
    const QRect crop =
        separator == -1 ? QRect() : parseCrop(id.mid(separator + 1));
    const qint64 snapshotStartedNs =
        ZoinGallery::MediaTimingTrace::enabled()
        ? ZoinGallery::MediaTimingTrace::monotonicNanoseconds() : 0;
    QImage snapshot = providerImageStore->snapshot(imageId);
    const qint64 snapshotFinishedNs =
        ZoinGallery::MediaTimingTrace::enabled()
        ? ZoinGallery::MediaTimingTrace::monotonicNanoseconds() : 0;
    ZoinGallery::MediaTimingTrace::eventAt(
        QStringLiteral("qt.gallery.qml_provider.snapshot"),
        snapshotFinishedNs,
        ZoinGallery::MediaTimingTrace::mergedFields(
            providerFields(imageId, requestedSize), {
                {QStringLiteral("durationNs"),
                 snapshotFinishedNs - snapshotStartedNs},
                {QStringLiteral("hit"), !snapshot.isNull()},
                {QStringLiteral("imageWidth"), snapshot.width()},
                {QStringLiteral("imageHeight"), snapshot.height()},
                {QStringLiteral("imageBytes"),
                 QVariant::fromValue<qlonglong>(snapshot.sizeInBytes())},
                {QStringLiteral("crop"), crop.isValid()},
            }));
    auto runnable = new AsyncImageResponseRunnable(
        imageId, std::move(snapshot), crop);
    connect(runnable, &AsyncImageResponseRunnable::done, this, &AsyncImageResponse::handleDone);
    threadPool->start(runnable);
}

void AsyncImageResponse::handleDone(const QImage &image) {
    _image = image;
    const qint64 finishedNs =
        ZoinGallery::MediaTimingTrace::enabled()
        ? ZoinGallery::MediaTimingTrace::monotonicNanoseconds() : 0;
    ZoinGallery::MediaTimingTrace::eventAt(
        QStringLiteral("qt.gallery.qml_provider.finished"), finishedNs,
        ZoinGallery::MediaTimingTrace::mergedFields(
            providerFields(_providerId, _requestedSize), {
                {QStringLiteral("durationNs"),
                 _requestStartedNs == 0 ? 0
                                        : finishedNs - _requestStartedNs},
                {QStringLiteral("outcome"), QStringLiteral("ready")},
                {QStringLiteral("null"), _image.isNull()},
                {QStringLiteral("imageWidth"), _image.width()},
                {QStringLiteral("imageHeight"), _image.height()},
                {QStringLiteral("imageBytes"),
                 QVariant::fromValue<qlonglong>(_image.sizeInBytes())},
            }));
    emit finished();
}

QQuickTextureFactory *AsyncImageResponse::textureFactory() const {
    ZoinGallery::MediaTimingTrace::Span span(
        QStringLiteral("qt.gallery.qml_provider.texture_factory"),
        ZoinGallery::MediaTimingTrace::mergedFields(
            providerFields(_providerId, _requestedSize), {
                {QStringLiteral("null"), _image.isNull()},
                {QStringLiteral("imageWidth"), _image.width()},
                {QStringLiteral("imageHeight"), _image.height()},
            }));
    QQuickTextureFactory *factory =
        QQuickTextureFactory::textureFactoryForImage(_image);
    span.set(QStringLiteral("created"), factory != nullptr);
    return factory;
}


void AsyncImageResponseRunnable::run() {
    ZoinGallery::MediaTimingTrace::Span span(
        QStringLiteral("qt.gallery.qml_provider.worker"), {
            {QStringLiteral("providerId"), _providerId},
            {QStringLiteral("inputNull"), _image.isNull()},
            {QStringLiteral("inputWidth"), _image.width()},
            {QStringLiteral("inputHeight"), _image.height()},
            {QStringLiteral("crop"), _crop.isValid()},
        });
    if (!_image.isNull()) {
        const QImage output = _crop.isValid() ? _image.copy(_crop) : _image;
        span.set(QStringLiteral("outcome"), QStringLiteral("ready"));
        span.set(QStringLiteral("outputWidth"), output.width());
        span.set(QStringLiteral("outputHeight"), output.height());
        emit done(output);
        return;
    }

    const QImage output = emptyImage();
    span.set(QStringLiteral("outcome"), QStringLiteral("missing"));
    span.set(QStringLiteral("outputWidth"), output.width());
    span.set(QStringLiteral("outputHeight"), output.height());
    emit done(output);
}
