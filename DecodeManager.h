#ifndef DECODEMANAGER_H
#define DECODEMANAGER_H

#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QTimer>

#include "CacheUsageMode.h"
#include "ImageFile.h"

enum class RunnerType {
    ImageInfoRead,
    ImageRead,
    FolderListRead,
    ImageDecode,
    CachedImageStore,
    CachedImageInfo,
    CachedImageRetrieve,
    RecursiveFolderScanner
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
    virtual bool isHighPriority() const { return false; }
    virtual quint64 viewerGeneration() const { return 0; }
    virtual int viewerPriorityOrdinal() const { return -1; }

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
    ~DecodeManager() override;
    void readImagesInfo(const QList<QString> &paths, bool isFromEmbeddedView,
                        int directOpenGeneration = 0,
                        bool highPriority = false);
    void decodeImages(const QList<ImageDecodeRequest> &requests);
    void readFolderList(const QStringList &paths, int totalImages = -1,
                        quint64 requestGeneration = 0);

    void scan(const QString &root);
    void scanImages(const QList<QString> &imagePaths);

    void cancelAllDecodeRunners();
    void cancelAllRunners();
    void cancelAllDecodeViewerRunners();

    void prepareToClose();

    CacheUsageMode imageCacheMode() const;
    void setImageCacheMode(CacheUsageMode mode);
    CacheUsageMode fileListCacheMode() const;
    void setFileListCacheMode(CacheUsageMode mode);

    bool runningTasksDebug() const;
    void setRunningTasksDebug(bool isRunningTasksDebug);

signals:
    void imageInfoReady(const ImageInfo &result);
    void imagesInfoReady(const QList<ImageInfo> &result);
    void imageReady(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo);
    void imageReadFailed(const ImageDecodeRequest &request);
    void folderListReady(const QString &path, const QList<FileInfo> &subfiles,
                         bool isFromCache, quint64 requestGeneration);
    void folderListFailed(const QString &path, const QString &errorText,
                          quint64 requestGeneration);

    void runningTasksChanged(const QString &runningTasks, const QStringList &tasksInfo);
    void viewerRunnerCanceled(const QString &path);

protected:
    void processQueue();
    void onRunnerFinished(QObject *runner);

private:
    void onImageInfoReady(const ImageInfo &result);
    void onImageReadReady(const ImageData &result);
    void onImageReadFailed(const ImageDecodeRequest &request);
    void onImageReady(const ImageDecodeRequest &request, const QImage &image, const DecodedImageInfo &decodedInfo);
    void onFolderListReady(const QString &path, const QList<FileInfo> &subfiles,
                           quint64 requestGeneration);
    void onFolderListFailed(const QString &path, const QString &errorText,
                            quint64 requestGeneration);
    void onScannerInfoReady(const ImageInfo &result);

    void onStoreInCache(const ImageDecodeRequest &request, const QByteArray &imageData);
    void onCachedImageInfoRetrieved(const QList<ImageInfo> &results,
                                    const QStringList &notFound,
                                    bool isFromEmbeddedView,
                                    const QString &lastPath,
                                    int directOpenGeneration,
                                    bool highPriority);
    void onInfoNotFoundInCache(const QList<QString> &paths,
                               bool isFromEmbeddedView,
                               int directOpenGeneration = 0,
                               bool highPriority = false);
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

    CacheUsageMode _imageCacheMode = CacheUsageMode::On;
    CacheUsageMode _fileListCacheMode = CacheUsageMode::On;
    bool _imageCacheNeedsDump = false;
    bool _fileListCacheNeedsDump = false;
    bool _runningTasksDebug = false;
    bool _isClosing = false;
    quint64 _nextViewerGeneration = 0;
};

#endif // DECODEMANAGER_H
