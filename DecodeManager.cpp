#include "DecodeManager.h"

#include "Runners/FolderListReadRunner.h"
#include "Runners/ImageDecodeRunner.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageReadRunner.h"
#include "Runners/CacheImageRunners.h"
#include "Runners/RecursiveFolderScanner.h"

#include "PersistentFolderCache.h"
#include "PersistentImageCache.h"
#include "NaturalSort.h"
#include "ThumbnailLoader.h"

#include <QDebug>
#include <QThread>

#include <chrono>
#include <limits>
#include <utility>

using namespace std::chrono_literals;

namespace {
enum class QueuePriority {
    Background,
    High,
    Viewer,
};

QueuePriority queuePriority(const Runner *runner) {
    if (runner->isViewerRequest()) {
        return QueuePriority::Viewer;
    }
    if (runner->isHighPriority()) {
        return QueuePriority::High;
    }
    return QueuePriority::Background;
}

int firstBackgroundTask(const QQueue<Runner *> &queue) {
    int index = 0;
    while (index < queue.size() &&
           queuePriority(queue.at(index)) != QueuePriority::Background) {
        ++index;
    }
    return index;
}

int priorityBandEnd(const QQueue<Runner *> &queue, const Runner *runner) {
    const QueuePriority priority = queuePriority(runner);
    if (priority == QueuePriority::Viewer) {
        int index = 0;
        while (index < queue.size() &&
               queuePriority(queue.at(index)) == QueuePriority::Viewer &&
               queue.at(index)->viewerGeneration() >
                   runner->viewerGeneration()) {
            ++index;
        }
        while (index < queue.size() &&
               queuePriority(queue.at(index)) == QueuePriority::Viewer &&
               queue.at(index)->viewerGeneration() ==
                   runner->viewerGeneration() &&
               queue.at(index)->viewerPriorityOrdinal() <
                   runner->viewerPriorityOrdinal()) {
            ++index;
        }
        while (index < queue.size() &&
               queuePriority(queue.at(index)) == QueuePriority::Viewer &&
               queue.at(index)->viewerGeneration() ==
                   runner->viewerGeneration() &&
               queue.at(index)->viewerPriorityOrdinal() ==
                   runner->viewerPriorityOrdinal()) {
            ++index;
        }
        return index;
    }
    int index = 0;
    while (index < queue.size() &&
           queuePriority(queue.at(index)) >= priority) {
        ++index;
    }
    return index;
}

void insertAheadOfLowerPriority(QQueue<Runner *> &queue, Runner *runner) {
    queue.insert(priorityBandEnd(queue, runner), runner);
}

void insertViewerStageAheadOfSameRequest(QQueue<Runner *> &queue,
                                         Runner *runner) {
    int index = 0;
    while (index < queue.size() &&
           queuePriority(queue.at(index)) == QueuePriority::Viewer &&
           queue.at(index)->viewerGeneration() >
               runner->viewerGeneration()) {
        ++index;
    }
    while (index < queue.size() &&
           queuePriority(queue.at(index)) == QueuePriority::Viewer &&
           queue.at(index)->viewerGeneration() ==
               runner->viewerGeneration() &&
           queue.at(index)->viewerPriorityOrdinal() <
               runner->viewerPriorityOrdinal()) {
        ++index;
    }
    queue.insert(index, runner);
}

QList<FileInfo> previewImages(const QList<FileInfo> &entries, int totalImages) {
    QList<FileInfo> images;
    for (const FileInfo &entry : entries) {
        if (!entry.isDirectory && ThumbnailLoader::isFormatSupported(entry.name)) {
            images.append(entry);
        }
    }
    sortFileInfosNaturally(images);
    if (totalImages < 0 || images.size() <= totalImages) {
        return images;
    }
    if (totalImages == 0) {
        return {};
    }

    QList<FileInfo> sampled;
    const float step = qMax(1.0f, float(images.size()) / totalImages);
    for (float index = 0; index < images.size() && sampled.size() < totalImages; index += step) {
        sampled.append(images.at(static_cast<int>(index)));
    }
    return sampled;
}
}

bool isRunnerDecode(Runner *runner) {
    return runner->type() == RunnerType::ImageRead || runner->type() == RunnerType::ImageDecode || runner->type() == RunnerType::CachedImageRetrieve;
}

