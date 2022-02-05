#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"

#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <QTimer>

ThreadedThumbnailGenerator::ThreadedThumbnailGenerator(QObject *parent)
    : QObject{parent} {
    for (int i = 0; i < MaxThreads; i++) {
        WorkerInfo info;
        info.thread = new QThread(this);
        info.worker = new Worker();
        info.worker->moveToThread(info.thread);
        connect(info.worker, &Worker::resultReady,
                this, &ThreadedThumbnailGenerator::onResultReady);
        _workers.append(info);

        info.thread->start();
        info.thread->setPriority(QThread::LowPriority);
    }
}

void ThreadedThumbnailGenerator::generate(QStringList paths) {
    _paths = paths;
    _lastPathIndex = -1;
    for (int i = 0; i < _workers.size(); i++) {
        requestNextThumbnail(_workers[i]);
    }
    qDebug() << "FIRST QUEUE DONE ----------------------------";
}

void ThreadedThumbnailGenerator::requestNextThumbnail(WorkerInfo &worker) {
    _lastPathIndex++;
    if (_lastPathIndex < _paths.size()) {
        qDebug() << "rendering" << _lastPathIndex << "of" << _paths.size();
        disconnect(worker.requestConnection);
        worker.requestConnection = connect(this, &ThreadedThumbnailGenerator::requestThumbnail,
                                           worker.worker, &Worker::generateThumbnail);
        emit requestThumbnail(_paths.at(_lastPathIndex));
    }
    else {
        qDebug() << "FINISH";
    }
}

void Worker::generateThumbnail(QString path) {
    qDebug() << "-------- hello from thread" << QThread::currentThreadId();
//    ThumbnailLoader loader;
//    QImage img = loader.load(_path);

//    QImage img(200, 164, QImage::Format_RGBA8888);
    QImage img(2000, 2000, QImage::Format_RGBA8888);
    for (int i = 0; i < 1000; i++) {
        img.fill(Qt::blue);
    }
    emit resultReady(path, img.scaled(100, 100));
//    QThread::sleep(10);
}

void ThreadedThumbnailGenerator::onResultReady(QString path, QImage image) {
    qDebug() << "-------- on ready" << path;
    emit thumbnailReady(path, image);
    Worker *worker = static_cast<Worker*>(sender());
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].worker == worker) {
            qDebug() << "-------- waiting to request";
            QTimer::singleShot(3000, this, [=] () {
                requestNextThumbnail(_workers[i]);
            });
            break;
        }
    }
}
