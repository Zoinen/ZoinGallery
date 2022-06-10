#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"
#include "ImageFile.h"

#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QDeadlineTimer>

#include <chrono>
using namespace std::chrono_literals;

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

    _readWorker = new ReadWorker(this);
    connect(_readWorker, &ReadWorker::readResultReady,
            this, &ThreadedThumbnailGenerator::onThumbnailReadFinished);
    _readWorker->start();
}

void ThreadedThumbnailGenerator::prepareToClose() {
    // TODO: Terminate threads when closing app since they don't just stop
    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->terminate();
//        _workers[i].thread->quit();
//        _workers[i].thread->wait(QDeadlineTimer(200ms));
    }
    _readWorker->terminate();
//    _loaderThread->quit();
    //    _loaderThread->wait(QDeadlineTimer(200ms));
}

void ThreadedThumbnailGenerator::clearRequests() {
    _thumbnailReadSet.clear();
    _thumbnailDecodeQueue.clear();
    _readFinished = false;
    _requests.clear();

    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].isFinished = true;
    }
    _queueId.fetchAndAddOrdered(1);
}

void ThreadedThumbnailGenerator::requestRead(QList<ThumbnailReadRequest> requests) {
    _benchmark.start();
    for (int i = 0; i < requests.size(); i++) {
        requests[i].queueId = _queueId.loadAcquire(); // TODO: Make sure it's correct
    }
    prependList(_requests, requests);
    _readWorker->readQueue().prepend(requests);
}

void ThreadedThumbnailGenerator::requestDecode(QList<ThumbnailReadRequest> requests) {
//    qDebug() << "REQ ------------;";
//    for (int i = 0; i < requests.size(); i++) {
//        qDebug() << requests[i].sourcePath;
//    }
//    return;
    for (int i = 0; i < requests.size(); i++) {
        auto it = _thumbnailReadSet.find(requests.at(i).sourcePath);
        if (it != _thumbnailReadSet.end()) {
            ThumbnailReadResult res = *it;
            res.request.targetSize = requests.at(i).targetSize;
            _thumbnailDecodeQueue.enqueue(res);
            _thumbnailReadSet.erase(it);
        }
    }
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].isFinished) {
            requestNextThumbnailDecode(_workers[i]);
            break;
        }
    }
}

bool ThreadedThumbnailGenerator::requestNextThumbnailDecode(WorkerInfo &worker) {
    if (_thumbnailDecodeQueue.isEmpty()) {
        return false;
    }

    worker.isFinished = false;
    QMetaObject::Connection connection = connect(this, &ThreadedThumbnailGenerator::requestDecodeThumbnail,
                                                 worker.worker, &DecodeWorker::decodeThumbnail);
    emit requestDecodeThumbnail(_thumbnailDecodeQueue.dequeue(), _queueId.loadAcquire());
    disconnect(connection);
    return true;
}

void ThreadedThumbnailGenerator::checkIfFinished() {
    bool decodingFinished = true;
    for (int i = 0; i < _workers.size(); i++) {
        if (!_workers[i].isFinished) {
            decodingFinished = false;
            break;
        }
    }
    if (_readFinished && decodingFinished) {
        qDebug() << "decode finished";
        qDebug() << "took:" << _benchmark.restart() << "ms";
        emit decodeFinished();
    }
}

void ThreadedThumbnailGenerator::onThumbnailReadFinished(const ThumbnailReadResult &result) {
    if (result.request.queueId != _queueId.loadRelaxed()) {
        return;
    }
    _thumbnailReadSet[result.request.sourcePath] = result;
    if (!result.request.viewerRequest) {
        emit thumbnailInfoReady(result.request.sourcePath, rotateToOrientation(result.fullSize, result.orientation));
    }
    else {
        QList<ThumbnailReadRequest> requests;
        requests.append(ThumbnailReadRequest(result.request.sourcePath, result.request.targetSize));
        requestDecode(requests);
    }

    if (_requests.size() && result.request.sourcePath == _requests.last().sourcePath) {
        _readFinished = true;
        qDebug() << "read finished";
        emit readFinished();
        checkIfFinished();
    }
}

void ThreadedThumbnailGenerator::onThumbnailDecodeFinished(const ThumbnailReadResult &readResult, const QImage &image) {
    qDebug() << "-------- on ready" << readResult.request.sourcePath << readResult.request.viewerRequest;
    if (!readResult.request.viewerRequest) {
        emit thumbnailReady(readResult.request.sourcePath, image);
    }
    else {
        emit viewerReady(readResult.request.sourcePath, image);
    }
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
    loader.setPath(readResult.request.sourcePath);
    QImage thumbnail;
    if (!readResult.thumbnailData.isNull()) {
        thumbnail = loader.decodeImage(readResult.thumbnailData, readResult.mimeType, rotateToOrientation(readResult.request.targetSize, readResult.orientation));
        delete[] readResult.thumbnailData.constData();
    }
    if (!readResult.fullImageData.isEmpty() && (thumbnail.isNull() ||
            readResult.thumbnailSize.width() < readResult.request.targetSize.width() ||
            readResult.thumbnailSize.height() < readResult.request.targetSize.height())) {
//        qDebug() << "TRY#" << readResult.request.sourcePath << readResult.thumbnailSize << "OF" << readResult.request.targetSize;
        QImage fullImage = loader.decodeImage(readResult.fullImageData, "image/jpeg", rotateToOrientation(readResult.request.targetSize, readResult.orientation)); // TODO: Support other formats
        if (!fullImage.isNull()) {
            thumbnail = fullImage;
        }
    }

    thumbnail = loader.rotateAndFlip(thumbnail, readResult.orientation);
    thumbnail = loader.createThumbnail(thumbnail, readResult.request.targetSize, readResult.request.viewerRequest);
//    if (!readResult.request.viewerRequest) {
    thumbnail = loader.unsharpMask(thumbnail);
//    }
//    thumbnail.save(QString("c:\\temp\\%1+.png").arg(QFileInfo(readResult.request.sourcePath).baseName()));


    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    emit decodeResultReady(readResult, thumbnail);
}


ReadWorker::ReadWorker(QObject *parent)
    : QThread(parent) {
}

void ReadWorker::run() {
    forever {
        ThumbnailReadRequest request = _readQueue.dequeue();

        ThumbnailLoader loader;
        if (ThumbnailLoader::isExifCompatible(request.sourcePath)) {
            ThumbnailReadResult result;
            result.request = request;
            qDebug() << "READ" << request.sourcePath << request.targetSize;
            bool fileLoaded = loader.readExifPreview(request.sourcePath, request.targetSize, result);
            if ((!fileLoaded && !result.fullSize.isNull()) ||
                    (fileLoaded && (result.thumbnailSize.width() < request.targetSize.width() ||
                                    result.thumbnailSize.height() < request.targetSize.height()))) {
                QFile f(request.sourcePath);
                if (f.open(QFile::ReadOnly)) {
                    //                result.fullImageData = QByteArray::fromRawData((const char *)f.map(0, f.size()), f.size());

                    result.fullImageData = f.readAll();
                    f.close();
                    fileLoaded = true;
                }
            }
            if (fileLoaded) {
                qDebug() << "READ LOADED";
                emit readResultReady(result);
            }
        }

        qDebug() << "READ DONE" << request.sourcePath;
        //    QThread::msleep(300);
    }
}

ThreadSafeQueue<ThumbnailReadRequest> &ReadWorker::readQueue() {
    return _readQueue;
}
