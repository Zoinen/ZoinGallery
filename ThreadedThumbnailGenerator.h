#ifndef THREADEDTHUMBNAILGENERATOR_H
#define THREADEDTHUMBNAILGENERATOR_H

#include <QObject>
#include <QImage>
#include <QRunnable>

class QThread;

class Worker : public QObject  {
     Q_OBJECT
public:
    void generateThumbnail(QString path);

signals:
    void resultReady(QString path, QImage image);

private:
     QString _path;
};

class ThreadedThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadedThumbnailGenerator(QObject *parent = nullptr);

    void generate(QStringList paths);

signals:
    void thumbnailReady(QString path, QImage thumbnail);
    void requestThumbnail(QString path);

private:
    struct WorkerInfo {
        QThread *thread;
        Worker *worker;
        QMetaObject::Connection requestConnection;
    };

    void onResultReady(QString path, QImage image);
    void requestNextThumbnail(WorkerInfo &worker);

    QStringList _paths;
    int _lastPathIndex;

    QList<WorkerInfo> _workers;
    const int MaxThreads = 10;
};

#endif // THREADEDTHUMBNAILGENERATOR_H
