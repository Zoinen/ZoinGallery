#include "DecodeManager.h"
#include "Runners/CacheImageRunners.h"
#include "Runners/ImageDecodeRunner.h"
#include "Runners/ImageReadRunner.h"
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
#include "Runners/VideoThumbnailRunner.h"
#endif
#include <QDebug>

namespace {
bool isRunnerDecode(Runner *runner) {
    return runner->type() == RunnerType::ImageRead || runner->type() == RunnerType::ImageDecode || runner->type() == RunnerType::CachedImageRetrieve;
}

bool isRunnerDecodeViewer(Runner *runner) {
    return runner->isViewerRequest() &&
           (runner->type() == RunnerType::ImageRead ||
            runner->type() == RunnerType::ImageDecode ||
            runner->type() == RunnerType::CachedImageRetrieve);
}
} // namespace

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

void DecodeManager::cancelObsoleteViewerRequests(
    const QString &requestNamespace, const QHash<QString, int> &ranks) {
    QList<ImageDecodeRequest> canceled;
    const auto cancelIfObsolete = [&](Runner *runner) {
        if (!runner || runner->isCanceled()
            || runner->requestNamespace() != requestNamespace) return false;

        const ImageDecodeRequest *request = nullptr;
        if (auto *lookup = qobject_cast<CachedImageRetrieveRunner *>(runner)) {
            if (lookup->_request.viewerThumbnailRequest) {
                if (ranks.value(runner->path(), -1) == 0) return false;
                runner->cancel();
                return true;
            }
            request = &lookup->_request;
        } else if (auto *read = qobject_cast<ImageReadRunner *>(runner)) {
            request = &read->_request;
        } else if (auto *decode = qobject_cast<ImageDecodeRunner *>(runner)) {
            request = &decode->_imageData.request;
        }
        if (!request || !runner->isViewerRequest()
            || ranks.contains(runner->path())) return false;
        canceled.append(*request);
        runner->cancel();
        return true;
    };
    for (const WorkerInfo &worker : std::as_const(_workers)) {
        cancelIfObsolete(worker.runner);
    }
    _taskQueue.removeIf([&](Runner *runner) {
        if (!cancelIfObsolete(runner)) return false;
        runner->deleteLater();
        return true;
    });
    for (const ImageDecodeRequest &request : std::as_const(canceled)) {
        emit viewerDemandRequestCanceled(request);
    }
}

void DecodeManager::cancelThumbnailRequests(
    const QString &requestNamespace) {
    cancelDecodeRequests(requestNamespace, false);
}

void DecodeManager::cancelPanelThumbnailRequests(
    const QString &requestNamespace) {
    if (requestNamespace.isEmpty()) {
        return;
    }
    const auto cancelIfOwned = [&requestNamespace](Runner *runner) {
        return runner
            && runner->requestNamespace() == requestNamespace
            && runner->isPanelThumbnailRequest();
    };
    for (WorkerInfo &worker : _workers) {
        if (cancelIfOwned(worker.runner)) {
            worker.runner->cancel();
        }
    }
    for (int index = _taskQueue.size() - 1; index >= 0; --index) {
        Runner *runner = _taskQueue.at(index);
        if (!cancelIfOwned(runner)) {
            continue;
        }
        _taskQueue.removeAt(index);
        runner->cancel();
        runner->deleteLater();
    }
    processQueue();
}

QList<ImageDecodeRequest> DecodeManager::cancelQueuedPanelThumbnailRequests(
    const QString &requestNamespace) {
    QList<ImageDecodeRequest> canceled;
    if (requestNamespace.isEmpty()) return canceled;

    for (int index = _taskQueue.size() - 1; index >= 0; --index) {
        Runner *runner = _taskQueue.at(index);
        if (runner->requestNamespace() != requestNamespace
            || !runner->isPanelThumbnailRequest()) continue;

        ImageDecodeRequest request;
        if (auto *read = qobject_cast<ImageReadRunner *>(runner)) {
            request = read->_request;
        } else if (auto *decode = qobject_cast<ImageDecodeRunner *>(runner)) {
            request = decode->_imageData.request;
        } else if (auto *cache = qobject_cast<CachedImageRetrieveRunner *>(runner)) {
            request = cache->_request;
#if defined(ZOIN_ENABLE_VIDEO_THUMBNAILS)
        } else if (auto *video = qobject_cast<VideoThumbnailRunner *>(runner)) {
            request = video->request();
#endif
        } else {
            continue;
        }
        _taskQueue.removeAt(index);
        runner->cancel();
        runner->deleteLater();
        canceled.append(request);
    }
    // Do not start the old queue while its caller is replacing the viewport
    // lease. The following demand/decode submission resumes scheduling.
    return canceled;
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
    _visibleSources.remove(requestNamespace);
    _viewerSourceRanks.remove(requestNamespace);
    if (_interactiveNamespace == requestNamespace) {
        _interactiveNamespace.clear();
        _interactiveSource.clear();
        _imageSourceProvider->setInteractiveResource({});
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

void DecodeManager::cancelSourceRequests(
    const QString &requestNamespace,
    const QSet<QString> &sourceIdentities) {
    if (requestNamespace.isEmpty() || sourceIdentities.isEmpty()) {
        return;
    }

    const auto matches = [&requestNamespace, &sourceIdentities](
                             Runner *runner) {
        return runner && runner->requestNamespace() == requestNamespace &&
            sourceIdentities.contains(runner->path());
    };
    for (WorkerInfo &worker : _workers) {
        Runner *runner = worker.runner;
        if (!matches(runner)) {
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
