#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"

#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <QTimer>
#include <QFileInfo>

ThreadedThumbnailGenerator::ThreadedThumbnailGenerator(QObject *parent)
    : QObject{parent} {
    _queueId = 0;
    for (int i = 0; i < MaxThreads; i++) {
        WorkerInfo info;
        info.thread = new QThread(this);
        info.worker = new Worker(this);
        info.isFinished = true;
        info.worker->moveToThread(info.thread);
        connect(info.worker, &Worker::resultReady,
                this, &ThreadedThumbnailGenerator::onResultReady);
        connect(this, &ThreadedThumbnailGenerator::setThumbnailResolutionSignal,
                info.worker, &Worker::setThumbnailResolution);
        _workers.append(info);

        info.thread->start();
//        info.thread->setPriority(QThread::LowPriority);
    }
}

void ThreadedThumbnailGenerator::prepareToClose() {
    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->terminate();
    }
}

void ThreadedThumbnailGenerator::generate(QStringList paths) {
    _benchmark.start();
    _paths = paths;
    _pathsRequested.clear();
    _pathsRequested.resize(_paths.size());
    for (int i = 0; i < _pathsRequested.size(); i++) {
        _pathsRequested[i] = false;
    }
    _lastPathIndex = -1;
    _lastPathIndexIncrement = 1;
    _queueId.fetchAndAddOrdered(1);
    for (int i = 0; i < _workers.size(); i++) {
        requestNextThumbnail(_workers[i]);
    }
    //    qDebug() << "FIRST QUEUE DONE ----------------------------";
}

void ThreadedThumbnailGenerator::setNextRequestImage(QString path, bool isForward) {
    for (int i = 0; i < _paths.size(); i++) {
        if (_paths[i] == path) {
            _lastPathIndex = i - 1;
            qDebug() << "next:" << path << _lastPathIndex << isForward;
            _lastPathIndexIncrement = (isForward ? 1 : -1);
            break;
        }
    }
}

void ThreadedThumbnailGenerator::setThumbnailResolution(QSize dimensions, qreal dpr) {
    emit setThumbnailResolutionSignal(dimensions, dpr);
}

bool ThreadedThumbnailGenerator::requestNextThumbnail(WorkerInfo &worker) {
//    qDebug() << "lpi" << _lastPathIndex << _lastPathIndexIncrement;
    if (_lastPathIndex == -2) {
        return false;
    }

    if (_lastPathIndex > -1) {
        if (_lastPathIndexIncrement == 1) {
            int next = nextPathIndex();
            if (next == -1) {
                int prev = prevPathIndex();
                if (prev == -1) {
                    _lastPathIndex = -2;
//                    qDebug() << "hard stop forward";
                    return false;
                }
                else {
                    _lastPathIndex = prev;
                }
            }
            else {
                _lastPathIndex = next;
            }
        }
        else {
            int prev = prevPathIndex();
            if (prev == -1) {
                int next = nextPathIndex();
                if (next == -1) {
                    _lastPathIndex = -2;
                    qDebug() << "hard stop backward";
                    return false;
                }
                else {
                    _lastPathIndex = next;
                }
            }
            else {
                _lastPathIndex = prev;
            }
        }
    }
    else {
        if (_paths.size() > 0) {
            _lastPathIndex = 0;
//            qDebug() << "zero" << _paths.size();
        }
        else {
            _lastPathIndex = -2;
            return false;
        }
    }

    if (_lastPathIndex >= 0 && _lastPathIndex < _paths.size()) {
        QMetaObject::Connection connection = connect(this, &ThreadedThumbnailGenerator::requestThumbnail,
                                           worker.worker, &Worker::generateThumbnail);
        _pathsRequested[_lastPathIndex] = true;
        worker.isFinished = false;
        emit requestThumbnail(_paths.at(_lastPathIndex), _queueId.loadAcquire());
        disconnect(connection);
    }
    else {
        return false;
    }
    return true;
}

int ThreadedThumbnailGenerator::nextPathIndex() {
    int i = _lastPathIndex;
    for (; i < _paths.size(); i++) {
        if (!_pathsRequested[i]) {
            return i;
        }
    }
    return -1;
}

int ThreadedThumbnailGenerator::prevPathIndex() {
    int i = _lastPathIndex;
    for (; i >= 0; i--) {
        if (!_pathsRequested[i]) {
            return i;
        }
    }
    return -1;
}

Worker::Worker(ThreadedThumbnailGenerator *generator) {
    _generator = generator;
    _dpr = 1;
}

void Worker::setThumbnailResolution(QSize dimensions, qreal dpr) {
    _dimensions = dimensions;
    _dpr = dpr;
}

void Worker::generateThumbnail(QString path, int queueId) {
    _path = path;

    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    ThumbnailLoader loader;
    ThumbnailLoader::ExifOrientation orientation = ThumbnailLoader::Horizontal;
    QImage thumbnail;
    if (ThumbnailLoader::isRawOrTiff(path)) {
        thumbnail = loader.loadRawOrTiff(path, &orientation);
    }
    else if (ThumbnailLoader::isJpeg(path)) {
        thumbnail = loader.loadJpeg(path, &orientation);
    }
    else {
        thumbnail = loader.loadImageOther(path, &orientation);
    }
    thumbnail = loader.createThumbnail(thumbnail, _dimensions, orientation);
    thumbnail = loader.unsharpMask(thumbnail);
    thumbnail = loader.rotateAndFlip(thumbnail, orientation);

    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    thumbnail.setDevicePixelRatio(_dpr);
    emit resultReady(path, thumbnail);

//    QImage img(200, 164, QImage::Format_RGBA8888);

//    QImage img(2000, 2000, QImage::Format_RGBA8888);
//    for (int i = 0; i < 1000; i++) {
//        img.fill(Qt::blue);
//    }
//    emit resultReady(path, img.scaled(100, 100));

//    QThread::sleep(10);
}

void ThreadedThumbnailGenerator::onResultReady(QString path, QImage image) {
//    qDebug() << "-------- on ready" << path;
    emit thumbnailReady(path, image);
    Worker *worker = static_cast<Worker*>(sender());
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].worker == worker) {
            if (!requestNextThumbnail(_workers[i])) {
                _workers[i].isFinished = true;

                bool allFinished = true;
                for (int i = 0; i < _workers.size(); i++) {
                    if (!_workers[i].isFinished) {
                        allFinished = false;
                        break;
                    }
                }
                if (allFinished) {
                    qDebug() << "generation finished";
                    qDebug() << "took:" << _benchmark.restart() << "ms";
                    emit generationFinished();
                }
            }
            break;
        }
    }
}
