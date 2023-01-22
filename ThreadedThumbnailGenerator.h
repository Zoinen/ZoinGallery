#ifndef THREADEDTHUMBNAILGENERATOR_H
#define THREADEDTHUMBNAILGENERATOR_H

#include "ImageFile.h"
#include "ThreadSafeQueue.h"

#include <QObject>
#include <QImage>
#include <QRunnable>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QQueue>
#include <QSet>
#include <QThread>

class QThread;
class ThreadedThumbnailGenerator;


class ReadWorker : public QThread {
    Q_OBJECT
public:
    ReadWorker(QObject *parent);
    ThreadSafeQueue &readQueue();

signals:
   void readResultReady(const ImageReadResult &result);
   void folderListReady(const QString &path, const QStringList &result);

protected:
    void run() override;

private:
    void readImage(ImageReadRequest &request);
    ThreadSafeQueue _readQueue;
};


class DecodeWorker : public QObject  {
     Q_OBJECT
public:
    DecodeWorker(ThreadedThumbnailGenerator *generator);
    void decode(const ImageReadResult &readResult, int queueId);

signals:
    void decodeResultReady(const ImageReadResult &readResult, const QImage &image);

private:
     ThreadedThumbnailGenerator *_generator;
};


class ThreadedThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadedThumbnailGenerator(QObject *parent = nullptr);
    void prepareToClose();

    void clearRequests();
    void requestRead(QList<ImageReadRequest> requests);
    void requestThumbnailDecode(QList<ImageReadRequest> requests);
    void requestViewerDecode(ImageReadRequest request);

    QAtomicInt _queueId;

signals:
    void thumbnailReady(QString path, QImage thumbnail);
    void viewerReady(QString path, QImage thumbnail);
    void folderListReady(const QString &path, const QStringList &images);
    void thumbnailInfoReady(QString path, QSize fullSize);
    void requestDecodeThumbnail(ImageReadResult request, int queueId);
    void readFinished();
    void decodeFinished();

private:
    struct WorkerInfo {
        QThread *thread;
        DecodeWorker *worker;
        bool isFinished;
    };

    void onDecodeFinished(const ImageReadResult &readResult, const QImage &image);
    void onReadFinished(const ImageReadResult &result);
    bool requestNextDecode(WorkerInfo &worker);
    void checkIfFinished();

    QList<ImageReadRequest> _requests;
    ReadWorker *_readWorker;
    QHash<QString, ImageReadResult> _readSet;
    QQueue<ImageReadResult> _decodeQueue;
    bool _readFinished;

    QList<WorkerInfo> _workers;

    QElapsedTimer _benchmark;
};

#endif // THREADEDTHUMBNAILGENERATOR_H
