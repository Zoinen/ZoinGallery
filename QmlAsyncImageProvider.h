#ifndef QMLASYNCIMAGEPROVIDER_H
#define QMLASYNCIMAGEPROVIDER_H

#include "ProviderImageStore.h"

#include <QMutex>
#include <QQuickImageProvider>
#include <QRect>
#include <QSharedPointer>
#include <QThreadPool>

#include <functional>

class AsyncImageResponseRunnable : public QObject, public QRunnable {
    Q_OBJECT

public:
    AsyncImageResponseRunnable(QImage image, QRect crop);
    void run() override;

signals:
    void done(const QImage &image);

private:
    QImage _image;
    QRect _crop;
};


class AsyncImageResponse : public QQuickImageResponse {
public:
    AsyncImageResponse(
        const QString &id, const QSize &requestedSize,
        const QSharedPointer<ProviderImageStore> &providerImageStore,
        QThreadPool *threadPool);
    void handleDone(const QImage &image);
    QQuickTextureFactory *textureFactory() const override;

private:
    QImage _image;
};


class QmlAsyncImageProvider : public QQuickAsyncImageProvider {
public:
    QmlAsyncImageProvider(
        const QString &prefix,
        QSharedPointer<ProviderImageStore> providerImageStore,
        std::function<void()> destructionCallback = {},
        int maxThreads = 0);
    ~QmlAsyncImageProvider() override;
    void shutdown();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    void stopThreadPool();

    QSharedPointer<ProviderImageStore> _providerImageStore;
    QMutex _requestMutex;
    QThreadPool _threadPool;
    bool _acceptingRequests = true;
    std::function<void()> _destructionCallback;
};

#endif // QMLASYNCIMAGEPROVIDER_H
