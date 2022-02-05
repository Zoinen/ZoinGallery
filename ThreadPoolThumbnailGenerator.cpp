#include "ThreadPoolThumbnailGenerator.h"

#include "ThumbnailLoader.h"

#include <QThread>
#include <QThreadPool>
#include <QDebug>

ThreadPoolThumbnailGenerator::ThreadPoolThumbnailGenerator(QObject *parent)
    : QObject{parent} {
//    for (int i = 0; i < MaxThreads; i++) {
//        _workerThreads.append(new QThread(this));
//    }
}

void ThreadPoolThumbnailGenerator::generate(QStringList paths) {
    _paths = paths;
    qDebug() << "max threads:" << QThreadPool::globalInstance()->maxThreadCount();
    qDebug() << "hello from MAIN thread" << QThread::currentThreadId();

    for (int i = 0; i < paths.size(); i++) {
        PoolWorker *worker = new PoolWorker();
        worker->setPath(paths.at(i));
        QThreadPool::globalInstance()->start(worker, -1);
        connect(worker, &PoolWorker::resultReady,
                this, &ThreadPoolThumbnailGenerator::onResultReady);
        qDebug() << "queued" << i;
    }
    qDebug() << "-------------- queued";
//    for (int i = 0; i < MaxThreads; i++) {
//        Worker *worker = new Worker;
//        worker->moveToThread(_workerThreads[i]);
//        connect(_workerThreads[i], &QThread::finished, worker, &QObject::deleteLater);
//        connect(this, &Controller::operate, worker, &Worker::doWork);
//        connect(worker, &Worker::resultReady, this, &Controller::handleResults);
//        _workerThreads[i]->start();
    //    }
}

void ThreadPoolThumbnailGenerator::onResultReady(QString path, QImage image) {
//    static int maxCount = 5;
//    if (maxCount) {
        emit thumbnailReady(path, image);
//        maxCount--;
//    }
}

void PoolWorker::run() {
    qDebug() << "hello from thread" << QThread::currentThreadId();
//    ThumbnailLoader loader;
//    QImage img = loader.load(_path);

//    QImage img(200, 164, QImage::Format_RGBA8888);
    QImage img(20000, 20000, QImage::Format_RGBA8888);

    img.fill(Qt::blue);
//    img.scaled(100, 100);
    emit resultReady(_path, img.scaled(100, 100));
//    QThread::sleep(10);
}

void PoolWorker::setPath(const QString &path) {
    _path = path;
}
