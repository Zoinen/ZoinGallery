#ifndef THREADEDTHUMBNAILGENERATOR_H
#define THREADEDTHUMBNAILGENERATOR_H

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
    void setThumbnailResolution(QSize dimensions, qreal dpr);
    void generateThumbnail(QString path, int queueId);

signals:
    void resultReady(const QString &path, const QImage &image, QSize fullSize);

private:
     QString _path;
     QSize _dimensions;
     qreal _dpr;
     ThreadedThumbnailGenerator *_generator;
};

class ThreadedThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadedThumbnailGenerator(QObject *parent = nullptr);
    void prepareToClose();

    void generate(QStringList paths);
    void setNextRequestImage(QString path, bool isForward);
    void setThumbnailResolution(QSize dimensions, qreal dpr);

    QAtomicInt _queueId;

signals:
    void thumbnailReady(QString path, QImage thumbnail, QSize fullSize);
    void requestThumbnail(QString path, int queueId);
    void setThumbnailResolutionSignal(QSize dimensions, qreal dpr);
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

    QStringList _paths;
    QVector<bool> _pathsRequested;
    int _lastPathIndex;
    int _lastPathIndexIncrement;

    QList<WorkerInfo> _workers;
    const int MaxThreads = 1;

    QElapsedTimer _benchmark;
};

#endif // THREADEDTHUMBNAILGENERATOR_H
