#include "DecodeManager.h"

#include "Runners/FolderListReadRunner.h"
#include "Runners/ImageDecodeRunner.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageReadRunner.h"
#include "Runners/CacheImageRunners.h"
#include "Runners/RecursiveFolderScanner.h"

#include "PersistentFolderCache.h"
#include "PersistentImageCache.h"

#include <QThread>

#include <chrono>

using namespace std::chrono_literals;

bool isRunnerDecode(Runner *runner) {
    return runner->type() == RunnerType::ImageRead || runner->type() == RunnerType::ImageDecode || runner->type() == RunnerType::CachedImageRetrieve;
}

bool isRunnerDecodeViewer(Runner *runner) {
    return runner->isViewerRequest() && (runner->type() == RunnerType::ImageRead || runner->type() == RunnerType::ImageDecode);
}

bool isRunnerInReadThread(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead || runner->type() == RunnerType::ImageRead;
}

bool isRunnerImageInfoEmbedded(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead && static_cast<ImageInfoReadRunner *>(runner)->isEmbeddedRequest();
}


DecodeManager::DecodeManager(QObject *parent)
    : QObject(parent) {

    _runningTasksUpdateTimer.start(100);
    connect(&_runningTasksUpdateTimer, &QTimer::timeout,
            this, &DecodeManager::updateRunningTasksCount);

    const int MaxThreads = qMax(int(SpecialThreads::Last) + 1, QThread::idealThreadCount());
    qDebug() << "Using" << MaxThreads << "threads";
    for (int i = 0; i < MaxThreads; i++) {
        WorkerInfo info{
            .thread = new QThread(this),
            .runner = nullptr,
        };

        _workers.append(info);

        info.thread->start();
        // info.thread->setPriority(QThread::IdlePriority);
    }
}

void DecodeManager::readImagesInfo(const QList<QString> &paths, bool isFromEmbeddedView) {
    if (!_disableCache) {
        CachedImageInfoRunner *runner = new CachedImageInfoRunner(paths, isFromEmbeddedView);
        runner->connections.append(
            connect(runner, &CachedImageInfoRunner::cachedImageInfoRetrieved,
                    this, &DecodeManager::onCachedImageInfoRetrieved)
            );
        _taskQueue.prepend(runner);
        processQueue();
    }
    else {
        onInfoNotFoundInCache(paths, isFromEmbeddedView);
    }
}

void DecodeManager::onInfoNotFoundInCache(const QList<QString> &imagePaths, bool isFromEmbeddedView) {
    int insertIndex = _taskQueue.size();
    // Info requests from visible subviews should be first
    if (isFromEmbeddedView) {
        for (insertIndex = 0; insertIndex < _taskQueue.size(); insertIndex++) {
            if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead && !isRunnerImageInfoEmbedded(_taskQueue.at(insertIndex))) {
                break;
            }
        }
    }

    for (int i = 0; i < imagePaths.size(); i++) {
        ImageInfoReadRunner *runner = new ImageInfoReadRunner(imagePaths[i], i == imagePaths.size() - 1, isFromEmbeddedView);
        runner->connections.append(
            connect(runner, &ImageInfoReadRunner::imageInfoReady,
                    this, &DecodeManager::onImageInfoReady)
        );
        _taskQueue.insert(insertIndex, runner);
        insertIndex++;
    }
    processQueue();
}

