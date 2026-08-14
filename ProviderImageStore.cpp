#include "ProviderImageStore.h"

#include <QReadLocker>
#include <QWriteLocker>

QImage ProviderImageStore::snapshot(const QString &imageId) const {
    QReadLocker locker(&_lock);
    return _images.value(imageId);
}

bool ProviderImageStore::contains(const QString &imageId) const {
    QReadLocker locker(&_lock);
    return _images.contains(imageId);
}

qsizetype ProviderImageStore::imageCount() const {
    QReadLocker locker(&_lock);
    return _images.size();
}

qint64 ProviderImageStore::retainedBytes() const {
    QReadLocker locker(&_lock);
    return _retainedBytes;
}

void ProviderImageStore::publish(const QString &imageId,
                                 const QImage &image) {
    if (imageId.isEmpty() || image.isNull()) {
        return;
    }
    QWriteLocker locker(&_lock);
    const auto existing = _images.constFind(imageId);
    if (existing != _images.constEnd()) {
        _retainedBytes -= qMax<qint64>(
            0, static_cast<qint64>(existing->sizeInBytes()));
    }
    _images.insert(imageId, image);
    _retainedBytes += qMax<qint64>(
        0, static_cast<qint64>(image.sizeInBytes()));
}

void ProviderImageStore::remove(const QString &imageId) {
    if (imageId.isEmpty()) {
        return;
    }
    QWriteLocker locker(&_lock);
    const auto it = _images.find(imageId);
    if (it == _images.end()) {
        return;
    }
    _retainedBytes -= qMax<qint64>(
        0, static_cast<qint64>(it->sizeInBytes()));
    _images.erase(it);
}

void ProviderImageStore::remove(const QStringList &imageIds) {
    QWriteLocker locker(&_lock);
    for (const QString &imageId : imageIds) {
        const auto it = _images.find(imageId);
        if (it == _images.end()) {
            continue;
        }
        _retainedBytes -= qMax<qint64>(
            0, static_cast<qint64>(it->sizeInBytes()));
        _images.erase(it);
    }
}