bool isRunnerDecodeViewer(Runner *runner) {
    return runner->isViewerRequest() &&
           (runner->type() == RunnerType::ImageRead ||
            runner->type() == RunnerType::ImageDecode ||
            runner->type() == RunnerType::CachedImageRetrieve);
}

bool isRunnerInReadThread(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead || runner->type() == RunnerType::ImageRead;
}

bool isRunnerImageInfoEmbedded(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead && static_cast<ImageInfoReadRunner *>(runner)->isEmbeddedRequest();
}


DecodeManager::DecodeManager(QObject *parent, int maxThreads)
    : QObject(parent) {

    _runningTasksUpdateTimer.start(100);
    connect(&_runningTasksUpdateTimer, &QTimer::timeout,
            this, &DecodeManager::updateRunningTasksCount);

    int requestedThreads = qMax(1, QThread::idealThreadCount());
    if (maxThreads > 0) {
        requestedThreads = qMin(requestedThreads, maxThreads);
    }
    const int MaxThreads =
        qMax(int(SpecialThreads::Last) + 1, requestedThreads);
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

DecodeManager::~DecodeManager() {
    prepareToClose();
}

int DecodeManager::workerCount() const {
    return _workers.size();
}

void DecodeManager::readImagesInfo(const QList<QString> &paths,
                                   bool isFromEmbeddedView,
                                   int directOpenGeneration,
                                   bool highPriority,
                                   const QString &requestNamespace,
                                   qint64 sourceVersionToken,
                                   ImageInfoCachePolicy cachePolicy) {
    if (cachePolicy == ImageInfoCachePolicy::ForceSource) {
        // External catalogs carry a host-authoritative, nanosecond source
        // version. PersistentImageCache is intentionally compatible with the
        // standalone database and stores only QDateTime milliseconds, so it
        // cannot prove that metadata belongs to that exact host version.
        onInfoNotFoundInCache(paths, isFromEmbeddedView,
                              directOpenGeneration, highPriority,
                              requestNamespace, sourceVersionToken);
        return;
    }
    if (highPriority && sourceReadsEnabled(_imageCacheMode)) {
        // A single background cache-info runner may be validating thousands
        // of paths and cannot be preempted mid-run. Watcher-added foreground
        // files are new by definition, so read their metadata from source
        // immediately instead of waiting behind that cache batch.
        onInfoNotFoundInCache(paths, isFromEmbeddedView,
                              directOpenGeneration, true, requestNamespace,
                              sourceVersionToken);
        return;
    }
    if (cacheReadsEnabled(_imageCacheMode)) {
        CachedImageInfoRunner *runner = new CachedImageInfoRunner(
            paths, isFromEmbeddedView, sourceReadsEnabled(_imageCacheMode),
            directOpenGeneration, highPriority, requestNamespace,
            sourceVersionToken);
        runner->connections.append(
            connect(runner, &CachedImageInfoRunner::cachedImageInfoRetrieved,
                    this, &DecodeManager::onCachedImageInfoRetrieved)
            );
        if (runner->isHighPriority()) {
            insertAheadOfLowerPriority(_taskQueue, runner);
        }
        else {
            // Preserve the old cache-info-first policy inside the background
            // tier without allowing it to jump ahead of viewer/visible work.
            _taskQueue.insert(firstBackgroundTask(_taskQueue), runner);
        }
        processQueue();
    }
    else if (sourceReadsEnabled(_imageCacheMode)) {
        onInfoNotFoundInCache(paths, isFromEmbeddedView,
                              directOpenGeneration, highPriority,
                              requestNamespace, sourceVersionToken);
    }
}

void DecodeManager::onInfoNotFoundInCache(
    const QList<QString> &imagePaths, bool isFromEmbeddedView,
    int directOpenGeneration, bool highPriority,
    const QString &requestNamespace, qint64 sourceVersionToken) {
    int insertIndex = _taskQueue.size();
    // Info requests from visible subviews should be first
    if (!highPriority && isFromEmbeddedView) {
        for (insertIndex = firstBackgroundTask(_taskQueue);
             insertIndex < _taskQueue.size(); ++insertIndex) {
            if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead && !isRunnerImageInfoEmbedded(_taskQueue.at(insertIndex))) {
                break;
            }
        }
    }

    for (int i = 0; i < imagePaths.size(); i++) {
        ImageInfoReadRunner *runner = new ImageInfoReadRunner(imagePaths[i], i == imagePaths.size() - 1,
                                                              isFromEmbeddedView, false,
                                                              directOpenGeneration,
                                                              highPriority,
                                                              requestNamespace,
                                                              sourceVersionToken);
        runner->connections.append(
            connect(runner, &ImageInfoReadRunner::imageInfoReady,
                    this, &DecodeManager::onImageInfoReady)
        );
        if (runner->isHighPriority()) {
            insertAheadOfLowerPriority(_taskQueue, runner);
        }
        else {
            _taskQueue.insert(insertIndex, runner);
            ++insertIndex;
        }
    }
    processQueue();
}