void DecodeManager::decodeImages(const QList<ImageDecodeRequest> &requests) {
    // Only iterating images that are known to have cache
    /**/{
        int insertIndex = 0;
        // Read requests from viewer are first
        if (requests.size() && requests.first().viewerRequest) {
            insertIndex = 0;
        }
        else {
            // Thumbnails cache decode requests should be before any full-fledged reading or decoding
            for (; insertIndex < _taskQueue.size(); insertIndex++) {
                if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead ||
                    _taskQueue.at(insertIndex)->type() == RunnerType::ImageRead ||
                    _taskQueue.at(insertIndex)->type() == RunnerType::ImageDecode) {
                    break;
                }
            }
        }

        // qDebug() << "ZZ DECODE" << requests.size() << (requests.size() ? requests.first().info.path : "");
        if (!_disableCache) {
            for (const auto &request : requests) {
                if (request.checkCache) {
                    // qDebug() << "ZZ TO RUN CACHE LOOKUP";
                    CachedImageRetrieveRunner *runner = new CachedImageRetrieveRunner(request);
                    runner->connections.append(
                        connect(runner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved,
                                this, &DecodeManager::onImageReady)
                        );
                    _taskQueue.insert(insertIndex, runner);
                    insertIndex++;
                }
            }
        }
    }/**/

    /**/int insertIndex = 0;
    // Read requests from viewer are first
    if (requests.size() && requests.first().viewerRequest) {
        insertIndex = 0;
    }
    else {
        // Thumbnails Read requests should be before any Info requests
        for (; insertIndex < _taskQueue.size(); insertIndex++) {
            if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead) {
                break;
            }
        }
    }

    for (const auto &request : requests) {
        if (request.viewerRequest) {
            // qDebug() << "ZZ read" << request.info.path << request.targetSize << _taskQueue.size();
        }
        ImageReadRunner *runner = new ImageReadRunner(request);
        runner->connections.append(
            connect(runner, &ImageReadRunner::imageReadReady,
                    this, &DecodeManager::onImageReadReady)
        );
        _taskQueue.insert(insertIndex, runner);
        insertIndex++;
    }
    /**/
    processQueue();
}

void DecodeManager::readFolderList(const QStringList &paths, int totalImages) {
    QList<FolderInfo> results;
    QStringList notFound;
    if (!_disableCache) {
        PersistentFolderCache::retrieveFolders(paths, results, notFound);
    }
    else {
        notFound = paths;
    }
    for (FolderInfo &result : results) {
        emit folderListReady(result.path, result.subfiles);
    }

    for (const QString &path : notFound) {
        FolderListReadRunner *runner = new FolderListReadRunner(path, totalImages);
        runner->connections.append(
            connect(runner, &FolderListReadRunner::folderListReady,
                    this, &DecodeManager::onFolderListReady)
            );
        _taskQueue.enqueue(runner);
    }
    processQueue();
}

void DecodeManager::scan(const QString &root) {
    RecursiveFolderScanner *runner = new RecursiveFolderScanner(root);
    runner->connections.append(
        connect(runner, &RecursiveFolderScanner::scanImages,
                this, &DecodeManager::scanImages)
        );
    _taskQueue.enqueue(runner);
    processQueue();
}

void DecodeManager::scanImages(const QList<QString> &imagePaths) {
    for (int i = 0; i < imagePaths.size(); i++) {
        ImageInfoReadRunner *runner = new ImageInfoReadRunner(imagePaths[i], false, false, true);
        runner->connections.append(
            connect(runner, &ImageInfoReadRunner::imageInfoReady,
                    this, &DecodeManager::onScannerInfoReady)
            );
        _taskQueue.append(runner);
    }
    processQueue();

}

void DecodeManager::cancelAllDecodeRunners() {
    // qDebug() << "-----------" << __FUNCTION__;
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            if (isRunnerDecode(runner)) {
                runner->cancel();

                if (runner->isViewerRequest()) {
                    emit viewerRunnerCanceled(runner->path());
                }
            }
        }
    }

    for (int i = 0; i < _taskQueue.size(); i++) {
        if (isRunnerDecode(_taskQueue.at(i))) {
            Runner *runner = _taskQueue.takeAt(i);
            if (runner->isViewerRequest()) {
                emit viewerRunnerCanceled(runner->path());
            }
            runner->deleteLater();
            i--;
        }
    }
}

void DecodeManager::cancelAllRunners() {
    qDebug() << __FUNCTION__;
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            runner->cancel();

            if (runner->isViewerRequest()) {
                emit viewerRunnerCanceled(runner->path());
            }
        }
    }
    _taskQueue.clear();
}

void DecodeManager::cancelAllDecodeViewerRunners() {
    qDebug() << __FUNCTION__;
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            if (isRunnerDecodeViewer(runner)) {
                runner->cancel();

                if (runner->isViewerRequest()) {
                    emit viewerRunnerCanceled(runner->path());
                }
            }
        }
    }

    for (int i = 0; i < _taskQueue.size(); i++) {
        if (isRunnerDecodeViewer(_taskQueue.at(i))) {
            Runner *runner = _taskQueue.takeAt(i);
            if (runner->isViewerRequest()) {
                emit viewerRunnerCanceled(runner->path());
            }
            runner->deleteLater();
            i--;
        }
    }
}

