#include "BatchThumbnailGenerator.h"
#include "ThumbnailLoader.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentMap>

BatchThumbnailGenerator::BatchThumbnailGenerator(QObject *parent)
    : QObject(parent) {
    _iconGeneratorWatcher = nullptr;
}

void BatchThumbnailGenerator::generate(QStringList paths) {
    if (_iconGeneratorWatcher) {
        _iconGeneratorWatcher->cancel();
        delete _iconGeneratorWatcher;
    }

    _paths = paths;
    _iconRequestMap.clear();

    std::function<QImage(const QString &)> generateFunction = [] (const QString &input) {
        ThumbnailLoader loader;
        QImage img = loader.load(input);

        return img;
    };

    _iconGeneratorWatcher = new QFutureWatcher<QImage>();

    connect(_iconGeneratorWatcher, &QFutureWatcher<QImage>::finished, _iconGeneratorWatcher, [this] () {
        delete _iconGeneratorWatcher;
        _iconGeneratorWatcher = nullptr;
    });

    connect(_iconGeneratorWatcher, &QFutureWatcher<QImage>::resultReadyAt,
            this, &BatchThumbnailGenerator::onResultReadyAt);

    QFuture<QImage> future;
    future = QtConcurrent::mapped(paths, generateFunction);
    _iconGeneratorWatcher->setFuture(future);
}

void BatchThumbnailGenerator::onResultReadyAt(int index) {
    const QImage &image = _iconGeneratorWatcher->future().resultAt(index);
    emit thumbnailReady(_paths.at(index), image);
}
