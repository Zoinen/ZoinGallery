#include "GalleryNativeTextMetrics.h"

#include <QDataStream>
#include <QFontMetricsF>
#include <QImage>
#include <QIODevice>
#include <QPainter>
#include <QtMath>

qreal GalleryNativeTextMetrics::inkCenter(
    const QFont &font, const QString &text, qreal dpr) {
    dpr = qBound<qreal>(0.01, dpr, 8);
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream << font << text << dpr;
    if (const auto *cached = _centers.object(key))
        return *cached;

    const QRectF outline = QFontMetricsF(font).tightBoundingRect(text);
    qreal center = outline.center().y();
    // Allocate only a bounded scratch raster, never a header-sized texture.
    // QML passes the elided title, so normal visible headers stay well below
    // these limits. Keep the analytical fallback for pathological fonts/text.
    const QRect pixels(qFloor(outline.left() * dpr) - 2,
                       qFloor(outline.top() * dpr) - 2,
                       qCeil(outline.width() * dpr) + 5,
                       qCeil(outline.height() * dpr) + 5);
    if (!text.isEmpty() && pixels.width() > 0 && pixels.width() <= 4096
            && pixels.height() > 0 && pixels.height() <= 512) {
        QImage raster(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        raster.setDevicePixelRatio(dpr);
        raster.fill(Qt::transparent);
        {
            QPainter painter(&raster);
            painter.setFont(font);
            painter.setPen(Qt::white);
            // NativeRendering rounds the baseline on its device raster too.
            painter.drawText(-QPointF(pixels.topLeft()) / dpr, text);
        }
        int top = raster.height(), bottom = -1;
        for (int y = 0; y < raster.height(); ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(raster.constScanLine(y));
            for (int x = 0; x < raster.width(); ++x) {
                // Majority coverage excludes faint antialiasing fringes from
                // the optical center, independently of foreground/background.
                if (qAlpha(row[x]) >= 128) {
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        }
        if (bottom >= top)
            center = (pixels.top() + (top + bottom) / 2.0) / dpr;
    }
    _centers.insert(key, new qreal(center));
    return center;
}
