#ifndef DECODEMANAGER_H
#define DECODEMANAGER_H

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>

#include "ImageFile.h"

enum class RunnerType {
    ImageInfoRead,
    ImageRead,
    FolderListRead,
    ImageDecode,
    PersistentImageCacheAdd,
    PersistentImageCacheRetrieve,
    PersistentFolderCacheAdd,
    PersistentFolderCacheRetrieve
};
QDebug operator<<(QDebug dbg, const RunnerType &myEnum);

class QThread;

class Runner : public QObject {
    Q_OBJECT

public:
    virtual RunnerType type() = 0;
    virtual void run() {} // =0

    QList<QMetaObject::Connection> connections;

signals:
    void finished(Runner *runner);
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
    void imageReady(const ImageDecodeRequest &request, const QImage &image);
    void folderListReady(const QString &path, int totalImages, const QList<QFileInfo> &result);

protected:
    void processQueue();
    void onRunnerFinished(QObject *runner);

private:
    void onImageReadReady(const ImageData &result);
    bool isRunnerTypeMatchesThreadType(Runner *runner, int threadType);

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
};

#endif // DECODEMANAGER_H
