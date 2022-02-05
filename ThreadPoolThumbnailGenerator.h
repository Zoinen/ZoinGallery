#ifndef THREADPOOLTHUMBNAILGENERATOR_H
#define THREADPOOLTHUMBNAILGENERATOR_H

#include <QObject>
#include <QImage>
#include <QRunnable>

class QThread;

class PoolWorker : public QObject, public QRunnable {
     Q_OBJECT
public:
    void run() override;

    void setPath(const QString &path);

 signals:
     void resultReady(QString path, QImage image);

private:
     QString _path;
};

class ThreadPoolThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThreadPoolThumbnailGenerator(QObject *parent = nullptr);

    void generate(QStringList paths);

signals:
    void thumbnailReady(QString path, QImage thumbnail);

private:
    void onResultReady(QString path, QImage image);

    QStringList _paths;
    int _lastPathIndex;
//    QList<QThread *> _workerThreads;
    const int MaxThreads = 32;
};

#endif // THREADPOOLTHUMBNAILGENERATOR_H