void DecodeManager::prepareToClose() {
    cancelAllRunners();

    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->quit();
    }

    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->wait(QDeadlineTimer(2000ms));
    }

    if (!_disableCache) {
        PersistentFolderCache::dumpDb();
        PersistentImageCache::dumpDb();
    }
}

bool DecodeManager::runningTasksDebug() const {
    return _runningTasksDebug;
}

void DecodeManager::setRunningTasksDebug(bool isRunningTasksDebug) {
    _runningTasksDebug = isRunningTasksDebug;
}

QString DecodeManager::runnerToString(Runner *task) {
    if (!task) {
        return "-";
    }
    switch (task->type()) {
    case RunnerType::ImageInfoRead:
        return QString("ImageInfoRead %2%3").arg(static_cast<ImageInfoReadRunner *>(task)->_path).arg(static_cast<ImageInfoReadRunner *>(task)->isEmbeddedRequest() ? " E" : "");
        break;
    case RunnerType::ImageRead:
        return QString("ImageRead %2%3").arg(static_cast<ImageReadRunner *>(task)->_request.info.path).arg(static_cast<ImageReadRunner *>(task)->isViewerRequest() ? " V" : "");
        break;
    case RunnerType::FolderListRead:
        return QString("FolderListRead %2 %3").arg(static_cast<FolderListReadRunner *>(task)->_path).arg(static_cast<FolderListReadRunner *>(task)->_totalImages);
        break;
    case RunnerType::ImageDecode:
        return QString("ImageDecode %2%3").arg(static_cast<ImageDecodeRunner *>(task)->_imageData.request.info.path).arg(static_cast<ImageDecodeRunner *>(task)->isViewerRequest() ? " V" : "");
        break;
    case RunnerType::CachedImageStore:
        return QString("CachedImageStore %2").arg(static_cast<CachedImageStoreRunner *>(task)->_imageInfo.path);
        break;
    case RunnerType::CachedImageInfo:
        return QString("CachedImageInfo %2%3").arg(static_cast<CachedImageInfoRunner *>(task)->_imagePaths.size()).arg(static_cast<CachedImageInfoRunner *>(task)->_isFromEmbeddedView ? " E" : "");
        break;
    case RunnerType::CachedImageRetrieve:
        return QString("CachedImageRetrieve %2%3").arg(static_cast<CachedImageRetrieveRunner *>(task)->_request.info.path).arg(static_cast<CachedImageRetrieveRunner *>(task)->_request.viewerRequest ? " V" : "");
        break;
    case RunnerType::RecursiveFolderScanner:
        return QString("RecursiveFolderScanner");
        break;
    }
    return QString();
}

void DecodeManager::updateRunningTasksCount() {
    int tasks = 0;
    for (int workerIndex = 0; workerIndex < _workers.size(); workerIndex++) {
        if (_workers[workerIndex].runner) {
            tasks++;
        }
    }

    QStringList tasksInfo;
    if (_runningTasksDebug) {
        for (int i = 0; i < _workers.size(); i++) {
            tasksInfo.append(QString("%1 %2").arg(i).arg(runnerToString(_workers[i].runner)));
        }
        tasksInfo.append("-------------------");
        for (int i = 0; i < qMin(100000, _taskQueue.size()); i++) {
            tasksInfo.append(QString("%1 %2").arg(i).arg(runnerToString(_taskQueue[i])));
        }
    }

    emit runningTasksChanged(QString("%1/%2").arg(tasks).arg(_taskQueue.size()), tasksInfo);
}

void DecodeManager::processQueue() {
    // qDebug() << __FUNCTION__ << _taskQueue.size();
    if (!_timer.isValid()) {
        _timer.start();
        // qDebug() << "ZZ TIMER START";
    }
    if (_taskQueue.isEmpty()) {
        // qDebug() << "ZZ FINISHED:" << _timer.elapsed() << "ms";
    }
    for (int workerIndex = 0; workerIndex < _workers.size(); workerIndex++) {
        if (!_workers[workerIndex].runner) {
            if (_taskQueue.isEmpty()) {
                updateRunningTasksCount();
                _runningTasksUpdateTimer.stop();
                return;
            }
            else {
                if (!_runningTasksUpdateTimer.isActive()) {
                    updateRunningTasksCount();
                    _runningTasksUpdateTimer.start();
                }
            }
            Runner *runner = nullptr;
            for (int taskIndex = 0; taskIndex < _taskQueue.size(); taskIndex++) {
                if (isRunnerTypeMatchesThreadType(_taskQueue.at(taskIndex), workerIndex)) {
                    runner = _taskQueue.takeAt(taskIndex);
                    break;
                }
            }
            if (!runner) {
                continue;
            }

            // qDebug() << "RUNNING" << runner->type() << "on thread" << workerIndex;
            _workers[workerIndex].runner = runner;
            runner->moveToThread(_workers[workerIndex].thread);

            connect(runner, &Runner::finished,
                    this, &DecodeManager::onRunnerFinished);
            QMetaObject::invokeMethod(runner, &Runner::run, Qt::QueuedConnection);
        }
    }
}