void DecodeManager::readVersionedImagesInfo(
    const QList<VersionedImageInfoRequest> &requests,
    bool isFromEmbeddedView, bool highPriority,
    const QString &requestNamespace) {
    if (requests.isEmpty()) {
        return;
    }

    int insertIndex = _taskQueue.size();
    if (!highPriority && isFromEmbeddedView) {
        for (insertIndex = firstBackgroundTask(_taskQueue);
             insertIndex < _taskQueue.size(); ++insertIndex) {
            if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead &&
                !isRunnerImageInfoEmbedded(_taskQueue.at(insertIndex))) {
                break;
            }
        }
    }

    for (int index = 0; index < requests.size(); ++index) {
        const VersionedImageInfoRequest &request = requests.at(index);
        auto *runner = new ImageInfoReadRunner(
            request.path, index == requests.size() - 1,
            isFromEmbeddedView, false, 0, highPriority,
            requestNamespace, request.sourceVersionToken);
        runner->connections.append(
            connect(runner, &ImageInfoReadRunner::imageInfoReady,
                    this, &DecodeManager::onImageInfoReady));
        if (runner->isHighPriority()) {
            insertAheadOfLowerPriority(_taskQueue, runner);
        }
        else {
            _taskQueue.insert(insertIndex, runner);
            ++insertIndex;
        }
    }
    processQueue();
}

void DecodeManager::decodeImages(const QList<ImageDecodeRequest> &requests) {
    QList<ImageDecodeRequest> queuedRequests = requests;
    for (ImageDecodeRequest &request : queuedRequests) {
        // Keep the owner on both halves of the request. Read/decode/cache
        // lookup runners use the outer field, while a later cache-store stage
        // only receives ImageInfo.
        if (request.requestNamespace.isEmpty()) {
            request.requestNamespace = request.info.requestNamespace;
        }
        else {
            request.info.requestNamespace = request.requestNamespace;
        }
    }
    bool hasNewViewerBatch = false;
    for (const ImageDecodeRequest &request : std::as_const(queuedRequests)) {
        if (request.viewerRequest && request.viewerGeneration == 0) {
            hasNewViewerBatch = true;
            break;
        }
    }
    const quint64 newViewerGeneration =
        hasNewViewerBatch ? ++_nextViewerGeneration : 0;
    if (hasNewViewerBatch) {
        int viewerOrdinal = 0;
        for (ImageDecodeRequest &request : queuedRequests) {
            if (request.viewerRequest && request.viewerGeneration == 0) {
                request.viewerGeneration = newViewerGeneration;
                request.viewerPriorityOrdinal = viewerOrdinal++;
            }
        }
    }
    for (ImageDecodeRequest &request : queuedRequests) {
        if (request.viewerRequest && request.viewerPriorityOrdinal < 0) {
            request.viewerPriorityOrdinal = 0;
        }
    }

    // Only iterating images that are known to have cache
    /**/{
        // qDebug() << "ZZ DECODE" << requests.size() << (requests.size() ? requests.first().info.path : "");
        if (cacheReadsEnabled(_imageCacheMode)) {
            for (const auto &request : queuedRequests) {
                if (request.checkCache || _imageCacheMode == CacheUsageMode::OnlyCache) {
                    // qDebug() << "ZZ TO RUN CACHE LOOKUP";
                    CachedImageRetrieveRunner *runner = new CachedImageRetrieveRunner(
                        request, sourceReadsEnabled(_imageCacheMode));
                    runner->connections.append(
                        connect(runner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved,
                                this, &DecodeManager::onImageReady)
                        );
                    if (runner->isViewerRequest() || runner->isHighPriority()) {
                        insertAheadOfLowerPriority(_taskQueue, runner);
                    }
                    else {
                        // Background thumbnail cache lookups retain their old
                        // ordering, but only inside the background tier.
                        int insertIndex = firstBackgroundTask(_taskQueue);
                        for (; insertIndex < _taskQueue.size(); ++insertIndex) {
                            if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead ||
                                _taskQueue.at(insertIndex)->type() == RunnerType::ImageRead ||
                                _taskQueue.at(insertIndex)->type() == RunnerType::ImageDecode) {
                                break;
                            }
                        }
                        _taskQueue.insert(insertIndex, runner);
                    }
                }
            }
        }
    }/**/

    if (!sourceReadsEnabled(_imageCacheMode)) {
        processQueue();
        return;
    }

    for (const auto &request : queuedRequests) {
        if (request.viewerRequest) {
            // qDebug() << "ZZ read" << request.info.path << request.targetSize << _taskQueue.size();
        }
        ImageReadRunner *runner = new ImageReadRunner(request);
        runner->connections.append(
            connect(runner, &ImageReadRunner::imageReadReady,
                    this, &DecodeManager::onImageReadReady)
        );
        runner->connections.append(
            connect(runner, &ImageReadRunner::imageReadFailed,
                    this, &DecodeManager::onImageReadFailed)
        );
        if (runner->isViewerRequest() || runner->isHighPriority()) {
            insertAheadOfLowerPriority(_taskQueue, runner);
        }
        else {
            // Background thumbnail reads retain their old position before
            // metadata reads, without crossing either priority tier.
            int insertIndex = firstBackgroundTask(_taskQueue);
            for (; insertIndex < _taskQueue.size(); ++insertIndex) {
                if (_taskQueue.at(insertIndex)->type() == RunnerType::ImageInfoRead) {
                    break;
                }
            }
            _taskQueue.insert(insertIndex, runner);
        }
    }
    /**/
    processQueue();
}

