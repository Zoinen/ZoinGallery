#include "DecodeManager.h"
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
