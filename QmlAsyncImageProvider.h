#ifndef QMLASYNCIMAGEPROVIDER_H
#define QMLASYNCIMAGEPROVIDER_H

#include <QQuickImageProvider>
#include <QThreadPool>

class FileListModel;


class AsyncImageResponseRunnable : public QObject, public QRunnable {
    Q_OBJECT

public:
    AsyncImageResponseRunnable(const QString &id, const QSize &requestedSize);
    void run() override;

signals:
    void done(const QImage &image);

private:
    QString _id;
    QSize _requestedSize;
};


class AsyncImageResponse : public QQuickImageResponse {
public:
    AsyncImageResponse(const QString &id, const QSize &requestedSize);
    void handleDone(const QImage &image);
    QQuickTextureFactory *textureFactory() const override;

private:
    QImage _image;
};


class QmlAsyncImageProvider : public QQuickAsyncImageProvider {
public:
    QmlAsyncImageProvider(const QString &prefix, FileListModel *model);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

#endif // QMLASYNCIMAGEPROVIDER_H