void DecodeManager::readFolderList(const QStringList &paths, int totalImages,
                                   quint64 requestGeneration,
                                   const QString &requestNamespace) {
    QList<FolderInfo> results;
    QStringList notFound;
    if (cacheReadsEnabled(_fileListCacheMode)) {
        PersistentFolderCache::retrieveFolders(paths, results, notFound);
    }
    else if (sourceReadsEnabled(_fileListCacheMode)) {
        notFound = paths;
    }
    for (FolderInfo &result : results) {
        emit folderListReady(result.path,
                             previewImages(result.subfiles, totalImages),
                             true, requestGeneration);
    }

    if (sourceReadsEnabled(_fileListCacheMode)) {
        for (const QString &path : notFound) {
            FolderListReadRunner *runner = new FolderListReadRunner(
                path, totalImages, cacheWritesEnabled(_fileListCacheMode),
                requestGeneration, requestNamespace);
            runner->connections.append(
                connect(runner, &FolderListReadRunner::folderListReady,
                        this, &DecodeManager::onFolderListReady)
                );
            runner->connections.append(
                connect(runner, &FolderListReadRunner::folderListFailed,
                        this, &DecodeManager::onFolderListFailed)
                );
            _taskQueue.enqueue(runner);
        }
    }
    processQueue();
}

void DecodeManager::scan(const QString &root,
                         const QString &requestNamespace) {
    if (!sourceReadsEnabled(_imageCacheMode) || !sourceReadsEnabled(_fileListCacheMode)) {
        return;
    }
    RecursiveFolderScanner *runner = new RecursiveFolderScanner(
        root, requestNamespace);
    runner->connections.append(
        connect(runner, &RecursiveFolderScanner::scanImages,
                this, [this, requestNamespace](const QStringList &paths) {
                    scanImages(paths, requestNamespace);
                })
        );
    _taskQueue.enqueue(runner);
    processQueue();
}

