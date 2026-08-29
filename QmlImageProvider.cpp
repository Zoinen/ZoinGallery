#include "QmlImageProvider.h"

#include <ZoinGallery/MediaTimingTrace.h>

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
    ZoinGallery::MediaTimingTrace::Span span(
        QStringLiteral("qt.gallery.qml_provider.sync_request"), {
            {QStringLiteral("providerName"), _prefix},
            {QStringLiteral("providerId"), id},
            {QStringLiteral("requestedWidth"), requestedSize.width()},
            {QStringLiteral("requestedHeight"), requestedSize.height()},
        });
    QImage image = _providerImageStore->snapshot(id);
    if (!image.isNull()) {
        span.set(QStringLiteral("outcome"), QStringLiteral("hit"));
        span.set(QStringLiteral("imageWidth"), image.width());
        span.set(QStringLiteral("imageHeight"), image.height());
        span.set(QStringLiteral("imageBytes"),
                 QVariant::fromValue<qlonglong>(image.sizeInBytes()));
        if (size) {
            *size = image.size();
        }
        return image;
    }

    QImage empty(1, 1, QImage::Format_RGBA8888);
    empty.fill(Qt::transparent);
    span.set(QStringLiteral("outcome"), QStringLiteral("missing"));
    span.set(QStringLiteral("imageWidth"), empty.width());
    span.set(QStringLiteral("imageHeight"), empty.height());
    span.set(QStringLiteral("imageBytes"),
             QVariant::fromValue<qlonglong>(empty.sizeInBytes()));
    if (size) {
        *size = empty.size();
    }
    return empty;
}
