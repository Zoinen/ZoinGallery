#include "QmlImageProvider.h"

#include <ZoinGallery/MediaTimingTrace.h>

#include <QtMath>

#include <cmath>
#include <utility>

namespace {
constexpr QLatin1StringView FolderMaskSuffix("?folder-rounded=");
constexpr QLatin1StringView FitMaskSuffix("?thumbnail-rounded=");

QImage roundThumbnail(QImage image, QStringView geometry, bool preserveAspect) {
    const auto parts = geometry.split(u'x');
    if (parts.size() != 3)
        return image;
    const QSize slot(parts[0].toInt(), parts[1].toInt());
    const qreal dpr = parts[2].toDouble();
    if (slot.isEmpty() || !std::isfinite(dpr) || dpr <= 0)
        return image;

    const QSizeF bounds = preserveAspect
        ? QSizeF(image.size()).scaled(slot, Qt::KeepAspectRatio) : QSizeF(slot);
    // Keep existing resolution when possible. Tiny originals need enough
    // samples for the mask to survive scaling. Whole multiples preserve the
    // intrinsic aspect ratio of the thumbnails shown with PreserveAspectFit.
    const int fitScale = preserveAspect
        ? qMax(1, qCeil(qMin(128.0, bounds.width()) / image.width())) : 1;
    const QSize raster = preserveAspect ? image.size() * fitScale
        : QSize(qMax(image.width(), qMin(slot.width(), 128)),
                qMax(image.height(), qMin(slot.height(), 128)));
    if (raster != image.size())
        image = image.scaled(raster, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return image;

    // ImageView's radius and antialiasing band in physical pixels. The mask
    // is cached with the raster, so scrolling still draws one ordinary Image.
    constexpr qreal edgeWidth = 0.8;
    const qreal radius = qMin(4.1 * dpr, qMin(bounds.width(), bounds.height()) * 0.5);
    const qreal stepX = bounds.width() / image.width();
    const qreal stepY = bounds.height() / image.height();
    const int columns = qMin((image.width() + 1) / 2, qCeil((radius + edgeWidth) / stepX));
    const int rows = qMin((image.height() + 1) / 2, qCeil((radius + edgeWidth) / stepY));
    for (int y = 0; y < rows; ++y) {
        auto *top = reinterpret_cast<QRgb *>(image.scanLine(y));
        auto *bottom = reinterpret_cast<QRgb *>(image.scanLine(image.height() - 1 - y));
        const qreal py = (y + 0.5) * stepY;
        for (int x = 0; x < columns; ++x) {
            const qreal px = (x + 0.5) * stepX;
            const qreal dx = radius - px;
            const qreal dy = radius - py;
            const qreal distance = std::hypot(qMax(0.0, dx), qMax(0.0, dy))
                + qMin(0.0, qMax(dx, dy)) - radius;
            const qreal edge = qBound(0.0, (distance + edgeWidth) / (2 * edgeWidth), 1.0);
            qreal coverage = 1 - edge * edge * (3 - 2 * edge);
            if (px < 1 || py < 1)
                coverage = qMin(1.0, coverage * 1.1);
            if (coverage == 1)
                continue;
            const auto mask = [coverage](QRgb &pixel) {
                pixel = qRgba(qRound(qRed(pixel) * coverage), qRound(qGreen(pixel) * coverage),
                              qRound(qBlue(pixel) * coverage), qRound(qAlpha(pixel) * coverage));
            };
            const int right = image.width() - 1 - x;
            mask(top[x]);
            if (right != x) mask(top[right]);
            if (bottom != top) {
                mask(bottom[x]);
                if (right != x) mask(bottom[right]);
            }
        }
    }
    return image;
}
} // namespace

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
    auto maskSuffix = FolderMaskSuffix;
    qsizetype maskPos = id.indexOf(maskSuffix);
    if (maskPos < 0) {
        maskSuffix = FitMaskSuffix;
        maskPos = id.indexOf(maskSuffix);
    }
    QImage image = _providerImageStore->snapshot(maskPos < 0 ? id : id.left(maskPos));
    if (!image.isNull()) {
        if (maskPos >= 0)
            image = roundThumbnail(std::move(image),
                QStringView(id).sliced(maskPos + maskSuffix.size()), maskSuffix == FitMaskSuffix);
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