void DecodeManager::scanImages(const QList<QString> &imagePaths,
                               const QString &requestNamespace) {
    for (int i = 0; i < imagePaths.size(); i++) {
        ImageInfoReadRunner *runner = new ImageInfoReadRunner(
            imagePaths[i], false, false, true, 0, false,
            requestNamespace);
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
                    emit viewerRunnerCanceled(runner->path(),
                                              runner->requestNamespace());
                }
            }
        }
    }

    for (int i = 0; i < _taskQueue.size(); i++) {
        if (isRunnerDecode(_taskQueue.at(i))) {
            Runner *runner = _taskQueue.takeAt(i);
            if (runner->isViewerRequest()) {
                emit viewerRunnerCanceled(runner->path(),
                                          runner->requestNamespace());
            }
            runner->cancel();
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
                emit viewerRunnerCanceled(runner->path(),
                                          runner->requestNamespace());
            }
        }
    }
    while (!_taskQueue.isEmpty()) {
        Runner *runner = _taskQueue.dequeue();
        if (runner->isViewerRequest()) {
            emit viewerRunnerCanceled(runner->path(),
                                      runner->requestNamespace());
        }
        runner->cancel();
        runner->deleteLater();
    }
}

void DecodeManager::cancelAllDecodeViewerRunners() {
    qDebug() << __FUNCTION__;
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            if (isRunnerDecodeViewer(runner)) {
                runner->cancel();

                if (runner->isViewerRequest()) {
                    emit viewerRunnerCanceled(runner->path(),
                                              runner->requestNamespace());
                }
            }
        }
    }

    for (int i = 0; i < _taskQueue.size(); i++) {
        if (isRunnerDecodeViewer(_taskQueue.at(i))) {
            Runner *runner = _taskQueue.takeAt(i);
            if (runner->isViewerRequest()) {
                emit viewerRunnerCanceled(runner->path(),
                                          runner->requestNamespace());
            }
            runner->cancel();
            runner->deleteLater();
            i--;
        }
    }
}

void DecodeManager::cancelThumbnailRequests(
    const QString &requestNamespace) {
    cancelDecodeRequests(requestNamespace, false);
}

void DecodeManager::cancelViewerRequests(
    const QString &requestNamespace) {
    cancelDecodeRequests(requestNamespace, true);
}

void DecodeManager::cancelDecodeRequests(
    const QString &requestNamespace, bool viewerRequests) {
    if (requestNamespace.isEmpty()) {
        return;
    }

    const auto matches = [&requestNamespace, viewerRequests](Runner *runner) {
        return isRunnerDecode(runner) &&
               runner->requestNamespace() == requestNamespace &&
               runner->isViewerRequest() == viewerRequests;
    };

    for (WorkerInfo &worker : _workers) {
        Runner *runner = worker.runner;
        if (!runner || !matches(runner)) {
            continue;
        }
        runner->cancel();
        if (runner->isViewerRequest()) {
            emit viewerRunnerCanceled(runner->path(),
                                      runner->requestNamespace());
        }
    }

    for (int index = _taskQueue.size() - 1; index >= 0; --index) {
        Runner *runner = _taskQueue.at(index);
        if (!matches(runner)) {
            continue;
        }
        _taskQueue.removeAt(index);
        if (runner->isViewerRequest()) {
            emit viewerRunnerCanceled(runner->path(),
                                      runner->requestNamespace());
        }
        runner->cancel();
        runner->deleteLater();
    }

    processQueue();
}

void DecodeManager::cancelRequests(const QString &requestNamespace) {
    if (requestNamespace.isEmpty()) {
        return;
    }

    for (WorkerInfo &worker : _workers) {
        Runner *runner = worker.runner;
        if (runner && runner->requestNamespace() == requestNamespace) {
            runner->cancel();
            if (runner->isViewerRequest()) {
                emit viewerRunnerCanceled(runner->path(),
                                          runner->requestNamespace());
            }
        }
    }

    for (int index = _taskQueue.size() - 1; index >= 0; --index) {
        Runner *runner = _taskQueue.at(index);
        if (runner->requestNamespace() != requestNamespace) {
            continue;
        }
        _taskQueue.removeAt(index);
        if (runner->isViewerRequest()) {
            emit viewerRunnerCanceled(runner->path(),
                                      runner->requestNamespace());
        }
        runner->cancel();
        runner->deleteLater();
    }
}

