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
    ThreadSafeQueue<ThumbnailReadRequest> &readQueue();

signals:
   void readResultReady(const ThumbnailReadResult &result);

protected:
    void run() override;

private:
    ThreadSafeQueue<ThumbnailReadRequest> _readQueue;
};


class DecodeWorker : public QObject  {
     Q_OBJECT
public:
    DecodeWorker(ThreadedThumbnailGenerator *generator);
    void decodeThumbnail(const ThumbnailReadResult &readResult, int queueId);

signals:
    void decodeResultReady(const ThumbnailReadResult &readResult, const QImage &image);

private:
     ThreadedThumbnailGenerator *_generator;
};


class ThreadedThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadedThumbnailGenerator(QObject *parent = nullptr);
    void prepareToClose();

    void clearRequests();
    void requestRead(QList<ThumbnailReadRequest> requests);
    void requestDecode(QList<ThumbnailReadRequest> requests);

    QAtomicInt _queueId;

signals:
    void thumbnailReady(QString path, QImage thumbnail);
    void viewerReady(QString path, QImage thumbnail);
    void thumbnailInfoReady(QString path, QSize fullSize);
    void requestDecodeThumbnail(ThumbnailReadResult request, int queueId);
    void readFinished();
    void decodeFinished();

private:
    struct WorkerInfo {
        QThread *thread;
        DecodeWorker *worker;
        bool isFinished;
    };

    void onThumbnailDecodeFinished(const ThumbnailReadResult &readResult, const QImage &image);
    void onThumbnailReadFinished(const ThumbnailReadResult &result);
    bool requestNextThumbnailDecode(WorkerInfo &worker);
    void checkIfFinished();

    QList<ThumbnailReadRequest> _requests;
    ReadWorker *_readWorker;
    QHash<QString, ThumbnailReadResult> _thumbnailReadSet;
    QQueue<ThumbnailReadResult> _thumbnailDecodeQueue;
    bool _readFinished;

    QList<WorkerInfo> _workers;

    QElapsedTimer _benchmark;
};

#endif // THREADEDTHUMBNAILGENERATOR_H