void DecodeManager::onRunnerFinished(QObject *runner) {
    for (int i = 0; i < _workers.size(); i++) {
        if (_workers[i].runner == runner) {
            _workers[i].runner = nullptr;
            runner->deleteLater();
            processQueue();
            break;
        }
    }
}

void DecodeManager::onImageReadReady(const ImageData &result) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    ImageDecodeRunner *runner = new ImageDecodeRunner(result);
    if (!_disableCache) {
        runner->connections.append({
            connect(runner, &ImageDecodeRunner::imageReady,
                    this, &DecodeManager::onImageReady),
            connect(runner, &ImageDecodeRunner::storeInCache,
                    this, &DecodeManager::onStoreInCache)
        });
    }
    else {
        runner->connections.append({
            connect(runner, &ImageDecodeRunner::imageReady,
                    this, &DecodeManager::onImageReady)
        });
    }
    _taskQueue.enqueue(runner);

    processQueue();
}

void DecodeManager::onImageInfoReady(const ImageInfo &result) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    emit imageInfoReady(result);
}

void DecodeManager::onImageReady(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    if (!request.info.isFromScanner) {
        emit imageReady(request, image, decodedInfo);
    }
}

void DecodeManager::onFolderListReady(const QString &path, const QList<FileInfo> &subfiles) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    emit folderListReady(path, subfiles);
}

void DecodeManager::onScannerInfoReady(const ImageInfo &result) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    ImageDecodeRequest request{
        .info = result,
        .targetSize = CACHE_IMAGE_RESOLUTION,
        .viewerRequest = false,
        .checkCache = false
    };

    ImageReadRunner *runner = new ImageReadRunner(request);
    runner->connections.append(
        connect(runner, &ImageReadRunner::imageReadReady,
                this, &DecodeManager::onImageReadReady)
        );
    _taskQueue.prepend(runner);
    processQueue();
}


void DecodeManager::onStoreInCache(const ImageDecodeRequest &request, const QByteArray &imageData) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    CachedImageStoreRunner *runner = new CachedImageStoreRunner(request.info, imageData);
    _taskQueue.enqueue(runner);
    processQueue();
}

void DecodeManager::onCachedImageInfoRetrieved(const QList<ImageInfo> &results, const QStringList &notFound, bool isFromEmbeddedView, const QString &lastPath) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    emit imagesInfoReady(results);

    onInfoNotFoundInCache(notFound, isFromEmbeddedView);
}

bool DecodeManager::isRunnerTypeMatchesThreadType(Runner *runner, int threadType) {
    return isRunnerInReadThread(runner) == (threadType == (int)SpecialThreads::Read);
}

QDebug operator<<(QDebug dbg, const RunnerType &myEnum) {
    switch (myEnum) {
    case RunnerType::ImageInfoRead: dbg.nospace() << "ImageInfoRead"; break;
    case RunnerType::ImageRead: dbg.nospace() << "ImageRead"; break;
    case RunnerType::FolderListRead: dbg.nospace() << "FolderListRead"; break;
    case RunnerType::ImageDecode: dbg.nospace() << "ImageDecode"; break;
    case RunnerType::CachedImageStore: dbg.nospace() << "CachedImageStore"; break;
    case RunnerType::CachedImageInfo: dbg.nospace() << "CachedImageInfo"; break;
    case RunnerType::CachedImageRetrieve: dbg.nospace() << "CachedImageRetrieve"; break;
    case RunnerType::RecursiveFolderScanner: dbg.nospace() << "RecursiveFolderScanner"; break;
    default: dbg.nospace() << "Unknown"; break;
    }
    return dbg.space();
}

void Runner::cancel() {
    _isCanceled = true;
    for (auto connection : connections) {
        disconnect(connection);
    }
    connections.clear();
}