void DecodeManager::prepareToClose() {
    qInfo() << "[Shutdown] DecodeManager::prepareToClose begin"
            << "alreadyClosing" << _isClosing
            << "workers" << _workers.size()
            << "queuedTasks" << _taskQueue.size();
    if (_isClosing) {
        qInfo() << "[Shutdown] DecodeManager::prepareToClose already closing";
        return;
    }
    _isClosing = true;
    _runningTasksUpdateTimer.stop();

    int runningWorkers = 0;
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            runningWorkers++;
            runner->cancel();
        }
    }
    qInfo() << "[Shutdown] DecodeManager::prepareToClose canceled running workers"
            << runningWorkers;

    int queuedTasks = 0;
    while (!_taskQueue.isEmpty()) {
        Runner *runner = _taskQueue.dequeue();
        runner->cancel();
        delete runner;
        queuedTasks++;
    }
    qInfo() << "[Shutdown] DecodeManager::prepareToClose deleted queued tasks"
            << queuedTasks;

    for (int i = 0; i < _workers.size(); i++) {
        _workers[i].thread->quit();
    }
    qInfo() << "[Shutdown] DecodeManager::prepareToClose requested worker thread quit";

    for (int i = 0; i < _workers.size(); i++) {
        qInfo() << "[Shutdown] DecodeManager::prepareToClose waiting for worker"
                << i
                << "hasRunner" << (_workers[i].runner != nullptr);
        if (!_workers[i].thread->wait(QDeadlineTimer(10s))) {
            qWarning() << "Decode worker did not stop in time; forcing shutdown" << i;
            _workers[i].thread->terminate();
            _workers[i].thread->wait();
            qInfo() << "[Shutdown] DecodeManager::prepareToClose terminated worker" << i;
        }
        else {
            qInfo() << "[Shutdown] DecodeManager::prepareToClose worker stopped" << i;
        }
    }

    if (_imageCacheNeedsDump || _fileListCacheNeedsDump) {
        qInfo() << "[Shutdown] DecodeManager::prepareToClose dumping persistent caches";
        if (_fileListCacheNeedsDump) {
            PersistentFolderCache::dumpDb();
        }
        if (_imageCacheNeedsDump) {
            PersistentImageCache::dumpDb();
        }
        qInfo() << "[Shutdown] DecodeManager::prepareToClose persistent caches dumped";
    }
    qInfo() << "[Shutdown] DecodeManager::prepareToClose end";
}

bool DecodeManager::runningTasksDebug() const {
    return _runningTasksDebug;
}

void DecodeManager::setRunningTasksDebug(bool isRunningTasksDebug) {
    _runningTasksDebug = isRunningTasksDebug;
}

CacheUsageMode DecodeManager::imageCacheMode() const {
    return _imageCacheMode;
}

void DecodeManager::setImageCacheMode(CacheUsageMode mode) {
    _imageCacheMode = mode;
    _imageCacheNeedsDump = _imageCacheNeedsDump || cacheWritesEnabled(mode);
}

CacheUsageMode DecodeManager::fileListCacheMode() const {
    return _fileListCacheMode;
}

void DecodeManager::setFileListCacheMode(CacheUsageMode mode) {
    _fileListCacheMode = mode;
    _fileListCacheNeedsDump = _fileListCacheNeedsDump || cacheWritesEnabled(mode);
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
    if (_isClosing) {
        return;
    }
    if (!_timer.isValid()) {
        _timer.start();
        // qDebug() << "ZZ TIMER START";
    }
    if (_taskQueue.isEmpty()) {
        // qDebug() << "ZZ FINISHED:" << _timer.elapsed() << "ms";
    }
    // The read worker is the only producer for ImageDecodeRunner payloads.
    // Capture its allowance once per scheduling pass instead of rescanning a
    // potentially large request queue for every candidate read.
    const int compressedPayloadCount = compressedPayloadRunnerCount();
    const qint64 compressedPayloadByteCount = compressedPayloadBytes();
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
            bool blockedEarlierImageRead = false;
            for (int taskIndex = 0; taskIndex < _taskQueue.size(); taskIndex++) {
                Runner *candidate = _taskQueue.at(taskIndex);
                if (!isRunnerTypeMatchesThreadType(candidate, workerIndex)) {
                    continue;
                }
                if (candidate->type() == RunnerType::ImageRead) {
                    if (blockedEarlierImageRead ||
                        !canStartRunner(candidate, compressedPayloadCount,
                                        compressedPayloadByteCount)) {
                        blockedEarlierImageRead = true;
                        continue;
                    }
                }
                runner = _taskQueue.takeAt(taskIndex);
                break;
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
            if (!_isClosing) {
                processQueue();
            }
            break;
        }
    }
}

