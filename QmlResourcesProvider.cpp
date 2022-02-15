#include "QmlResourcesProvider.h"

#include "FileListModel.h"
#include "StandardFileListModel.h"

#include <QPainter>

QmlResourcesProvider::QmlResourcesProvider(const QString &prefix)
    : QQuickImageProvider(QQuickImageProvider::Image),
    _prefix(prefix) {
}

QImage QmlResourcesProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    QImage pix(QString(":/%1/%2").arg(_prefix).arg(id));
    qDebug() << "REQUESTED" << QString(":/%1/%2").arg(_prefix).arg(id) << pix.isNull() << requestedSize << pix.size();
    *size = pix.size();
    pix.setDevicePixelRatio(2);
    return pix;
}
