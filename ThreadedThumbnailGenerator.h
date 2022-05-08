#ifndef THREADEDTHUMBNAILGENERATOR_H
#define THREADEDTHUMBNAILGENERATOR_H

#include "ImageFile.h"

#include <QObject>
#include <QImage>
#include <QRunnable>
#include <QAtomicInt>
#include <QElapsedTimer>

class QThread;
class ThreadedThumbnailGenerator;


class Worker : public QObject  {
     Q_OBJECT
public:
    Worker(ThreadedThumbnailGenerator *generator);
    void generateThumbnail(ThumbnailRequest request, int queueId);

signals:
    void resultReady(const QString &path, const QImage &image, QSize fullSize);

private:
     ThumbnailRequest _request;
     ThreadedThumbnailGenerator *_generator;
};


class ThreadedThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadedThumbnailGenerator(QObject *parent = nullptr);
    void prepareToClose();

    void setRequestQueue(QList<ThumbnailRequest> requests);
    void addRequestQueue(QList<ThumbnailRequest> requests);
    void setNextRequestImage(QString path, bool isForward);

    QAtomicInt _queueId;

signals:
    void thumbnailReady(QString path, QImage thumbnail, QSize fullSize);
    void requestThumbnail(ThumbnailRequest request, int queueId);
    void generationFinished();

private:
    struct WorkerInfo {
        QThread *thread;
        Worker *worker;
        bool isFinished;
    };

    void onResultReady(const QString &path, const QImage &image, QSize fullSize);
    bool requestNextThumbnail(WorkerInfo &worker);

    int nextPathIndex();
    int prevPathIndex();

    QList<ThumbnailRequest> _requests;
    int _lastRequestIndex;
    int _lastRequestIndexIncrement;

    QList<WorkerInfo> _workers;
    const int MaxThreads = 1;

    QElapsedTimer _benchmark;
};

#endif // THREADEDTHUMBNAILGENERATOR_H