void DecodeManager::onImageReadReady(const ImageData &result) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    ImageDecodeRunner *runner = new ImageDecodeRunner(result);
    if (cacheWritesEnabled(_imageCacheMode)) {
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
    if (runner->isViewerRequest()) {
        // The read stage belongs ahead of older viewer generations, but its
        // decode must also run before same-generation neighbor prefetch work.
        insertViewerStageAheadOfSameRequest(_taskQueue, runner);
    }
    else if (runner->isHighPriority()) {
        insertAheadOfLowerPriority(_taskQueue, runner);
    }
    else {
        _taskQueue.enqueue(runner);
    }

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

void DecodeManager::onFolderListReady(const QString &path,
                                      const QList<FileInfo> &subfiles,
                                      quint64 requestGeneration) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    emit folderListReady(path, subfiles, false, requestGeneration);
}

void DecodeManager::onImageReadFailed(
    const ImageDecodeRequest &request) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }
    emit imageReadFailed(request);
}

void DecodeManager::onFolderListFailed(const QString &path,
                                       const QString &errorText,
                                       quint64 requestGeneration) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    emit folderListFailed(path, errorText, requestGeneration);
}

void DecodeManager::onScannerInfoReady(const ImageInfo &result) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    ImageDecodeRequest request{
        .info = result,
        .targetSize = CACHE_IMAGE_RESOLUTION,
        .requestNamespace = result.requestNamespace,
        .viewerRequest = false,
        .checkCache = false
    };

    ImageReadRunner *runner = new ImageReadRunner(request);
    runner->connections.append(
        connect(runner, &ImageReadRunner::imageReadReady,
                this, &DecodeManager::onImageReadReady)
        );
    runner->connections.append(
        connect(runner, &ImageReadRunner::imageReadFailed,
                this, &DecodeManager::onImageReadFailed)
        );
    // Scanner work keeps its old front-of-background behavior, but must not
    // leapfrog viewer or interactively visible requests.
    _taskQueue.insert(firstBackgroundTask(_taskQueue), runner);
    processQueue();
}


void DecodeManager::onStoreInCache(const ImageDecodeRequest &request, const QByteArray &imageData) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    if (!cacheWritesEnabled(_imageCacheMode)) {
        return;
    }

    CachedImageStoreRunner *runner = new CachedImageStoreRunner(request.info, imageData);
    _taskQueue.enqueue(runner);
    processQueue();
}

void DecodeManager::onCachedImageInfoRetrieved(
    const QList<ImageInfo> &results, const QStringList &notFound,
    bool isFromEmbeddedView, const QString &lastPath,
    int directOpenGeneration, bool highPriority,
    const QString &requestNamespace, qint64 sourceVersionToken) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    emit imagesInfoReady(results);

    if (sourceReadsEnabled(_imageCacheMode)) {
        onInfoNotFoundInCache(notFound, isFromEmbeddedView,
                              directOpenGeneration, highPriority,
                              requestNamespace, sourceVersionToken);
    }
}

bool DecodeManager::isRunnerTypeMatchesThreadType(Runner *runner, int threadType) {
    return isRunnerInReadThread(runner) == (threadType == (int)SpecialThreads::Read);
}

int DecodeManager::compressedPayloadRunnerLimit() const {
    // Every worker except the dedicated source-read worker can consume an
    // ImageDecodeRunner with the current thread routing policy.
    return qMax(1, _workers.size() - 1);
}

int DecodeManager::compressedPayloadRunnerCount() const {
    int count = 0;
    for (const WorkerInfo &worker : _workers) {
        if (worker.runner &&
            worker.runner->type() == RunnerType::ImageDecode) {
            ++count;
        }
    }
    for (Runner *runner : _taskQueue) {
        if (runner->type() == RunnerType::ImageDecode) {
            ++count;
        }
    }
    return count;
}

