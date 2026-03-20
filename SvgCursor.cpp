#include "SvgCursor.h"

#include <QtSvg/QSvgRenderer>
#include <QPainter>
#include <QGuiApplication>
#include <QQuickWindow>

bool SvgCursor::_cursorOverridden = false;

void SvgCursor::setOverrideCursor(const QString &path, qreal dp, qreal rotation) {
    if (!path.isEmpty()) {
        QSvgRenderer renderer(path);
        QSize destSize(renderer.defaultSize() * dp);

        QPixmap pix(destSize);
        pix.fill(Qt::transparent);

        QPainter painter(&pix);
        if (rotation) {
            QTransform t;
            t.translate(pix.width() / 2, pix.height() / 2);
            t.rotate(rotation);
            t.translate(-pix.width() / 2, -pix.height() / 2);
            painter.setTransform(t);
        }
        renderer.render(&painter);
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
