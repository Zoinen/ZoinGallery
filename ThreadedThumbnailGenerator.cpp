#include "ThreadedThumbnailGenerator.h"
#include "ThumbnailLoader.h"
#include "ImageFile.h"

#include <QThread>
#include <QThreadPool>
#include <QDebug>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
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
                this, &ThreadedThumbnailGenerator::onDecodeFinished);
        _workers.append(info);

        info.thread->start();
//        info.thread->setPriority(QThread::LowPriority);
    }

    _readWorker = new ReadWorker(this);
    connect(_readWorker, &ReadWorker::readResultReady,
            this, &ThreadedThumbnailGenerator::onReadFinished);
    connect(_readWorker, &ReadWorker::folderListReady,
            this, &ThreadedThumbnailGenerator::folderListReady);
    _readWorker->start();
}

void ThreadedThumbnailGenerator::prepareToClose() {
    _readWorker->requestInterruption();
    _readWorker->readQueue().unlock();
    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->quit();
    }

    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->wait(QDeadlineTimer(2000ms));
    }
    _readWorker->wait(QDeadlineTimer(2000ms));
    qDebug() << "Prepare to close";
}

void ThreadedThumbnailGenerator::clearRequests() {
    _readSet.clear();
    _decodeQueue.clear();
    _readFinished = false;
    _requests.clear();
    _readWorker->readQueue().clear();

    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].isFinished = true;
    }
    _queueId.fetchAndAddOrdered(1);
}

void ThreadedThumbnailGenerator::requestRead(QList<ImageReadRequest> requests) {
    if (!requests.size()) {
        return;
    }

    _benchmark.start();
    int queueId = _queueId.loadAcquire();
    for (int i = 0; i < requests.size(); i++) {
        requests[i].queueId = queueId;
    }

    prependList(_requests, requests);
    if (requests.first().viewerRequest) {
        _readWorker->readQueue().prependNewAndPrioritizeDuplicates(requests);
    }
    else {
        _readWorker->readQueue().prepend(requests);
    }
}

void ThreadedThumbnailGenerator::requestThumbnailDecode(QList<ImageReadRequest> requests) {
//    qDebug() << "REQ ------------;";
//    for (int i = 0; i < requests.size(); i++) {
//        qDebug() << requests[i].sourcePath;
//    }
//    return;
    for (int i = 0; i < requests.size(); i++) {
        auto it = _readSet.find(requests.at(i).sourcePath);
        if (it != _readSet.end()) {
            ImageReadResult res = *it;
            res.request.targetSize = requests.at(i).targetSize;
            _decodeQueue.enqueue(res);
            _readSet.erase(it);
        }
    }
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].isFinished) {
            requestNextDecode(_workers[i]);
            break;
        }
    }
}

void ThreadedThumbnailGenerator::requestViewerDecode(ImageReadRequest request) {
    auto it = _readSet.find(request.sourcePath);
    if (it != _readSet.end()) {
        ImageReadResult res = *it;
        res.request.targetSize = request.targetSize;
        _decodeQueue.prepend(res);
        _readSet.erase(it);
    }

    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].isFinished) {
            requestNextDecode(_workers[i]);
            break;
        }
    }
}

bool ThreadedThumbnailGenerator::requestNextDecode(WorkerInfo &worker) {
    if (_decodeQueue.isEmpty()) {
        return false;
    }

    worker.isFinished = false;
    QMetaObject::Connection connection = connect(this, &ThreadedThumbnailGenerator::requestDecodeThumbnail,
                                                 worker.worker, &DecodeWorker::decode);
    ImageReadResult request = _decodeQueue.dequeue();
    if ((request.thumbnailSize.width() < request.request.targetSize.width() ||
        request.thumbnailSize.height() < request.request.targetSize.height()) && request.largerImageAvailable &&
            !request.request.viewerRequest) {
        _readSet.remove(request.request.sourcePath);

        ImageReadRequest readHigher = ImageReadRequest(request.request.sourcePath, request.request.targetSize);
        readHigher.higherThumbnailRequest = true;
        requestRead(QList<ImageReadRequest>({readHigher}));

        worker.isFinished = true;
    }
    else {
        emit requestDecodeThumbnail(request, _queueId.loadAcquire());
    }
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
        qDebug() << "decode finished. Took:" << _benchmark.restart() << "ms";
        emit decodeFinished();
    }
}

void ThreadedThumbnailGenerator::onReadFinished(const ImageReadResult &result) {
    _readSet[result.request.sourcePath] = result;
    if (result.request.queueId != _queueId.loadRelaxed()) {
        return;
    }
    if (result.request.higherThumbnailRequest) {
        ImageReadRequest decodeHigherRequest(result.request.sourcePath, result.request.targetSize);
        requestThumbnailDecode(QList<ImageReadRequest>{decodeHigherRequest});
    }
    else if (result.request.viewerRequest) {
        requestViewerDecode(ImageReadRequest(result.request.sourcePath, result.request.targetSize));
    }
    else if (result.request.inFolderRequest) {

    }
    else {
        emit thumbnailInfoReady(result.request.sourcePath, rotateToOrientation(result.fullSize, result.orientation));
    }

    if (!_readWorker->readQueue().size() && !_readFinished) {
        _readFinished = true;
        qDebug() << "Generator: read finished";
        emit readFinished();
        checkIfFinished();
    }
}