qint64 DecodeManager::compressedPayloadBytes() const {
    qint64 bytes = 0;
    const auto addPayload = [&bytes](Runner *runner) {
        if (!runner || runner->type() != RunnerType::ImageDecode) {
            return;
        }
        const auto *decodeRunner = static_cast<ImageDecodeRunner *>(runner);
        const qint64 sourceBytes = decodeRunner->_imageData.data.size();
        const qint64 previewBytes = qMax<qint64>(
            0, decodeRunner->_imageData.previewDataSize);
        const qint64 maxBytes = std::numeric_limits<qint64>::max();
        if (sourceBytes > maxBytes - bytes) {
            bytes = std::numeric_limits<qint64>::max();
            return;
        }
        bytes += sourceBytes;
        if (previewBytes > maxBytes - bytes) {
            bytes = maxBytes;
            return;
        }
        bytes += previewBytes;
    };

    for (const WorkerInfo &worker : _workers) {
        addPayload(worker.runner);
    }
    for (Runner *runner : _taskQueue) {
        addPayload(runner);
    }
    return bytes;
}

int DecodeManager::viewerCompressedPayloadRunnerCount() const {
    int count = 0;
    const auto addViewerPayload = [&count](Runner *runner) {
        if (runner && runner->type() == RunnerType::ImageDecode &&
            runner->isViewerRequest()) {
            ++count;
        }
    };
    for (const WorkerInfo &worker : _workers) {
        addViewerPayload(worker.runner);
    }
    for (Runner *runner : _taskQueue) {
        addViewerPayload(runner);
    }
    return count;
}

qint64 DecodeManager::viewerCompressedPayloadBytes() const {
    qint64 bytes = 0;
    const auto addViewerPayload = [&bytes](Runner *runner) {
        if (!runner || runner->type() != RunnerType::ImageDecode ||
            !runner->isViewerRequest()) {
            return;
        }
        const auto *decodeRunner =
            static_cast<ImageDecodeRunner *>(runner);
        const qint64 sourceBytes = decodeRunner->_imageData.data.size();
        const qint64 previewBytes = qMax<qint64>(
            0, decodeRunner->_imageData.previewDataSize);
        const qint64 maxBytes = std::numeric_limits<qint64>::max();
        if (sourceBytes > maxBytes - bytes) {
            bytes = maxBytes;
            return;
        }
        bytes += sourceBytes;
        if (previewBytes > maxBytes - bytes) {
            bytes = maxBytes;
            return;
        }
        bytes += previewBytes;
    };
    for (const WorkerInfo &worker : _workers) {
        addViewerPayload(worker.runner);
    }
    for (Runner *runner : _taskQueue) {
        addViewerPayload(runner);
    }
    return bytes;
}

bool DecodeManager::canStartRunner(
    Runner *runner, int compressedPayloadCount,
    qint64 compressedPayloadByteCount) const {
    if (!runner || runner->type() != RunnerType::ImageRead) {
        return true;
    }

    const auto fitsAllowance = [](int count, int countLimit,
                                  qint64 bytes, qint64 expectedBytes) {
        if (count >= countLimit) {
            return false;
        }
        if (bytes == 0) {
            // Always admit one source, including an oversized source or one
            // whose metadata could not provide a reliable byte count.
            return true;
        }
        if (expectedBytes < 0 || bytes >= MaxCompressedPayloadBytes) {
            return false;
        }
        return expectedBytes <= MaxCompressedPayloadBytes - bytes;
    };

    const auto *readRunner = static_cast<const ImageReadRunner *>(runner);
    const qint64 expectedBytes = readRunner->_request.info.fileSize;
    if (fitsAllowance(compressedPayloadCount,
                      compressedPayloadRunnerLimit(),
                      compressedPayloadByteCount, expectedBytes)) {
        return true;
    }

    if (!runner->isViewerRequest()) {
        return false;
    }

    // Background thumbnails may already occupy the global allowance.  Give
    // the ordered viewer batch an independently bounded lane for its current,
    // previous, and next frames, matching the latency of the old dedicated
    // viewer scheduler without bringing back unbounded compressed read-ahead.
    const int viewerLimit = qMin(ViewerCompressedPayloadReserve,
                                 compressedPayloadRunnerLimit());
    const int viewerCount = viewerCompressedPayloadRunnerCount();
    const qint64 viewerBytes = viewerCompressedPayloadBytes();
    return fitsAllowance(viewerCount, viewerLimit,
                         viewerBytes, expectedBytes);
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
    _isCanceled.store(true, std::memory_order_release);
    for (auto connection : connections) {
        disconnect(connection);
    }
    connections.clear();
}
