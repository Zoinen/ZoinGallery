#include "SvgCursor.h"

#include <QtSvg/QSvgRenderer>
#include <QColorSpace>
#include <QPainter>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QQuickWindow>

#include <utility>

bool SvgCursor::_cursorOverridden = false;

void SvgCursor::setOverrideCursor(const QString &path, qreal dp, qreal rotation) {
    if (!path.isEmpty()) {
        QSvgRenderer renderer(path);
        QSize destSize(renderer.defaultSize() * dp);

        if (!renderer.isValid() || destSize.isEmpty()) {
            return;
        }

        // Raster QPixmaps created directly on macOS inherit the display's
        // custom ICC profile. Qt 6.11.1 has QTBUG-147602: QImage::toCGImage()
        // releases the corresponding CGColorSpaceRef before CGImageCreate()
        // consumes it. Cocoa converts custom cursor pixmaps through exactly
        // that path, causing a pointer-authentication trap on the next mouse
        // event. Render UI cursors into an untagged image so Qt takes its safe
        // system-sRGB fallback instead of the dangling custom-profile path.
        QImage cursorImage(destSize, QImage::Format_ARGB32_Premultiplied);
        if (cursorImage.isNull()) {
            return;
        }
        cursorImage.fill(Qt::transparent);

        QPainter painter(&cursorImage);
        if (rotation) {
            QTransform t;
            t.translate(cursorImage.width() / 2,
                        cursorImage.height() / 2);
            t.rotate(rotation);
            t.translate(-cursorImage.width() / 2,
                        -cursorImage.height() / 2);
            painter.setTransform(t);
        }
        renderer.render(&painter);
        painter.end();
        cursorImage.setColorSpace(QColorSpace());

        QPixmap pix = QPixmap::fromImage(
            std::move(cursorImage), Qt::NoFormatConversion);
        if (pix.isNull()) {
            return;
        }
        pix.setDevicePixelRatio(dp);

        QCursor cursor(pix);
        if (!_cursorOverridden) {
            _cursorOverridden = true;
            qApp->setOverrideCursor(cursor);
        }
        else {
            qApp->changeOverrideCursor(cursor);
        }
    }
    else {
        _cursorOverridden = false;
        qApp->restoreOverrideCursor();
    }
}
