#include "ProviderImageStore.h"

#include <QReadLocker>
#include <QWriteLocker>

QImage ProviderImageStore::snapshot(const QString &imageId) const {
    QReadLocker locker(&_lock);
    return _images.value(imageId);
}

void ProviderImageStore::publish(const QString &imageId,
                                 const QImage &image) {
    if (imageId.isEmpty() || image.isNull()) {
        return;
    }
    QWriteLocker locker(&_lock);
    _images.insert(imageId, image);
}

void ProviderImageStore::remove(const QString &imageId) {
    if (imageId.isEmpty()) {
        return;
    }
    QWriteLocker locker(&_lock);
    _images.remove(imageId);
}

void ProviderImageStore::remove(const QStringList &imageIds) {
    QWriteLocker locker(&_lock);
    for (const QString &imageId : imageIds) {
        _images.remove(imageId);
    }
}
