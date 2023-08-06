#include "QmlResourcesProvider.h"

#include <QPainter>

QmlResourcesProvider::QmlResourcesProvider(const QString &prefix)
    : QQuickImageProvider(QQuickImageProvider::Image),
    _prefix(prefix) {
}

QImage QmlResourcesProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    if (id.startsWith("transparent_grid")) {
        qreal dpr = id.split("%7C")[1].toFloat(); // "|"

        QImage grid(16 * dpr, 16 * dpr, QImage::Format_ARGB32);
        grid.fill(QColor(0xcccccc));
        QPainter p(&grid);
        p.fillRect(QRect(0, 0, 8 * dpr, 8 * dpr), 0xffffff);
        p.fillRect(QRect(8 * dpr, 8 * dpr, 8 * dpr, 8 * dpr), 0xffffff);
        *size = grid.size();
        return grid;
    }
    QImage pix(QString(":/%1/%2").arg(_prefix).arg(id));
    qDebug() << "REQUESTED" << QString(":/%1/%2").arg(_prefix).arg(id) << pix.isNull() << requestedSize << pix.size();
    *size = pix.size();
    pix.setDevicePixelRatio(2);
    return pix;
}
