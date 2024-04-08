#include "ThumbnailCache.h"

#include <QImage>
#include <QByteArray>
#include <QBuffer>
#include <QDebug>

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent) {
    _cache.setMaxCost(10000);
    _totalSize = 0;
}

void ThumbnailCache::add(const QString &path, const QDateTime &lastModified, const QImage &thumbnail) {
    if (_cache.contains(path)) {
        return;
    }

    // Don't cache png/svg images with transparency. We check corners for better speed
    // if (thumbnail.hasAlphaChannel()) {
    //     if (qAlpha(thumbnail.pixel(0, 0)) != UCHAR_MAX ||
    //         qAlpha(thumbnail.pixel(thumbnail.width() - 1, 0)) != UCHAR_MAX ||
    //         qAlpha(thumbnail.pixel(0, thumbnail.height() - 1)) != UCHAR_MAX ||
    //         qAlpha(thumbnail.pixel(thumbnail.width() - 1, thumbnail.height() - 1)) != UCHAR_MAX) {
    //         return;
    //     }
    // }

    QImage img = thumbnail.scaled(thumbnail.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray *bytes = new QByteArray();
    QBuffer buffer(bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "webp", 10);

    if (_cache.insert(path, bytes)) {
        _totalSize += bytes->size();
    }
}

void ThumbnailCache::requestThumbnail(const QString &path, const QDateTime &lastModified) {
    QByteArray *bytes = _cache.object(path);
    if (bytes) {
        QImage img = QImage::fromData(*bytes, "webp");
        emit cachedThumbnailAvailable(path, img);
    }
}
