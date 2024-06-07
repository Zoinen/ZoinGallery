#include "DecodeManager.h"

#include "Runners/FolderListReadRunner.h"
#include "Runners/ImageDecodeRunner.h"
#include "Runners/ImageInfoReadRunner.h"
#include "Runners/ImageReadRunner.h"
#include "Runners/PersistentFolderCacheRunners.h"
#include "Runners/PersistentImageCacheRunners.h"

#include <QThread>

#include <chrono>
using namespace std::chrono_literals;


bool isRunnerDecode(Runner *runner) {
    return runner->type() == RunnerType::ImageRead || runner->type() == RunnerType::ImageDecode;
}

bool isRunnerDecodeViewer(Runner *runner) {
    return (runner->type() == RunnerType::ImageRead && static_cast<ImageReadRunner *>(runner)->isViewerRequest()) ||
           (runner->type() == RunnerType::ImageDecode && static_cast<ImageDecodeRunner *>(runner)->isViewerRequest());
}

bool isRunnerInReadThread(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead || runner->type() == RunnerType::ImageRead;
}

bool isRunnerImageInfoEmbedded(Runner *runner) {
    return runner->type() == RunnerType::ImageInfoRead && static_cast<ImageInfoReadRunner *>(runner)->isEmbeddedRequest();
}


DecodeManager::DecodeManager(QObject *parent)
    : QObject(parent) {

    const int MaxThreads = qMax(int(SpecialThreads::Last) + 1, QThread::idealThreadCount());
    qDebug() << "Using" << MaxThreads << "threads";
    for (int i = 0; i < MaxThreads; i++) {
        WorkerInfo info;
        info.thread = new QThread(this);
        info.runner = nullptr;

        _workers.append(info);

        info.thread->start();
        //        info.thread->setPriority(QThread::LowPriority);
    }
}

void DecodeManager::readImagesInfo(QList<QString> imagePaths, bool isFromEmbeddedView) {
    int insertIndex = _taskQueue.size();
    // Info requests from visible subviews should be first
    if (isFromEmbeddedView/* && imagePaths.first().startsWith("P:\\RAED\\2014.06.20")*/) {
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
                    this, &DecodeManager::imageInfoReady)
        );
        _taskQueue.insert(insertIndex, runner);
        insertIndex++;
    }
    processQueue();
}

void DecodeManager::decodeImages(QList<ImageDecodeRequest> requests) {
    int insertIndex = 0;
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
        ImageReadRunner *runner = new ImageReadRunner(request);
        runner->connections.append(
            connect(runner, &ImageReadRunner::imageReadReady,
                    this, &DecodeManager::onImageReadReady)
        );
        _taskQueue.insert(insertIndex, runner);
        insertIndex++;
    }
    processQueue();
}

void DecodeManager::readFolderList(const QStringList &paths, int totalImages) {
    for (const QString &path : paths) {
        FolderListReadRunner *runner = new FolderListReadRunner(path, totalImages);
        runner->connections.append(
            connect(runner, &FolderListReadRunner::folderListReady,
                    this, &DecodeManager::folderListReady)
            );
        _taskQueue.enqueue(runner);
    }
    processQueue();
}

void DecodeManager::cancelAllDecodeRunners() {
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            if (isRunnerDecode(runner)) {
                for (auto connection : runner->connections) {
                    disconnect(connection);
                }
                runner->connections.clear();
            }
        }
    }

    for (int i = 0; i < _taskQueue.size(); i++) {
        if (isRunnerDecode(_taskQueue.at(i))) {
            _taskQueue.remove(i);
            i--;
        }
    }
}

void DecodeManager::cancelAllRunners() {
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            for (auto connection : runner->connections) {
                disconnect(connection);
            }
            runner->connections.clear();
        }
    }
    _taskQueue.clear();
}

void DecodeManager::cancelAllDecodeViewerRunners() {
    for (int i = 0; i < _workers.size(); i++) {
        if (Runner *runner = _workers[i].runner) {
            if (isRunnerDecodeViewer(runner)) {
                for (auto connection : runner->connections) {
                    disconnect(connection);
                }
                runner->connections.clear();
            }
        }
    }

    for (int i = 0; i < _taskQueue.size(); i++) {
        if (isRunnerDecodeViewer(_taskQueue.at(i))) {
            _taskQueue.remove(i);
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
}

void DecodeManager::processQueue() {
    if (!_timer.isValid()) {
        _timer.start();
        qDebug() << "ZZ TIMER START";
    }
    if (_taskQueue.isEmpty()) {
        // qDebug() << "ZZ FINISHED:" << _timer.elapsed() << "ms";
    }
    for (int workerIndex = 0; workerIndex < _workers.size(); workerIndex++) {
        if (!_workers[workerIndex].runner) {
            if (_taskQueue.isEmpty()) {
                return;
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
    ImageDecodeRunner *runner = new ImageDecodeRunner(result);
    runner->connections.append(
        connect(runner, &ImageDecodeRunner::imageReady,
                this, &DecodeManager::imageReady)
    );
    _taskQueue.enqueue(runner);

    processQueue();
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
    case RunnerType::PersistentImageCacheAdd: dbg.nospace() << "PersistentImageCacheAdd"; break;
    case RunnerType::PersistentImageCacheRetrieve: dbg.nospace() << "PersistentImageCacheRetrieve"; break;
    case RunnerType::PersistentFolderCacheAdd: dbg.nospace() << "PersistentFolderCacheAdd"; break;
    case RunnerType::PersistentFolderCacheRetrieve: dbg.nospace() << "PersistentFolderCacheRetrieve"; break;
    default: dbg.nospace() << "Unknown"; break;
    }
    return dbg.space();
}
