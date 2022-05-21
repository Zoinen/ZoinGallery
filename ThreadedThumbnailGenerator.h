#ifndef THREADEDTHUMBNAILGENERATOR_H
#define THREADEDTHUMBNAILGENERATOR_H

#include "ImageFile.h"

#include <QObject>
#include <QImage>
#include <QRunnable>
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QQueue>
#include <QSet>

class QThread;
class ThreadedThumbnailGenerator;


class ReadWorker : public QObject {
    Q_OBJECT
public:
   ReadWorker(ThreadedThumbnailGenerator *generator);
   void readThumbnail(ThumbnailReadRequest request, int queueId);

signals:
   void readResultReady(const ThumbnailReadResult &result);

private:
    ThreadedThumbnailGenerator *_generator;
};


class DecodeWorker : public QObject  {
     Q_OBJECT
public:
    DecodeWorker(ThreadedThumbnailGenerator *generator);
    void decodeThumbnail(const ThumbnailReadResult &readResult, int queueId);

signals:
    void decodeResultReady(const QString &path, const QImage &image);

private:
     ThreadedThumbnailGenerator *_generator;
};


class ThreadedThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadedThumbnailGenerator(QObject *parent = nullptr);
    void prepareToClose();

    void setThumbnailReadQueue(QList<ThumbnailReadRequest> requests);
    void requestDecode(QList<ThumbnailReadRequest> requests);
    void setNextRequestImage(QString path, bool isForward);

    QAtomicInt _queueId;

signals:
    void thumbnailReady(QString path, QImage thumbnail);
    void thumbnailInfoReady(QString path, QSize fullSize);
    void requestReadThumbnail(ThumbnailReadRequest request, int queueId);
    void requestDecodeThumbnail(ThumbnailReadResult request, int queueId);
    void readFinished();
    void decodeFinished();

private:
    struct WorkerInfo {
        QThread *thread;
        DecodeWorker *worker;
        bool isFinished;
    };

    void onThumbnailDecodeFinished(const QString &path, const QImage &image);
    void onThumbnailReadFinished(const ThumbnailReadResult &result);
    bool requestNextThumbnailRead();
    bool requestNextThumbnailDecode(WorkerInfo &worker);
    void checkIfFinished();

    int nextPathIndex();
    int prevPathIndex();

    QList<ThumbnailReadRequest> _requests;
    QThread *_loaderThread;
    ReadWorker *_loaderWorker;
    QHash<QString, ThumbnailReadResult> _thumbnailReadSet;
    QQueue<ThumbnailReadResult> _thumbnailDecodeQueue;
    int _lastRequestIndex;
    int _lastRequestIndexIncrement;
    bool _readFinished;

    QList<WorkerInfo> _workers;

    QElapsedTimer _benchmark;
};

#endif // THREADEDTHUMBNAILGENERATOR_H
