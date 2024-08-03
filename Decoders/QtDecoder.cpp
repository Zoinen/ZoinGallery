#include "QtDecoder.h"

#include <QBuffer>
#include <QImageReader>

QStringList QtDecoder::_formats;

void QtDecoder::init() {
    if (_formats.isEmpty()) {
        for (QByteArray arr : QImageReader::supportedImageFormats()) {
            _formats.append(QString::fromLatin1(arr));
        }
        qDebug() << "Qt plugin formats:" << _formats;
    }
}

QStringList QtDecoder::supportedFormats() {
    return _formats;
}

bool QtDecoder::canDecode(const QString &mimeType) {
    return true;
}

QImage QtDecoder::decode(const QByteArray &data, QSize targetSize) {
    QBuffer buf(const_cast<QByteArray *>(&data));
    buf.open(QIODevice::ReadOnly);

    QImageReader reader(&buf);
    reader.setScaledSize(targetSize);

    QImage img = reader.read();;
    if (img.isNull()) {
        qDebug() << "Could not decode image";
    }
    return img;
}
