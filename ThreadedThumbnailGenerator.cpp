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
    _readFinished = true;
    const int MaxThreads = QThread::idealThreadCount();
    qDebug() << "Using" << MaxThreads << "threads";
    for (int i = 0; i < MaxThreads; i++) {
        WorkerInfo info;
        info.thread = new QThread(this);
        info.worker = new DecodeWorker(this);
        info.isFinished = true;
        info.worker->moveToThread(info.thread);
        connect(info.worker, &DecodeWorker::decodeResultReady,
                this, &ThreadedThumbnailGenerator::onThumbnailDecodeFinished);
        _workers.append(info);

        info.thread->start();
//        info.thread->setPriority(QThread::LowPriority);
    }

    _loaderThread = new QThread(this);
    _loaderWorker = new ReadWorker(this);
    _loaderWorker->moveToThread(_loaderThread);
    _loaderThread->start();

    connect(this, &ThreadedThumbnailGenerator::requestReadThumbnail,
            _loaderWorker, &ReadWorker::readThumbnail);
    connect(_loaderWorker, &ReadWorker::readResultReady,
            this, &ThreadedThumbnailGenerator::onThumbnailReadFinished);
}

void ThreadedThumbnailGenerator::prepareToClose() {
    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->quit();
        _workers[i].thread->wait();
    }
    _loaderThread->quit();
    _loaderThread->wait();
}

void ThreadedThumbnailGenerator::setRequestQueue(QList<ThumbnailReadRequest> requests) {
    _benchmark.start();
    _requests = requests;
    _lastRequestIndex = -1;
    _lastRequestIndexIncrement = 1;
    _readFinished = false;
    _queueId.fetchAndAddOrdered(1);
    for (int i = 0; i < _requests.size(); i++) {
        requestNextThumbnailRead();
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

void ThreadedThumbnailGenerator::addRequestQueue(QList<ThumbnailReadRequest> requests) {
//    if (_lastRequestIndex != -2) {
//        insertList(_requests, _lastRequestIndex, requests);
//    }

//    int lastQueueSize = _requests.size() - 1;
//    _requests.append(requests);
//    if (_lastRequestIndex == -2) {
//        _lastRequestIndex = lastQueueSize;
//        for (int i = 0; i < _workers.size(); i++) {
//            if (_workers[i].isFinished) {
//                requestNextThumbnailRead(_workers[i]);
//            }
//        }
//    }
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

bool ThreadedThumbnailGenerator::requestNextThumbnailRead() {
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
        _requests[_lastRequestIndex].requested = true;
        emit requestReadThumbnail(_requests.at(_lastRequestIndex), _queueId.loadAcquire());
    }
    else {
        return false;
    }
    return true;
}

bool ThreadedThumbnailGenerator::requestNextThumbnailDecode(WorkerInfo &worker) {
    if (_thumbnailData.isEmpty()) {
        return false;
    }

    worker.isFinished = false;
    QMetaObject::Connection connection = connect(this, &ThreadedThumbnailGenerator::requestDecodeThumbnail,
                                                 worker.worker, &DecodeWorker::decodeThumbnail);
    emit requestDecodeThumbnail(_thumbnailData.dequeue(), _queueId.loadAcquire());
    disconnect(connection);
    return true;
}

void ThreadedThumbnailGenerator::checkIfFinished() {
    bool decodeFinished = true;
    for (int i = 0; i < _workers.size(); i++) {
        if (!_workers[i].isFinished) {
            decodeFinished = false;
            break;
        }
    }
    if (_readFinished && decodeFinished) {
        qDebug() << "generation finished";
        qDebug() << "took:" << _benchmark.restart() << "ms";
        emit generationFinished();
    }
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

void ThreadedThumbnailGenerator::onThumbnailReadFinished(const ThumbnailReadResult &result) {
    _thumbnailData.enqueue(result);

    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].isFinished) {
            requestNextThumbnailDecode(_workers[i]);
            break;
        }
    }

    if (result.request.sourcePath == _requests.last().sourcePath) {
        _readFinished = true;
        checkIfFinished();
    }
}

void ThreadedThumbnailGenerator::onThumbnailDecodeFinished(const QString &path, const QImage &image, QSize fullSize) {
//    qDebug() << "-------- on ready" << path;
    emit thumbnailReady(path, image, fullSize);
    DecodeWorker *worker = static_cast<DecodeWorker*>(sender());
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].worker == worker) {
            if (!requestNextThumbnailDecode(_workers[i])) {
                _workers[i].isFinished = true;
                checkIfFinished();
            }
            break;
        }
    }
}


DecodeWorker::DecodeWorker(ThreadedThumbnailGenerator *generator) {
    _generator = generator;
}

void DecodeWorker::decodeThumbnail(const ThumbnailReadResult &readResult, int queueId) {
    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    ThumbnailLoader loader;
    QImage thumbnail = loader.decodeImage(readResult.data, readResult.mimeType);

    QSize fullSize = readResult.fullSize;
    if (readResult.orientation == ExifOrientation::Rotate270CW ||
            readResult.orientation == ExifOrientation::Rotate90CW ||
            readResult.orientation == ExifOrientation::MirrorHorizontalAndRotate270CW ||
            readResult.orientation == ExifOrientation::MirrorHorizontalAndRotate90CW) {
        fullSize = QSize(fullSize.height(), fullSize.width());
    }
    thumbnail = loader.rotateAndFlip(thumbnail, readResult.orientation);
    thumbnail = loader.createThumbnail(thumbnail, readResult.request.targetSize, readResult.orientation);
    thumbnail = loader.unsharpMask(thumbnail);

    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    emit decodeResultReady(readResult.request.sourcePath, thumbnail, fullSize);
}


ReadWorker::ReadWorker(ThreadedThumbnailGenerator *generator) {
    _generator = generator;
}

void ReadWorker::readThumbnail(ThumbnailReadRequest request, int queueId) {
    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    ThumbnailLoader loader;
    if (ThumbnailLoader::isExifCompatible(request.sourcePath)) {
        ThumbnailReadResult result;
        result.request = request;
        if (loader.readExifPreview(request.sourcePath, request.targetSize, result)) {
            if (queueId != _generator->_queueId.loadRelaxed()) {
                return;
            }

            emit readResultReady(result);
        }
    }
}