void ThreadedThumbnailGenerator::onDecodeFinished(const ImageReadResult &readResult, const QImage &image) {
//    qDebug() << "-------- on ready" << readResult.request.sourcePath << readResult.request.viewerRequest;
    if (!readResult.request.viewerRequest) {
        emit thumbnailReady(readResult.request.sourcePath, image);
    }
    else {
        emit viewerReady(readResult.request.sourcePath, image);
    }
    DecodeWorker *worker = static_cast<DecodeWorker*>(sender());
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].worker == worker) {
            if (!requestNextDecode(_workers[i])) {
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

void DecodeWorker::decode(const ImageReadResult &readResult, int queueId) {
    if (queueId != _generator->_queueId.loadRelaxed()) {
        return;
    }

    ThumbnailLoader loader;
    loader.setPath(readResult.request.sourcePath);
    QImage thumbnail;
    if (!readResult.thumbnailData.isNull()) {
        thumbnail = loader.decodeImage(readResult.thumbnailData, readResult.mimeType, rotateToOrientation(readResult.request.targetSize, readResult.orientation), readResult);
        delete[] readResult.thumbnailData.constData();
    }
    if (!readResult.fullImageData.isEmpty() && (thumbnail.isNull() ||
            readResult.thumbnailSize.width() < readResult.request.targetSize.width() ||
            readResult.thumbnailSize.height() < readResult.request.targetSize.height())) {
//        qDebug() << readResult.request.sourcePath << "full image";
//        qDebug() << "TRY#" << readResult.request.sourcePath << readResult.thumbnailSize << "OF" << readResult.request.targetSize;
        QImage fullImage = loader.decodeImage(readResult.fullImageData, readResult.mimeType, rotateToOrientation(readResult.request.targetSize, readResult.orientation), readResult);
        if (!fullImage.isNull()) {
            thumbnail = fullImage;
        }
    }

    thumbnail = loader.rotateAndFlip(thumbnail, readResult.orientation);
    if (!ThumbnailLoader::isVectorImage(readResult.request.sourcePath)) {
        thumbnail = loader.createThumbnail(thumbnail, readResult.request.targetSize, readResult.request.viewerRequest);

        // TODO: Remove this hack and fix alpha channel handling for pngs
        if (!readResult.request.sourcePath.endsWith(".png", Qt::CaseInsensitive)) {
            thumbnail = loader.unsharpMask(thumbnail);
        }
    }
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
        if (isInterruptionRequested()) {
            return;
        }
        ImageReadRequest request = _readQueue.dequeue();

        if (!request.folderRequest) {
            readImage(request);
        }
        else {
            QDir dir(request.sourcePath);
            auto images = dir.entryInfoList(ThumbnailLoader::supportedFormats(), QDir::Files, QDir::Name);
            int totalImages = 16;
            QList<QFileInfo> imagesFiltered;
            for (float i = 0; i < images.size(); i += qMax(1.0f, float(images.size()) / totalImages)) {
                imagesFiltered.append(images.at(i));
            }
            emit folderListReady(request.sourcePath, imagesFiltered);
        }

//        qDebug() << "READ DONE" << request.sourcePath;
//            QThread::msleep(300);
    }
}

void ReadWorker::readImage(ImageReadRequest &request) {
    ThumbnailLoader loader;
    ImageReadResult result;
    result.request = request;
//            qDebug() << "READ" << request.sourcePath << request.targetSize;
    bool fileLoaded = loader.readExifPreview(request.sourcePath, request.targetSize, result);
    QSize rotatedThumbnailSize = rotateToOrientation(result.thumbnailSize, result.orientation);;
    if ((!fileLoaded && result.fullSize.isValid()) ||
            (fileLoaded && (rotatedThumbnailSize.width() < request.targetSize.width() ||
                            rotatedThumbnailSize.height() < request.targetSize.height()))) {
        QFile f(request.sourcePath);
        if (f.open(QFile::ReadOnly)) {
            //                result.fullImageData = QByteArray::fromRawData((const char *)f.map(0, f.size()), f.size());

            result.fullImageData = f.readAll();
            f.close();

            fileLoaded = true;
        }
    }
    else if (!fileLoaded) {
        fileLoaded = loader.readGenericPreview(request.sourcePath, request.targetSize, result);
        qDebug() << "READING FULL" << result.request.sourcePath;
    }
    if (fileLoaded) {
        emit readResultReady(result);
    }
}

ThreadSafeQueue &ReadWorker::readQueue() {
    return _readQueue;
}
