#ifndef DECODEMANAGER_H
#define DECODEMANAGER_H

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QTimer>

#include "ImageFile.h"

enum class RunnerType {
    ImageInfoRead,
    ImageRead,
    FolderListRead,
    ImageDecode,
    CachedImageStore,
    CachedImageInfo,
    CachedImageRetrieve
};
QDebug operator<<(QDebug dbg, const RunnerType &myEnum);

class QThread;

class Runner : public QObject {
    Q_OBJECT

public:
    virtual RunnerType type() = 0;
    virtual void run() {} // =0

    virtual QString path() const { return QString(); }
    virtual bool isViewerRequest() const { return false; }

    void cancel();
    bool isCanceled() const { return _isCanceled; }


    QList<QMetaObject::Connection> connections;

signals:
    void finished(Runner *runner);

private:
    bool _isCanceled = false;
};


class DecodeManager : public QObject {
    Q_OBJECT

public:
    explicit DecodeManager(QObject *parent = nullptr);
    void readImagesInfo(QList<QString> paths, bool isFromEmbeddedView);
    void decodeImages(QList<ImageDecodeRequest> requests);
    void readFolderList(const QStringList &paths, int totalImages = -1);
    void cancelAllDecodeRunners();
    void cancelAllRunners();
    void cancelAllDecodeViewerRunners();

    void prepareToClose();

signals:
    void imageInfoReady(const ImageInfo &result);
    void imagesInfoReady(const QList<ImageInfo> &result);
    void imageReady(const ImageDecodeRequest &request, const QImage &image, bool isFromCache);
    void folderListReady(const QString &path, const QList<FileInfo> &subfiles);

    void runningTasksChanged(QString runningTasks, QStringList tasksInfo);
    void viewerRunnerCanceled(const QString &path);

protected:
    void processQueue();
    void onRunnerFinished(QObject *runner);

private:
    void onImageInfoReady(const ImageInfo &result);
    void onImageReadReady(const ImageData &result);
    void onImageReady(const ImageDecodeRequest &request, const QImage &image, bool isFromCache);
    void onFolderListReady(const QString &path, const QList<FileInfo> &subfiles);

    void onStoreInCache(const ImageDecodeRequest &request, const QImage &image);
    void onCachedImageInfoRetrieved(const QList<ImageInfo> &results, const QStringList &notFound,
                                    bool isFromEmbeddedView, const QString &lastPath);
    void onInfoNotFoundInCache(QList<QString> paths, bool isFromEmbeddedView);
    bool isRunnerTypeMatchesThreadType(Runner *runner, int threadType);
    void updateRunningTasksCount();
    QString runnerToString(Runner *task);

    struct WorkerInfo {
        QThread *thread;
        Runner *runner;
    };

    QQueue<Runner *> _taskQueue;
    QList<WorkerInfo> _workers;

    enum class SpecialThreads {
        Read,
        Cache,
        Last
    };


    QElapsedTimer _timer;
    QTimer _runningTasksUpdateTimer;

    int _runningTasks = 0;
};

#endif // DECODEMANAGER_H
