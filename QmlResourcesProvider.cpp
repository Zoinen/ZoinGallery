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
    else if (id.startsWith("top_left_round_corner") ||
             id.startsWith("top_right_round_corner") ||
             id.startsWith("bottom_left_round_corner") ||
             id.startsWith("bottom_right_round_corner")) {
        QStringList args = id.split("%7C");
        qreal dpr = args[1].toFloat(); // "|"
        QColor color(0x1b1b1b);
        qDebug() << args;
        if (args.size() > 2) {
            color = QColor::fromString(args[2]);
        }

        QImage corner(4 * dpr, 4 * dpr, QImage::Format_ARGB32);
        corner.fill(color);
        QPainter p(&corner);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_SourceOut);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        int alotX = corner.width() * 2;
        int alotY = corner.height() * 2;
        p.drawRoundedRect(id.contains("left") ? 0 : -alotX/2, id.contains("top") ? 0 : -alotY/2,
                          alotX, alotY, 4 * dpr, 4 * dpr);
        *size = corner.size();
        return corner;
    }

    QImage pix(QString(":/%1/%2").arg(_prefix).arg(id));
    qDebug() << "REQUESTED" << QString(":/%1/%2").arg(_prefix).arg(id) << pix.isNull() << requestedSize << pix.size();
    *size = pix.size();
    pix.setDevicePixelRatio(2);
    return pix;
}
