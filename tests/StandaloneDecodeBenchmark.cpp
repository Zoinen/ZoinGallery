#include "CacheUsageMode.h"
#include "DecodeManager.h"
#include "FileListModel.h"
#include "ProviderImageStore.h"
#include "ThumbnailLoader.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <functional>

namespace {

int percentile(QList<int> values, qreal fraction) {
    if (values.isEmpty()) {
        return -1;
    }
    std::sort(values.begin(), values.end());
    const qsizetype index = std::clamp<qsizetype>(
        static_cast<qsizetype>(
            qRound((values.size() - 1) * fraction)),
        0, values.size() - 1);
    return values.at(index);
}

QSize parseSize(const QString &value) {
    const QStringList parts = value.toLower().split(QLatin1Char('x'));
    if (parts.size() != 2) {
        return {};
    }
    bool widthOk = false;
    bool heightOk = false;
    const int width = parts.at(0).toInt(&widthOk);
    const int height = parts.at(1).toInt(&heightOk);
    return widthOk && heightOk && width > 0 && height > 0
        ? QSize(width, height) : QSize();
}

QStringList imagePaths(const QString &directoryPath, int limit,
                       bool recursive) {
    QStringList paths;
    QDirIterator iterator(
        directoryPath, QDir::Files | QDir::Readable,
        recursive ? QDirIterator::Subdirectories
                  : QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (ThumbnailLoader::isFormatSupported(path)) {
            paths.append(QFileInfo(path).canonicalFilePath());
        }
    }
    paths.removeAll(QString());
    paths.sort(Qt::CaseSensitive);
    if (limit > 0 && paths.size() > limit) {
        paths.resize(limit);
    }
    return paths;
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return predicate();
}

QJsonObject runViewerPipeline(const QString &directory,
                              const QStringList &paths,
                              const QSize &viewerSize, int timeoutMs) {
    const auto store = QSharedPointer<ProviderImageStore>::create();
#if defined(ZOIN_BENCH_HAS_WORKER_COUNT)
    // The reusable runtime passes the historical unbounded policy to its
    // standalone local session. Exercise that same construction here; the
    // exact pre-split benchmark intentionally keeps using the legacy
    // FileListModel(store) constructor below.
    FileListModel model(
        store, nullptr, QString(), QStringLiteral("main-"),
        QStringLiteral("zoingallery-thumbnails"),
        QStringLiteral("zoingallery-async"),
        ViewerImageCache::UnboundedByteBudget,
        ViewerImageCache::UnboundedByteBudget);
#else
    FileListModel model(store);
#endif
    model.setImageCacheMode(static_cast<int>(CacheUsageMode::Off));
    model.setFileListCacheMode(static_cast<int>(CacheUsageMode::Off));

    QElapsedTimer openTimer;
    openTimer.start();
    model.cd(directory);
    const qint64 synchronousOpenMs = openTimer.elapsed();

    QList<int> indexes;
    indexes.reserve(paths.size());
    for (const QString &path : paths) {
        const int index = model.fileIndex(QFileInfo(path).fileName());
        if (index >= 0) {
            indexes.append(index);
        }
    }
    const bool metadataReady = waitFor([&] {
        return std::all_of(paths.cbegin(), paths.cend(), [&](const QString &path) {
            return model.imageInfoForPath(path).imageSize.isValid();
        });
    }, timeoutMs);

    const auto sourceAt = [&](int index) {
        return model.bestViewerImageUrlForIndex(index);
    };
    const auto cachedCount = [&] {
        return std::count_if(indexes.cbegin(), indexes.cend(),
                             [&](int index) { return !sourceAt(index).isEmpty(); });
    };

    QJsonObject result{
        {QStringLiteral("synchronousOpenMs"), synchronousOpenMs},
        {QStringLiteral("modelImages"), indexes.size()},
        {QStringLiteral("metadataReady"), metadataReady},
    };
    if (!metadataReady || indexes.size() < 18) {
        result.insert(QStringLiteral("complete"), false);
        model.prepareToClose();
        return result;
    }

    const int centerPosition = indexes.size() / 2;
    const int centerIndex = indexes.at(centerPosition);
    const int nextIndex = indexes.at(centerPosition + 1);
    const int farPosition = qMin(indexes.size() - 1, centerPosition + 24);
    const int farIndex = indexes.at(farPosition);
    const int expectedPrefetch = qMin(16, indexes.size());

    QElapsedTimer prefetchTimer;
    prefetchTimer.start();
    model.requestViewer(centerIndex, viewerSize.width(), viewerSize.height());
    const bool initialPrefetchReady = waitFor(
        [&] { return cachedCount() >= expectedPrefetch; }, timeoutMs);
    const qint64 initialPrefetchMs = prefetchTimer.elapsed();
    const QString centerSource = sourceAt(centerIndex);
    const QString nextSource = sourceAt(nextIndex);
    const int cachedAfterInitial = cachedCount();

    QElapsedTimer nextTimer;
    nextTimer.start();
    model.requestViewer(nextIndex, viewerSize.width(), viewerSize.height());
    const qint64 nextRequestNs = nextTimer.nsecsElapsed();
    const QString nextSourceAfterRequest = sourceAt(nextIndex);

    model.requestViewer(farIndex, viewerSize.width(), viewerSize.height());
    const bool centerRetainedAfterFarRequest =
        sourceAt(centerIndex) == centerSource && !centerSource.isEmpty();
    const int cachedImmediatelyAfterFar = cachedCount();
    const bool farReady = waitFor(
        [&] { return !sourceAt(farIndex).isEmpty(); }, timeoutMs);

    const QString centerBeforeReturn = sourceAt(centerIndex);
    QElapsedTimer returnTimer;
    returnTimer.start();
    model.requestViewer(centerIndex, viewerSize.width(), viewerSize.height());
    const bool centerReadyOnReturn = waitFor(
        [&] { return !sourceAt(centerIndex).isEmpty(); }, timeoutMs);
    const qint64 returnReadyMs = returnTimer.elapsed();

    result.insert(QStringLiteral("complete"),
                  initialPrefetchReady && farReady && centerReadyOnReturn);
    result.insert(QStringLiteral("initialPrefetchMs"), initialPrefetchMs);
    result.insert(QStringLiteral("cachedAfterInitial"), cachedAfterInitial);
    result.insert(QStringLiteral("nextWasPrepared"), !nextSource.isEmpty());
    result.insert(QStringLiteral("nextReusedSynchronously"),
                  !nextSource.isEmpty() &&
                      nextSourceAfterRequest == nextSource);
    result.insert(QStringLiteral("nextRequestNs"), nextRequestNs);
    result.insert(QStringLiteral("cachedImmediatelyAfterFar"),
                  cachedImmediatelyAfterFar);
    result.insert(QStringLiteral("centerRetainedAfterFarRequest"),
                  centerRetainedAfterFarRequest);
    result.insert(QStringLiteral("centerReadyBeforeReturn"),
                  !centerBeforeReturn.isEmpty());
    result.insert(QStringLiteral("returnReadyMs"), returnReadyMs);

    model.prepareToClose();
    return result;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("ZoinGalleryDecodeBenchmark"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Cold source decode benchmark for ZoinGallery"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("directory"),
                                 QStringLiteral("Image directory"));
    const QCommandLineOption limitOption(
        QStringList{QStringLiteral("l"), QStringLiteral("limit")},
        QStringLiteral("Maximum number of images (0 means all)"),
        QStringLiteral("count"), QStringLiteral("0"));
    const QCommandLineOption roundsOption(
        QStringList{QStringLiteral("r"), QStringLiteral("rounds")},
        QStringLiteral("Decode rounds"), QStringLiteral("count"),
        QStringLiteral("3"));
    const QCommandLineOption targetOption(
        QStringList{QStringLiteral("t"), QStringLiteral("target")},
        QStringLiteral("Requested decode size, e.g. 320x240"),
        QStringLiteral("size"), QStringLiteral("320x240"));
    const QCommandLineOption timeoutOption(
        QStringList{QStringLiteral("timeout-ms")},
        QStringLiteral("Per-stage timeout"), QStringLiteral("milliseconds"),
        QStringLiteral("180000"));
    const QCommandLineOption viewerOption(
        QStringList{QStringLiteral("viewer")},
        QStringLiteral("Mark requests as viewer predecode work"));
    const QCommandLineOption highPriorityOption(
        QStringList{QStringLiteral("high-priority")},
        QStringLiteral("Mark requests as visible/high priority"));
    const QCommandLineOption recursiveOption(
        QStringList{QStringLiteral("recursive")},
        QStringLiteral("Scan the directory recursively"));
    const QCommandLineOption viewerPipelineOption(
        QStringList{QStringLiteral("viewer-pipeline")},
        QStringLiteral("Also audit FileListModel viewer predecode/reuse"));
    parser.addOptions({limitOption, roundsOption, targetOption, timeoutOption,
                       viewerOption, highPriorityOption, recursiveOption,
                       viewerPipelineOption});
    parser.process(application);

    if (parser.positionalArguments().size() != 1) {
        parser.showHelp(2);
    }

    bool limitOk = false;
    bool roundsOk = false;
    bool timeoutOk = false;
    const int limit = parser.value(limitOption).toInt(&limitOk);
    const int rounds = parser.value(roundsOption).toInt(&roundsOk);
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    const QSize targetSize = parseSize(parser.value(targetOption));
    if (!limitOk || limit < 0 || !roundsOk || rounds < 1 ||
        !timeoutOk || timeoutMs < 1 || !targetSize.isValid()) {
        QTextStream(stderr) << "Invalid benchmark option\n";
        return 2;
    }

    ThumbnailLoader::init();
    const QString directory = QFileInfo(
        parser.positionalArguments().first()).canonicalFilePath();
    const QStringList paths = imagePaths(
        directory, limit, parser.isSet(recursiveOption));
    if (directory.isEmpty() || paths.isEmpty()) {
        QTextStream(stderr) << "No supported images in directory\n";
        return 3;
    }

    DecodeManager manager;
    manager.setImageCacheMode(CacheUsageMode::Off);
    manager.setFileListCacheMode(CacheUsageMode::Off);

    QHash<QString, ImageInfo> infos;
    QElapsedTimer metadataTimer;
    QEventLoop metadataLoop;
    bool metadataTimedOut = false;
    const QMetaObject::Connection metadataConnection = QObject::connect(
        &manager, &DecodeManager::imageInfoReady, &metadataLoop,
        [&](const ImageInfo &info) {
            infos.insert(info.path, info);
            if (infos.size() == paths.size()) {
                metadataLoop.quit();
            }
        });
    QTimer metadataTimeout;
    metadataTimeout.setSingleShot(true);
    QObject::connect(&metadataTimeout, &QTimer::timeout, &metadataLoop, [&] {
        metadataTimedOut = true;
        metadataLoop.quit();
    });
    metadataTimer.start();
    metadataTimeout.start(timeoutMs);
    manager.readImagesInfo(paths, false);
    if (infos.size() != paths.size()) {
        metadataLoop.exec();
    }
    const qint64 metadataWallMs = metadataTimer.elapsed();
    metadataTimeout.stop();
    QObject::disconnect(metadataConnection);
    if (metadataTimedOut || infos.size() != paths.size()) {
        QTextStream(stderr) << "Metadata stage incomplete: " << infos.size()
                            << '/' << paths.size() << "\n";
        manager.prepareToClose();
        return 4;
    }

    QJsonArray roundResults;
    bool failed = false;
    for (int round = 0; round < rounds; ++round) {
        QList<ImageDecodeRequest> requests;
        requests.reserve(paths.size());
        for (const QString &path : paths) {
            ImageDecodeRequest request;
            request.info = infos.value(path);
            request.targetSize = targetSize;
            request.viewerRequest = parser.isSet(viewerOption);
            request.checkCache = false;
            request.highPriority = parser.isSet(highPriorityOption);
            requests.append(request);
        }

        int completed = 0;
        int readFailures = 0;
        qint64 firstReadyMs = -1;
        QList<int> decoderTimes;
        QElapsedTimer decodeTimer;
        QEventLoop decodeLoop;
        bool decodeTimedOut = false;
        const auto completeOne = [&] {
            ++completed;
            if (completed == requests.size()) {
                decodeLoop.quit();
            }
        };
        const QMetaObject::Connection readyConnection = QObject::connect(
            &manager, &DecodeManager::imageReady, &decodeLoop,
            [&](const ImageDecodeRequest &, const QImage &image,
                const DecodedImageInfo &decodedInfo) {
                if (firstReadyMs < 0) {
                    firstReadyMs = decodeTimer.elapsed();
                }
                if (!image.isNull() && decodedInfo.decodingTookTime >= 0) {
                    decoderTimes.append(decodedInfo.decodingTookTime);
                }
                completeOne();
            });
        const QMetaObject::Connection failureConnection = QObject::connect(
            &manager, &DecodeManager::imageReadFailed, &decodeLoop,
            [&](const ImageDecodeRequest &) {
                ++readFailures;
                completeOne();
            });
        QTimer decodeTimeout;
        decodeTimeout.setSingleShot(true);
        QObject::connect(&decodeTimeout, &QTimer::timeout, &decodeLoop, [&] {
            decodeTimedOut = true;
            decodeLoop.quit();
        });

        decodeTimer.start();
        decodeTimeout.start(timeoutMs);
        manager.decodeImages(requests);
        if (completed != requests.size()) {
            decodeLoop.exec();
        }
        const qint64 wallMs = decodeTimer.elapsed();
        decodeTimeout.stop();
        QObject::disconnect(readyConnection);
        QObject::disconnect(failureConnection);

        QJsonObject result{
            {QStringLiteral("round"), round},
            {QStringLiteral("completed"), completed},
            {QStringLiteral("readFailures"), readFailures},
            {QStringLiteral("wallMs"), wallMs},
            {QStringLiteral("firstReadyMs"), firstReadyMs},
            {QStringLiteral("decoderP50Ms"), percentile(decoderTimes, 0.50)},
            {QStringLiteral("decoderP95Ms"), percentile(decoderTimes, 0.95)},
            {QStringLiteral("decoderMaxMs"), percentile(decoderTimes, 1.0)},
            {QStringLiteral("timedOut"), decodeTimedOut},
        };
        roundResults.append(result);
        if (decodeTimedOut || completed != requests.size() ||
            readFailures != 0 || decoderTimes.size() != requests.size()) {
            failed = true;
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    qint64 sourceBytes = 0;
    for (const QString &path : paths) {
        sourceBytes += QFileInfo(path).size();
    }
    QJsonObject output{
        {QStringLiteral("directory"), directory},
        {QStringLiteral("images"), paths.size()},
        {QStringLiteral("sourceBytes"), sourceBytes},
        {QStringLiteral("targetWidth"), targetSize.width()},
        {QStringLiteral("targetHeight"), targetSize.height()},
        {QStringLiteral("viewerRequests"), parser.isSet(viewerOption)},
        {QStringLiteral("highPriority"), parser.isSet(highPriorityOption)},
        {QStringLiteral("idealThreads"), QThread::idealThreadCount()},
        {QStringLiteral("metadataWallMs"), metadataWallMs},
        {QStringLiteral("rounds"), roundResults},
    };
    if (parser.isSet(viewerPipelineOption)) {
        output.insert(QStringLiteral("viewerPipeline"),
                      runViewerPipeline(directory, paths, targetSize,
                                        timeoutMs));
    }
#if defined(ZOIN_BENCH_HAS_WORKER_COUNT)
    output.insert(QStringLiteral("decodeWorkers"), manager.workerCount());
#endif
    QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Compact)
                        << '\n';
    manager.prepareToClose();
    return failed ? 5 : 0;
}
