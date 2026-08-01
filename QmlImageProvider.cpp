#include "QmlImageProvider.h"

#include <QPainter>

#include <utility>

QmlImageProvider::QmlImageProvider(
    const QString &prefix,
    QSharedPointer<ProviderImageStore> providerImageStore)
    : QQuickImageProvider(QQuickImageProvider::Image),
      _prefix(prefix),
      _providerImageStore(std::move(providerImageStore)) {
}

QImage QmlImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    Q_UNUSED(requestedSize)
    QImage image = _providerImageStore->snapshot(id);
    if (!image.isNull()) {
        if (size) {
            *size = image.size();
        }
        return image;
    }

    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    if (size) {
        *size = empty.size();
    }
    return empty;
}
