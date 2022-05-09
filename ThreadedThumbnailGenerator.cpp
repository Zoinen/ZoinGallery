#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"
#include "ImageFile.h"

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

void ThreadedThumbnailGenerator::setRequestQueue(QList<ThumbnailRequest> requests) {
    _benchmark.start();
    _requests = requests;
    _lastRequestIndex = -1;
    _lastRequestIndexIncrement = 1;
    _queueId.fetchAndAddOrdered(1);
    for (int i = 0; i < _workers.size(); i++) {
        requestNextThumbnail(_workers[i]);
    }
    //    qDebug() << "FIRST QUEUE DONE ----------------------------";
}

template <class T>
void insertList(QList<T> &listDst, int pos, const QList<T> &listSrc) {
    listDst.insert(pos, listSrc.size(), T());
    for (int i = 0; i < listSrc.size(); i++) {
        listDst[pos + i] = listSrc[i];
    }
}

void ThreadedThumbnailGenerator::addRequestQueue(QList<ThumbnailRequest> requests) {
    if (_lastRequestIndex != -2) {
        insertList(_requests, _lastRequestIndex, requests);
    }

    int lastQueueSize = _requests.size() - 1;
    _requests.append(requests);
    if (_lastRequestIndex == -2) {
        _lastRequestIndex = lastQueueSize;
        for (int i = 0; i < _workers.size(); i++) {
            if (_workers[i].isFinished) {
                requestNextThumbnail(_workers[i]);
            }
        }
    }
}

void ThreadedThumbnailGenerator::setNextRequestImage(QString path, bool isForward) {
    for (int i = 0; i < _requests.size(); i++) {
        if (_requests[i].sourcePath == path) {
            _lastRequestIndex = i - 1;
            qDebug() << "next:" << path << _lastRequestIndex << isForward;
            _lastRequestIndexIncrement = (isForward ? 1 : -1);
            break;
        }
    }
}

bool ThreadedThumbnailGenerator::requestNextThumbnail(WorkerInfo &worker) {
//    qDebug() << "lpi" << _lastPathIndex << _lastPathIndexIncrement;
    if (_lastRequestIndex == -2) {
        return false;
    }

    if (_lastRequestIndex > -1) {
        if (_lastRequestIndexIncrement == 1) {
            int next = nextPathIndex();
            if (next == -1) {
                int prev = prevPathIndex();
                if (prev == -1) {
                    _lastRequestIndex = -2;
//                    qDebug() << "hard stop forward";
                    return false;
                }
                else {
                    _lastRequestIndex = prev;
                }
            }
            else {
                _lastRequestIndex = next;
            }
        }
        else {
            int prev = prevPathIndex();
            if (prev == -1) {
                int next = nextPathIndex();
                if (next == -1) {
                    _lastRequestIndex = -2;
                    qDebug() << "hard stop backward";
                    return false;
                }
                else {
                    _lastRequestIndex = next;
                }
            }
            else {
                _lastRequestIndex = prev;
            }
        }
    }
    else {
        if (_requests.size() > 0) {
            _lastRequestIndex = 0;
//            qDebug() << "zero" << _paths.size();
        }
        else {
            _lastRequestIndex = -2;
            return false;
        }
    }

    if (_lastRequestIndex >= 0 && _lastRequestIndex < _requests.size()) {
        QMetaObject::Connection connection = connect(this, &ThreadedThumbnailGenerator::requestThumbnail,
                                           worker.worker, &Worker::generateThumbnail);
        _requests[_lastRequestIndex].requested = true;
        worker.isFinished = false;
        emit requestThumbnail(_requests.at(_lastRequestIndex), _queueId.loadAcquire());
        disconnect(connection);
    }
    else {
        return false;
    }
    return true;
}

int ThreadedThumbnailGenerator::nextPathIndex() {
    int i = _lastRequestIndex;
    for (; i < _requests.size(); i++) {
        if (!_requests[i].requested) {
            return i;
        }
    }
    return -1;
}

int ThreadedThumbnailGenerator::prevPathIndex() {
    int i = _lastRequestIndex;
    for (; i >= 0; i--) {
        if (!_requests[i].requested) {
            return i;
        }
    }
    return -1;
}

Worker::Worker(ThreadedThumbnailGenerator *generator) {
    _generator = generator;
}

void Worker::generateThumbnail(ThumbnailRequest request, int queueId) {
    _request = request;

    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    ThumbnailLoader loader;
    ThumbnailLoader::ExifOrientation orientation = ThumbnailLoader::Horizontal;
    QSize fullSize;
    QImage thumbnail;
    if (ThumbnailLoader::isRawOrTiff(request.sourcePath) || ThumbnailLoader::isJpeg(request.sourcePath)) {
        thumbnail = loader.loadRawOrTiff(request.sourcePath, request.targetSize, &orientation, &fullSize);
    }
//    else if (ThumbnailLoader::isJpeg(path)) {
//        thumbnail = loader.loadJpeg(path, _dimensions, &orientation, &fullSize);
//    }
    else {
        thumbnail = loader.loadImageOther(request.sourcePath, &orientation, &fullSize);
    }

    if (orientation == ThumbnailLoader::ExifOrientation::Rotate270CW ||
            orientation == ThumbnailLoader::ExifOrientation::Rotate90CW ||
            orientation == ThumbnailLoader::ExifOrientation::MirrorHorizontalAndRotate270CW ||
            orientation == ThumbnailLoader::ExifOrientation::MirrorHorizontalAndRotate90CW) {
        fullSize = QSize(fullSize.height(), fullSize.width());
    }
    if (!request.targetSize.isEmpty()) {
        thumbnail = loader.createThumbnail(thumbnail, request.targetSize, orientation);
        thumbnail = loader.unsharpMask(thumbnail);
    }
    thumbnail = loader.rotateAndFlip(thumbnail, orientation);

    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

//    thumbnail.setDevicePixelRatio(_dpr);
    emit resultReady(request.sourcePath, thumbnail, fullSize);

//    if (!request.targetSize.isEmpty()) {
//        thumbnail.save(QString("C:\\Temp\\%1.png").arg(QFileInfo(request.sourcePath).fileName()));
//    }
//    QImage img(200, 164, QImage::Format_RGBA8888);

//    QImage img(2000, 2000, QImage::Format_RGBA8888);
//    for (int i = 0; i < 1000; i++) {
//        img.fill(Qt::blue);
//    }
//    emit resultReady(path, img.scaled(100, 100));

//    QThread::sleep(10);
}

void ThreadedThumbnailGenerator::onResultReady(const QString &path, const QImage &image, QSize fullSize) {
//    qDebug() << "-------- on ready" << path;
    emit thumbnailReady(path, image, fullSize);
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
