#include "FolderPreviewSelection.h"
#include "DecodeManager.h"
#include "DecodeQueuePolicy.h"

#include "Runners/FolderListReadRunner.h"
#include "Runners/ImageDecodeRunner.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageProbeRunner.h"
#include "Runners/ImageReadRunner.h"
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
#include "Runners/VideoThumbnailRunner.h"
#endif
#include "Runners/CacheImageRunners.h"
#include "Runners/RecursiveFolderScanner.h"

#include "PersistentFolderCache.h"
#include "PersistentDerivedImageCache.h"
#include "PersistentImageCache.h"
#include "NaturalSort.h"
#include "ThumbnailLoader.h"
#include "VectorSource.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QDebug>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

using namespace std::chrono_literals;

namespace {
constexpr int InteractiveSourcePriority = 1000;
constexpr int ViewerFitPriority = 100;
constexpr int ViewerFitWindowSize = 16;
constexpr int ViewerFitForegroundNeighbors = 6;

bool isRankedViewerFit(Runner *runner, int priority) {
    return priority > ViewerFitPriority - ViewerFitWindowSize
        && runner->isViewerRequest() && runner->isViewerFitRequest();
}

bool canUseViewerFitWorker(Runner *runner, int priority) {
    return isRankedViewerFit(runner, priority)
        && (runner->type() == RunnerType::ImageDecode
            || runner->type() == RunnerType::CachedImageRetrieve);
}

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

int foregroundImageStageRank(Runner *runner) {
    switch (runner->type()) {
    case RunnerType::ImageDecode:
        // Compressed bytes are already resident. Decode and publish them before
        // admitting more source I/O, both for latency and payload memory.
        return 0;
    case RunnerType::CachedImageRetrieve:
    case RunnerType::ImageProbe:
        return 1;
    case RunnerType::ImageRead:
        return 2;
    default:
        return 3;
    }
}

void insertHighImageStageAheadOfMetadataImpl(QQueue<Runner *> &queue,
                                             Runner *runner) {
    if (queuePriority(runner) != QueuePriority::High) {
        insertAheadOfLowerPriority(queue, runner);
        return;
    }

    const int stageRank = foregroundImageStageRank(runner);
    int index = 0;
    while (index < queue.size() &&
           queuePriority(queue.at(index)) == QueuePriority::Viewer) {
        ++index;
    }
    while (index < queue.size() &&
           queuePriority(queue.at(index)) == QueuePriority::High &&
           foregroundImageStageRank(queue.at(index)) <= stageRank) {
        ++index;
    }
    queue.insert(index, runner);
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


}

namespace DecodeQueuePolicy {

void insertHighImageStageAheadOfMetadata(QQueue<Runner *> &queue,
                                         Runner *runner) {
    insertHighImageStageAheadOfMetadataImpl(queue, runner);
}

} // namespace DecodeQueuePolicy


bool isRunnerInReadThread(Runner *runner) {
    return runner->type() == RunnerType::ImageProbe ||
        runner->type() == RunnerType::ImageInfoRead ||
        runner->type() == RunnerType::ImageRead;
}

bool isRunnerImageInfoEmbedded(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead && static_cast<ImageInfoReadRunner *>(runner)->isEmbeddedRequest();
}


DecodeManager::DecodeManager(
    QObject *parent, int maxThreads,
    QSharedPointer<ZoinGallery::ImageSourceProvider> imageSourceProvider,
    bool expandViewerFitWorkers)
    : QObject(parent),
      _imageSourceProvider(std::move(imageSourceProvider)) {

    if (!_imageSourceProvider) {
        _imageSourceProvider =
            QSharedPointer<ZoinGallery::LocalImageSourceProvider>::create();
    }

    _runningTasksUpdateTimer.setInterval(100);
    _viewerFitCachePool.setMaxThreadCount(1);
    _viewerFitCachePool.setThreadPriority(QThread::LowPriority);
    connect(&_runningTasksUpdateTimer, &QTimer::timeout,
            this, &DecodeManager::updateRunningTasksCount);

    const int idealThreads = qMax(1, QThread::idealThreadCount());
    const int requestedThreads = maxThreads > 0
        ? qMin(idealThreads, maxThreads) : idealThreads;
    _sharedWorkerCount = qMax(int(SpecialThreads::Last) + 1, requestedThreads);
    const int workerCount = expandViewerFitWorkers
        ? qMax(_sharedWorkerCount, qMin(idealThreads, ViewerFitWindowSize + 1))
        : _sharedWorkerCount;
    // Leave room for the current frame, the first wave of neighbors and UI
    // work before borrowing up to three extra cores for the current resize.
    _foregroundFitResizePool.setMaxThreadCount(expandViewerFitWorkers
        ? qBound(0, idealThreads - ViewerFitForegroundNeighbors - 2, 3) : 0);
    qDebug() << "[FIX:viewer-fit-parallelism]" << _sharedWorkerCount
             << "shared decode workers and" << (workerCount - _sharedWorkerCount)
             << "extra Fit workers, started on demand";
    for (int i = 0; i < workerCount; i++) {
        WorkerInfo info{
            .thread = new QThread(this),
            .runner = nullptr,
        };

        _workers.append(info);
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
                                   const QString &sourceVersionToken,
                                   ImageInfoCachePolicy cachePolicy) {
    if (cachePolicy == ImageInfoCachePolicy::ForceSource) {
        // External catalogs carry a host-authoritative opaque source version.
        // PersistentImageCache is still path/stat based, so it cannot prove
        // that metadata belongs to that exact host revision.
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
    const QString &requestNamespace, const QString &sourceVersionToken) {
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

    QList<ImageInfo> candidates;
    candidates.reserve(requests.size());
    int highPriorityCount = 0;
    for (int index = 0; index < requests.size(); ++index) {
        const VersionedImageInfoRequest &request = requests.at(index);
        ImageInfo info;
        info.path = request.source.isValid()
            ? request.source.runtimeIdentity() : request.path;
        info.fileSize = request.source.isValid()
            ? request.source.size : -1;
        info.source = request.source;
        info.sourceVersionToken = request.sourceVersionToken;
        info.isLast = index == requests.size() - 1;
        info.isFromEmbeddedView = isFromEmbeddedView;
        info.highPriority = highPriority || request.highPriority;
        info.panelThumbnailRequest = request.panelThumbnailRequest;
        highPriorityCount += info.highPriority ? 1 : 0;
        info.requestNamespace = requestNamespace;
        candidates.append(std::move(info));
    }

    QList<ImageInfo> hits;
    QList<ImageInfo> misses;
    if (cacheReadsEnabled(_imageCacheMode)) {
        ZoinGallery::MediaTimingTrace::Span cacheSpan(
            QStringLiteral("qt.gallery.metadata_cache_batch"),
            {{QStringLiteral("requestCount"), candidates.size()},
             {QStringLiteral("requestNamespace"), requestNamespace},
             {QStringLiteral("highPriorityCount"), highPriorityCount}});
        for (ImageInfo candidate : candidates) {
            if (PersistentDerivedImageCache::retrieveMemoryMetadata(candidate)) hits.append(std::move(candidate));
            else misses.append(std::move(candidate));
        }
        cacheSpan.set(QStringLiteral("hitCount"), hits.size());
        cacheSpan.set(QStringLiteral("missCount"), misses.size());
    }
    else {
        misses = candidates;
    }

    if (cacheReadsEnabled(_imageCacheMode) && !misses.isEmpty()) {
        // The tiny RAM lookup above is the only cache work on the UI thread.
        // Cold manifests use the existing bounded cache worker and cancellation.
        for (ImageInfo &hit : hits) hit.isLast = false;
        if (!hits.isEmpty()) emit imagesInfoReady(hits);
        for (ImageInfo &miss : misses) miss.isLast = false;
        misses.last().isLast = true;
        auto *runner = new CachedImageInfoRunner(std::move(misses));
        runner->connections.append(connect(runner, &CachedImageInfoRunner::versionedInfoRetrieved,
            this, [this, runner](const QList<ImageInfo> &cached, const QList<ImageInfo> &absent) {
                if (!runner->isCanceled()) finishVersionedImageInfoLookup(cached, absent);
            }));
        if (runner->isHighPriority()) insertAheadOfLowerPriority(_taskQueue, runner);
        else _taskQueue.insert(firstBackgroundTask(_taskQueue), runner);
        processQueue();
        return;
    }
    finishVersionedImageInfoLookup(std::move(hits), std::move(misses));
}

void DecodeManager::finishVersionedImageInfoLookup(QList<ImageInfo> hits, QList<ImageInfo> misses) {
    // A mixed batch must flush after its slowest tier, not after a cached
    // entry which happened to be last in catalog order. Move the single batch
    // marker to the final miss; an all-hit batch keeps its original marker.
    if (!misses.isEmpty()) {
        for (ImageInfo &hit : hits) {
            hit.isLast = false;
        }
        for (ImageInfo &miss : misses) {
            miss.isLast = false;
        }
        misses.last().isLast = true;
    }

    // One direct signal replaces one queued runner and one queued signal per
    // cached file. ExternalCatalogModel applies this list atomically to its
    // ImageFiles and emits one range update, so Masonry performs one rewrap.
    if (!hits.isEmpty()) {
        emit imagesInfoReady(hits);
    }
    if (sourceReadsEnabled(_imageCacheMode)) {
        QList<ImageInfo> highPriorityMisses;
        QList<ImageInfo> backgroundMisses;
        highPriorityMisses.reserve(misses.size());
        backgroundMisses.reserve(misses.size());
        for (ImageInfo &miss : misses) {
            if (miss.highPriority) {
                highPriorityMisses.append(std::move(miss));
            }
            else {
                backgroundMisses.append(std::move(miss));
            }
        }
        // Preserve the old source-queue semantics after the cache lookup has
        // coalesced visible and background requests into one catalog batch.
        queueVersionedImageInfoSourceReads(highPriorityMisses);
        queueVersionedImageInfoSourceReads(backgroundMisses);
    }
}

void DecodeManager::queueVersionedImageInfoSourceReads(
    const QList<ImageInfo> &requests) {
    if (requests.isEmpty()) {
        return;
    }

    const bool isFromEmbeddedView = requests.first().isFromEmbeddedView;
    const bool highPriority = requests.first().highPriority;
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
        const ImageInfo &request = requests.at(index);
        auto *runner = new ImageInfoReadRunner(
            request.path, request.isLast,
            request.isFromEmbeddedView, false,
            request.directOpenGeneration, request.highPriority,
            request.requestNamespace, request.sourceVersionToken,
            request.source, _imageSourceProvider,
            false,
            cacheWritesEnabled(_imageCacheMode),
            request.panelThumbnailRequest);
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

void DecodeManager::probeImages(
    const QList<ZoinGallery::ImageProbeRequest> &requests) {
    for (const ZoinGallery::ImageProbeRequest &request : requests) {
        if (!request.source.isValid()) {
            continue;
        }
        auto *runner = new ImageProbeRunner(
            request, _imageSourceProvider);
        runner->connections.append(
            connect(runner, &ImageProbeRunner::imageProbeReady,
                    this, &DecodeManager::imageProbeReady));
        if (runner->isHighPriority()) {
            DecodeQueuePolicy::insertHighImageStageAheadOfMetadata(
                _taskQueue, runner);
        }
        else {
            // Provisional probes are the first catalog pass. Keep them ahead
            // of materialization/metadata work within the background band so
            // a slow full-file read cannot starve the remaining tiny probes.
            int insertIndex = firstBackgroundTask(_taskQueue);
            while (insertIndex < _taskQueue.size() &&
                   _taskQueue.at(insertIndex)->type() ==
                       RunnerType::ImageProbe) {
                ++insertIndex;
            }
            _taskQueue.insert(insertIndex, runner);
        }
    }
    processQueue();
}

void DecodeManager::restoreCachedVideoPoster(
    const ImageDecodeRequest &thumbnailRequest) {
    if (!cacheReadsEnabled(_imageCacheMode)) {
        return;
    }
    ImageDecodeRequest request = thumbnailRequest;
    request.targetSize = QSize(1024, 1024);
    request.viewerRequest = false;
    request.backgroundViewerRequest = false;
    request.panelThumbnailRequest = false;
    request.info.panelThumbnailRequest = false;
    request.fitToViewerRequest = false;
    request.expandToCacheResolution = false;
    request.videoPosterRequest = true;
    request.thumbnailTransformKey = QStringLiteral(
        "video-first-frame-poster-1024-display-v5");
    const QString key = request.requestNamespace + QChar(0x1f)
        + request.info.sourceIdentity() + QChar(0x1f)
        + request.info.sourceVersionToken;
    if (_pendingVideoPosterLookups.contains(key)) {
        return;
    }
    _pendingVideoPosterLookups.insert(key);
    // Restore only the disk artifact. This path must not open a video source
    // or initialize Qt Multimedia when the panel contact sheet is a hit.
    auto *runner = new CachedImageRetrieveRunner(request, true);
    runner->connections.append(connect(
        runner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved,
        this, &DecodeManager::onImageReady));
    connect(runner, &QObject::destroyed, this, [this, key]() {
        _pendingVideoPosterLookups.remove(key);
    });
    DecodeQueuePolicy::insertHighImageStageAheadOfMetadata(_taskQueue, runner);
    processQueue();
}

void DecodeManager::restoreCachedViewerThumbnail(
    const ImageDecodeRequest &request) {
    if (!cacheReadsEnabled(_imageCacheMode)) return;
    auto *runner = new CachedImageRetrieveRunner(request, true);
    runner->connections.append(connect(
        runner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved,
        this, &DecodeManager::onImageReady));
    _taskQueue.prepend(runner);
    processQueue();
}

void DecodeManager::setViewportDemand(
    const QString &requestNamespace, const QSet<QString> &visibleSources) {
    if (_visibleSources.contains(requestNamespace)
        && _visibleSources.value(requestNamespace) == visibleSources) return;
    _visibleSources.insert(requestNamespace, visibleSources);
    if (!visibleSources.isEmpty()) preemptSourceReads(requestNamespace);
    ZoinGallery::MediaTimingTrace::event(QStringLiteral("qt.gallery.demand.viewport"), {
        {QStringLiteral("requestNamespace"), requestNamespace},
        {QStringLiteral("visibleSources"), visibleSources.size()},
    });
    processQueue();
}

void DecodeManager::setViewerDemand(const QString &requestNamespace,
                                    const QStringList &orderedSources) {
    QHash<QString, int> ranks;
    for (int index = 0; index < qMin(orderedSources.size(), qsizetype(ViewerFitWindowSize)); ++index) {
        const QString &source = orderedSources[index];
        if (!ranks.contains(source)) ranks.insert(source, index);
    }
    if (_viewerSourceRanks.value(requestNamespace) == ranks) return;
    if (ranks.isEmpty()) _viewerSourceRanks.remove(requestNamespace);
    else _viewerSourceRanks.insert(requestNamespace, ranks);
    cancelObsoleteViewerRequests(requestNamespace, ranks);
    processQueue();
}

void DecodeManager::preemptSourceReads(const QString &requestNamespace,
                                       bool interactiveDemand) {
    for (const WorkerInfo &worker : std::as_const(_workers)) {
        Runner *runner = worker.runner;
        if (!runner || runner->isCanceled()) continue;
        if (interactiveDemand) {
            if (priorityForRunner(runner) >= InteractiveSourcePriority) continue;
        } else if (runner->requestNamespace() != requestNamespace
                   || runner->isViewerRequest()
                   || priorityForRunner(runner) != 0) {
            continue;
        }
        Runner *replacement = nullptr;
        const auto expensive = [](const ZoinGallery::ImageSourceDescriptor &source) {
            return source.isValid()
                && (source.accessProfile != QStringLiteral("directLocal")
                    || (!source.storageClass.isEmpty()
                        && source.storageClass != QStringLiteral("local")));
        };
        if (auto *probe = qobject_cast<ImageProbeRunner *>(runner);
            probe && expensive(probe->_request.source)) {
            auto *next = new ImageProbeRunner(probe->_request, _imageSourceProvider);
            next->connections.append(connect(next, &ImageProbeRunner::imageProbeReady,
                                              this, &DecodeManager::imageProbeReady));
            replacement = next;
        } else if (auto *info = qobject_cast<ImageInfoReadRunner *>(runner);
                   info && expensive(info->_source)) {
            auto *next = new ImageInfoReadRunner(
                info->_path, info->_isLast, info->_isFromEmbeddedView,
                info->_isFromScanner, info->_directOpenGeneration,
                info->_highPriority, info->_requestNamespace, info->_sourceVersionToken,
                info->_source, _imageSourceProvider, info->_readDerivedMetadataCache,
                info->_writeDerivedMetadataCache, info->_panelThumbnailRequest);
            next->connections.append(connect(next, &ImageInfoReadRunner::imageInfoReady,
                                              this, &DecodeManager::onImageInfoReady));
            replacement = next;
        } else if (auto *read = qobject_cast<ImageReadRunner *>(runner);
                   read && expensive(read->_request.info.source)) {
            auto *next = new ImageReadRunner(read->_request, _imageSourceProvider);
            next->connections.append({
                connect(next, &ImageReadRunner::imageReadReady,
                        this, &DecodeManager::onImageReadReady),
                connect(next, &ImageReadRunner::imageReadFailed,
                        this, &DecodeManager::onImageReadFailed),
            });
            replacement = next;
        }
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
        if (!replacement) {
            if (auto *video = qobject_cast<VideoThumbnailRunner *>(runner);
                video && expensive(video->request().info.source)) {
                auto *next = new VideoThumbnailRunner(video->request(), _imageSourceProvider);
                next->connections.append({
                    connect(next, &VideoThumbnailRunner::imageInfoReady,
                            this, &DecodeManager::onImageInfoReady),
                    connect(next, &VideoThumbnailRunner::imageReady,
                            this, &DecodeManager::onImageReady),
                    connect(next, &VideoThumbnailRunner::imageReadFailed,
                            this, &DecodeManager::onImageReadFailed),
                    connect(next, &VideoThumbnailRunner::storeInCache,
                            this, &DecodeManager::onStoreInCache),
                });
                replacement = next;
            }
        }
#endif
        if (!replacement) continue;
        // Keep the catalog/cache owner pending. The canceled attempt publishes
        // no failure, and its replacement resumes at the latest demand priority.
        runner->cancel();
        _taskQueue.append(replacement);
    }
}

void DecodeManager::setInteractiveSource(
    const QString &requestNamespace, const QString &sourceIdentity,
    const QString &resourceId) {
    if (sourceIdentity.isEmpty() && _interactiveNamespace != requestNamespace) return;
    if (_interactiveNamespace == requestNamespace
        && _interactiveSource == sourceIdentity) return;
    _interactiveNamespace = sourceIdentity.isEmpty() ? QString() : requestNamespace;
    _interactiveSource = sourceIdentity;
    for (const WorkerInfo &worker : std::as_const(_workers)) {
        if (worker.runner && worker.runner->type() == RunnerType::ImageDecode) {
            auto *decode = static_cast<ImageDecodeRunner *>(worker.runner);
            if (decode->_resizePool)
                decode->_foregroundFit.store(isInteractiveFitDecode(decode));
        }
    }
    _imageSourceProvider->setInteractiveResource(resourceId);
    // Queue promotion cannot displace a network read already occupying a
    // worker. On each viewer handoff, release lower-priority transport lanes
    // across panels and older viewer requests, retaining their pending owners.
    if (!_interactiveSource.isEmpty()) preemptSourceReads(requestNamespace, true);
    ZoinGallery::MediaTimingTrace::event(QStringLiteral("qt.gallery.demand.interactive"), {
        {QStringLiteral("requestNamespace"), requestNamespace},
        {QStringLiteral("sourceIdentity"), sourceIdentity},
    });
    processQueue();
}

bool DecodeManager::isInteractiveFitDecode(Runner *runner) const {
    return runner && !_interactiveSource.isEmpty()
        && !runner->isCanceled() && runner->type() == RunnerType::ImageDecode
        && runner->isViewerFitRequest()
        && runner->requestNamespace() == _interactiveNamespace
        && runner->path() == _interactiveSource;
}

int DecodeManager::priorityForRunner(Runner *runner) const {
    if (const auto *lookup = qobject_cast<CachedImageInfoRunner *>(runner)) {
        int priority = 0;
        const auto ranks = _viewerSourceRanks.constFind(runner->requestNamespace());
        for (const ImageInfo &candidate : lookup->_versionedCandidates) {
            const QString source = candidate.sourceIdentity();
            if (runner->requestNamespace() == _interactiveNamespace
                && !_interactiveSource.isEmpty() && source == _interactiveSource)
                return InteractiveSourcePriority;
            if (ranks != _viewerSourceRanks.cend()) {
                const auto rank = ranks->constFind(source);
                if (rank != ranks->cend()) priority = qMax(priority, ViewerFitPriority - *rank);
            }
        }
        if (priority > 0) return priority;
    }
    if (!_interactiveSource.isEmpty()
        && runner->requestNamespace() == _interactiveNamespace
        && runner->path() == _interactiveSource) {
        const auto *lookup = qobject_cast<CachedImageRetrieveRunner *>(runner);
        if (lookup && lookup->_request.viewerThumbnailRequest)
            return InteractiveSourcePriority + 2;
        if (runner->isViewerRequest() || runner->isViewerFitRequest())
            return InteractiveSourcePriority + 1;
        return InteractiveSourcePriority;
    }
    const auto viewer = _viewerSourceRanks.constFind(runner->requestNamespace());
    if (viewer != _viewerSourceRanks.cend()
        && ((runner->isViewerRequest() && runner->isViewerFitRequest())
            || runner->type() == RunnerType::ImageProbe
            || runner->type() == RunnerType::ImageInfoRead)) {
        const auto rank = viewer->constFind(runner->path());
        // Live ranks also reorder owners retained from older viewer plans.
        // Fit neighbors precede panel thumbnails and native neighbor prefetch.
        if (rank != viewer->cend()) return ViewerFitPriority - *rank;
    }
    const auto demand = _visibleSources.constFind(runner->requestNamespace());
    if (demand != _visibleSources.cend()
        && (runner->isPanelThumbnailRequest()
            || runner->type() == RunnerType::ImageProbe
            || runner->type() == RunnerType::ImageInfoRead)) {
        return demand->contains(runner->path()) ? 2 : 0;
    }
    if (runner->isViewerRequest()) return 1;
    return runner->isHighPriority() ? 1 : 0;
}

void DecodeManager::decodeImages(const QList<ImageDecodeRequest> &requests) {
    if (_vectorSources.size() > 128) {
        _vectorSources.removeIf([](auto it) { return it.value().isNull(); });
    }
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
                if (!isVectorSource(request.info) && (request.checkCache || _imageCacheMode == CacheUsageMode::OnlyCache)) {
                    // qDebug() << "ZZ TO RUN CACHE LOOKUP";
                    CachedImageRetrieveRunner *runner = new CachedImageRetrieveRunner(
                        request, sourceReadsEnabled(_imageCacheMode));
                    runner->connections.append(
                        connect(runner, &CachedImageRetrieveRunner::cachedThumbnailRetrieved,
                                this, &DecodeManager::onImageReady)
                        );
                    if (runner->isViewerRequest()) {
                        insertAheadOfLowerPriority(_taskQueue, runner);
                    }
                    else if (runner->isHighPriority()) {
                        DecodeQueuePolicy::insertHighImageStageAheadOfMetadata(
                            _taskQueue, runner);
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
        if (isVectorSource(request.info)) {
            const auto source = _vectorSources.value(vectorRevisionKey(request.info)).toStrongRef();
            if (source) {
                QTimer::singleShot(0, this, [this, request, source] {
                    emit vectorSourceReady(request, source);
                });
                continue;
            }
        }
        Runner *runner = nullptr;
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
        if (request.info.thumbnailKind == QStringLiteral("video")) {
            auto *videoRunner = new VideoThumbnailRunner(
                request, _imageSourceProvider);
            videoRunner->connections.append({
                connect(videoRunner, &VideoThumbnailRunner::imageInfoReady,
                        this, &DecodeManager::onImageInfoReady),
                connect(videoRunner, &VideoThumbnailRunner::imageReady,
                        this, &DecodeManager::onImageReady),
                connect(videoRunner, &VideoThumbnailRunner::imageReadFailed,
                        this, &DecodeManager::onImageReadFailed),
                connect(videoRunner, &VideoThumbnailRunner::storeInCache,
                        this, &DecodeManager::onStoreInCache),
            });
            runner = videoRunner;
        } else
#endif
        {
            auto *imageRunner = new ImageReadRunner(
                request, _imageSourceProvider);
            imageRunner->connections.append({
                connect(imageRunner, &ImageReadRunner::imageReadReady,
                        this, &DecodeManager::onImageReadReady),
                connect(imageRunner, &ImageReadRunner::imageReadFailed,
                        this, &DecodeManager::onImageReadFailed),
            });
            runner = imageRunner;
        }
        if (runner->isViewerRequest()) {
            insertAheadOfLowerPriority(_taskQueue, runner);
        }
        else if (runner->isHighPriority()) {
            DecodeQueuePolicy::insertHighImageStageAheadOfMetadata(
                _taskQueue, runner);
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
                             selectFolderPreviewImages(result.subfiles, totalImages),
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
    case RunnerType::ImageProbe:
        return QString("ImageProbe %1")
            .arg(static_cast<ImageProbeRunner *>(task)->_request.source.sourceKey);
    case RunnerType::ImageInfoRead:
        return QString("ImageInfoRead %2%3").arg(static_cast<ImageInfoReadRunner *>(task)->_path).arg(static_cast<ImageInfoReadRunner *>(task)->isEmbeddedRequest() ? " E" : "");
        break;
    case RunnerType::ImageRead:
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
        if (const auto *video = qobject_cast<const VideoThumbnailRunner *>(task)) {
            return QString("VideoThumbnail %1%2")
                .arg(video->request().info.path)
                .arg(video->isViewerRequest() ? " V" : "");
        }
#endif
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
    const auto hasRunningTask = [this]() {
        return std::any_of(
            _workers.cbegin(), _workers.cend(),
            [](const WorkerInfo &worker) { return worker.runner != nullptr; });
    };
    if (_taskQueue.isEmpty() && !hasRunningTask()) {
        updateRunningTasksCount();
        _runningTasksUpdateTimer.stop();
        return;
    }
    if (!_runningTasksUpdateTimer.isActive()) {
        updateRunningTasksCount();
        _runningTasksUpdateTimer.start();
    }
    if (!_timer.isValid()) {
        _timer.start();
        // qDebug() << "ZZ TIMER START";
    }
    if (_taskQueue.isEmpty()) {
        // qDebug() << "ZZ FINISHED:" << _timer.elapsed() << "ms";
    }
    // Snapshot resident compressed payloads before scanning the queue.
    const int compressedPayloadCount = compressedPayloadRunnerCount();
    const qint64 compressedPayloadByteCount = compressedPayloadBytes();
    const bool hasExtraWorkers = _workers.size() > _sharedWorkerCount;
    constexpr int NearFitPriority = ViewerFitPriority - ViewerFitForegroundNeighbors;
    QSet<QString> nearbyFitDecodes;
    const auto recordNearbyFitDecode = [&](Runner *runner, int priority) {
        if (hasExtraWorkers && priority >= NearFitPriority
            && runner->type() == RunnerType::ImageDecode
            && runner->isViewerRequest() && runner->isViewerFitRequest()
            && !runner->isCanceled()) {
            nearbyFitDecodes.insert(runner->requestNamespace());
        }
    };
    bool currentFitHasPayload = false;
    QList<int> priorities;
    QHash<QString, QSet<QString>> cancelingSources;
    for (const WorkerInfo &worker : std::as_const(_workers)) {
        if (worker.runner && worker.runner->isCanceled())
            cancelingSources[worker.runner->requestNamespace()].insert(worker.runner->path());
        if (hasExtraWorkers && worker.runner && worker.runner->type() == RunnerType::ImageDecode
            && worker.runner->isViewerRequest() && worker.runner->isViewerFitRequest()) {
            recordNearbyFitDecode(worker.runner, priorityForRunner(worker.runner));
        }
        currentFitHasPayload |= isInteractiveFitDecode(worker.runner);
    }
    int queuedViewerFitStages = 0;
    priorities.reserve(_taskQueue.size());
    for (Runner *runner : std::as_const(_taskQueue)) {
        const int priority = priorityForRunner(runner);
        priorities.append(priority);
        if (hasExtraWorkers && canUseViewerFitWorker(runner, priority))
            ++queuedViewerFitStages;
        recordNearbyFitDecode(runner, priority);
        currentFitHasPayload |= isInteractiveFitDecode(runner);
    }
    // Offer Fit pixel/cache stages to their own workers first. That leaves the
    // shared read-capable workers free to keep supplying compressed payloads.
    const int firstWorker = queuedViewerFitStages ? _sharedWorkerCount : 0;
    const int workerCount = queuedViewerFitStages ? _workers.size() : _sharedWorkerCount;
    for (int offset = 0; offset < workerCount; ++offset) {
        const int workerIndex = (firstWorker + offset) % _workers.size();
        const bool viewerOnly = workerIndex >= _sharedWorkerCount;
        if (viewerOnly && !queuedViewerFitStages) continue;
        if (!_workers[workerIndex].runner) {
            if (_taskQueue.isEmpty()) {
                updateRunningTasksCount();
                return;
            }
            Runner *runner = nullptr;
            int bestIndex = -1;
            int bestPriority = -1;
            for (int taskIndex = 0; taskIndex < _taskQueue.size(); taskIndex++) {
                Runner *candidate = _taskQueue.at(taskIndex);
                const int priority = priorities.at(taskIndex);
                if (viewerOnly && !canUseViewerFitWorker(candidate, priority))
                    continue;
                // Let the canceled transport release its shared flight before
                // admitting the replacement for the same immutable source.
                const auto canceling = cancelingSources.constFind(candidate->requestNamespace());
                if (canceling != cancelingSources.cend()
                    && canceling->contains(candidate->path())) continue;
                // Let already-read Fit neighbors release their compressed
                // payloads. Otherwise they can fill the viewer reserve and
                // prevent the current image's read from ever starting.
                const bool rankedFit = isRankedViewerFit(candidate, priority);
                // A faster current resize must not release all the distant
                // reads into the CPU pool while nearby frames are still
                // being resized. Already-read pixels may always drain.
                if (rankedFit && priority < NearFitPriority
                    && candidate->type() == RunnerType::ImageRead
                    && nearbyFitDecodes.contains(candidate->requestNamespace())) continue;
                const bool liveFitNeighbor = rankedFit
                    && candidate->requestNamespace() == _interactiveNamespace;
                // Give the next/previous three frames a head start before
                // opening the full pool. Starting the whole cold window
                // together can slow down the current and nearest resizes.
                const bool canReadAhead = currentFitHasPayload &&
                    (!hasExtraWorkers || priority >= NearFitPriority);
                const bool canPrefetch = liveFitNeighbor &&
                    (candidate->type() == RunnerType::ImageDecode
                     || (canReadAhead && candidate->type() == RunnerType::ImageRead));
                if (!_interactiveSource.isEmpty() && priority < InteractiveSourcePriority
                    && !canPrefetch
                    && (candidate->type() == RunnerType::ImageProbe
                        || candidate->type() == RunnerType::ImageInfoRead
                        || candidate->type() == RunnerType::ImageRead
                        || candidate->type() == RunnerType::ImageDecode
                        || candidate->type() == RunnerType::CachedImageStore)) continue;
                if (!isRunnerTypeMatchesThreadType(candidate, workerIndex)) {
                    continue;
                }
                if (candidate->type() == RunnerType::ImageRead) {
                    if (!canStartRunner(candidate, compressedPayloadCount,
                                        compressedPayloadByteCount)) {
                        continue;
                    }
                }
                if (priority > bestPriority) {
                    bestIndex = taskIndex;
                    bestPriority = priority;
                }
            }
            if (bestIndex < 0) {
                continue;
            }
            runner = _taskQueue.takeAt(bestIndex);
            if (hasExtraWorkers && canUseViewerFitWorker(runner, bestPriority))
                --queuedViewerFitStages;
            priorities.removeAt(bestIndex);

            // qDebug() << "RUNNING" << runner->type() << "on thread" << workerIndex;
            if (runner->type() == RunnerType::ImageDecode) {
                auto *decode = static_cast<ImageDecodeRunner *>(runner);
                if (decode->_resizePool)
                    decode->_foregroundFit.store(isInteractiveFitDecode(decode));
            }
            _workers[workerIndex].runner = runner;
            runner->moveToThread(_workers[workerIndex].thread);

            connect(runner, &Runner::finished,
                    this, &DecodeManager::onRunnerFinished);
            // Constructing a runtime must not fan out one operating-system
            // thread per logical CPU before there is decode work. Starting a
            // worker after its first runner has been assigned preserves the
            // configured parallelism while keeping an idle embedded gallery
            // effectively thread-free during application startup.
            if (!_workers[workerIndex].thread->isRunning()) {
                _workers[workerIndex].thread->start();
            }
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

    if (result.vectorSource) {
        _vectorSources.insert(vectorRevisionKey(result.request.info), result.vectorSource);
        emit vectorSourceReady(result.request, result.vectorSource);
        return;
    }
    QThreadPool *resizePool = result.request.fitToViewerRequest
        && _foregroundFitResizePool.maxThreadCount() > 0
        ? &_foregroundFitResizePool : nullptr;
    ImageDecodeRunner *runner = new ImageDecodeRunner(result, resizePool);
    if (cacheWritesEnabled(_imageCacheMode)) {
        runner->connections.append({
            connect(runner, &ImageDecodeRunner::imageReady,
                    this, &DecodeManager::onImageReady),
            connect(runner, &ImageDecodeRunner::storeInCache,
                    this, &DecodeManager::onStoreInCache),
            connect(runner, &ImageDecodeRunner::storeViewerFit,
                    this, &DecodeManager::onStoreViewerFit)
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
        DecodeQueuePolicy::insertHighImageStageAheadOfMetadata(
            _taskQueue, runner);
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

    if (result.vectorSource) {
        _vectorSources.insert(vectorRevisionKey(result), result.vectorSource);
    }
    emit imageInfoReady(result);
    if (result.vectorSource) {
        ImageDecodeRequest request;
        request.info = result;
        request.requestNamespace = result.requestNamespace;
        request.panelThumbnailRequest = result.panelThumbnailRequest;
        request.targetSize = result.imageSize;
        request.viewerRequest = !result.panelThumbnailRequest;
        emit vectorSourceReady(request, result.vectorSource);
    }
}

void DecodeManager::onImageReady(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo) {
    if (qobject_cast<Runner *>(sender())->isCanceled()) {
        return;
    }

    if (!request.info.isFromScanner && !isVectorSource(request.info)) {
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

    ImageReadRunner *runner = new ImageReadRunner(
        request, _imageSourceProvider);
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

void DecodeManager::onStoreViewerFit(const ImageDecodeRequest &request, const QImage &image) {
    if (_isClosing || !cacheWritesEnabled(_imageCacheMode)
        || qobject_cast<Runner *>(sender())->isCanceled() || image.isNull()) return;
    // Lossless encoding may outlast several viewer transitions. Keep it off
    // the decode workers and never queue an unbounded series of decoded frames.
    const auto generation = PersistentDerivedImageCache::writeGeneration();
    _viewerFitCachePool.tryStart([request, image, generation] {
        PersistentDerivedImageCache::storeViewerFitFrame(request, image, generation);
    });
}

void DecodeManager::onCachedImageInfoRetrieved(
    const QList<ImageInfo> &results, const QStringList &notFound,
    bool isFromEmbeddedView, const QString &lastPath,
    int directOpenGeneration, bool highPriority,
    const QString &requestNamespace, const QString &sourceVersionToken) {
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
    if (runner->type() == RunnerType::ImageProbe) {
        // Two bounded range probes per runtime hide network latency without
        // allowing full-file materialization to fan out. Keep the dedicated
        // source-read lane available to current viewport/viewer demand. Its
        // metadata/payload stage must not wait behind offscreen probes after
        // its own probe completes. The probe lanes still execute normal
        // decode/cache work whenever no probe is queued.
        return (priorityForRunner(runner) >= 2
                && threadType == static_cast<int>(SpecialThreads::Read)) ||
            threadType == static_cast<int>(SpecialThreads::Cache) ||
            threadType == static_cast<int>(SpecialThreads::Probe);
    }

    // A virtual/network source can block while opening or materializing one
    // item.  Keep the dedicated local-read lane for the ordinary path, but
    // let external metadata/payload reads use the two bounded I/O lanes as
    // well.  Otherwise one poisoned AFC handle at the head of a catalog
    // serializes every remaining image until its full-file timeout expires.
    if (runner->type() == RunnerType::ImageInfoRead) {
        const auto *info = static_cast<const ImageInfoReadRunner *>(runner);
        if (info->_source.isValid()) {
            const bool expensive = info->_source.accessProfile != QStringLiteral("directLocal")
                || (!info->_source.storageClass.isEmpty()
                    && info->_source.storageClass != QStringLiteral("local"));
            return ((!expensive || priorityForRunner(runner) >= 2)
                    && threadType == static_cast<int>(SpecialThreads::Read)) ||
                threadType == static_cast<int>(SpecialThreads::Cache) ||
                threadType == static_cast<int>(SpecialThreads::Probe);
        }
    }
    if (runner->type() == RunnerType::ImageRead) {
        const ImageDecodeRequest *request = nullptr;
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
        if (const auto *video = qobject_cast<const VideoThumbnailRunner *>(runner)) {
            request = &video->request();
        } else {
            const auto *read = static_cast<const ImageReadRunner *>(runner);
            request = &read->_request;
        }
#else
        const auto *read = static_cast<const ImageReadRunner *>(runner);
        request = &read->_request;
#endif
        if (request->info.source.isValid()) {
            const bool expensive = request->info.source.accessProfile != QStringLiteral("directLocal")
                || (!request->info.source.storageClass.isEmpty()
                    && request->info.source.storageClass != QStringLiteral("local"));
            return ((!expensive || priorityForRunner(runner) >= 2)
                    && threadType == static_cast<int>(SpecialThreads::Read)) ||
                threadType == static_cast<int>(SpecialThreads::Cache) ||
                threadType == static_cast<int>(SpecialThreads::Probe);
        }
    }
    return isRunnerInReadThread(runner) == (threadType == (int)SpecialThreads::Read);
}

int DecodeManager::compressedPayloadRunnerLimit() const {
    return qMax(1, _sharedWorkerCount - 1);
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
    const auto addViewerPayload = [this, &count](Runner *runner) {
        if (runner && runner->type() == RunnerType::ImageDecode &&
            (runner->isViewerRequest() || priorityForRunner(runner) >= InteractiveSourcePriority)) {
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
    const auto addViewerPayload = [this, &bytes](Runner *runner) {
        if (!runner || runner->type() != RunnerType::ImageDecode ||
            (!runner->isViewerRequest() && priorityForRunner(runner) < InteractiveSourcePriority)) {
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

    const ImageDecodeRequest *request = nullptr;
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
    if (const auto *video = qobject_cast<const VideoThumbnailRunner *>(runner)) {
        request = &video->request();
    } else {
        const auto *readRunner = static_cast<const ImageReadRunner *>(runner);
        request = &readRunner->_request;
    }
#else
    const auto *readRunner = static_cast<const ImageReadRunner *>(runner);
    request = &readRunner->_request;
#endif
    const qint64 expectedBytes = request->info.fileSize;
    if (fitsAllowance(compressedPayloadCount,
                      compressedPayloadRunnerLimit(),
                      compressedPayloadByteCount, expectedBytes)) {
        return true;
    }

    if (!runner->isViewerRequest() && priorityForRunner(runner) < InteractiveSourcePriority) {
        return false;
    }

    // Fit has its own count allowance so shared panel work cannot starve the
    // active window. Both allowances still limit resident compressed bytes.
    const bool expandedFit = _workers.size() > _sharedWorkerCount
        && isRankedViewerFit(runner, priorityForRunner(runner));
    const int viewerLimit = expandedFit
        ? qMin(ViewerFitWindowSize, int(_workers.size()) - 1)
        : qMin(ViewerCompressedPayloadReserve, compressedPayloadRunnerLimit());
    const int viewerCount = viewerCompressedPayloadRunnerCount();
    const qint64 viewerBytes = viewerCompressedPayloadBytes();
    return fitsAllowance(viewerCount, viewerLimit,
                         viewerBytes, expectedBytes);
}

QDebug operator<<(QDebug dbg, const RunnerType &myEnum) {
    switch (myEnum) {
    case RunnerType::ImageProbe: dbg.nospace() << "ImageProbe"; break;
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
    if (const auto cancellation = sourceCancellation()) {
        cancellation->cancel();
    }
    // The runner may be waiting in a nested event loop (for example while
    // QMediaPlayer is seeking a video frame).  Notify the runner's own thread
    // after publishing the atomic cancellation state so a queued wake-up can
    // terminate that wait without waiting for a media timeout.
    emit cancelRequested();
    for (auto connection : connections) {
        disconnect(connection);
    }
    connections.clear();
}
