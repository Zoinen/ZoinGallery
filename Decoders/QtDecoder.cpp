#include "QtDecoder.h"
#include "ImageFile.h"

#include <QBuffer>
#include <QImageReader>

REGISTER_DECODER_DEFINITION(QtDecoder)

QStringList QtDecoder::_formats;

QtDecoder::QtDecoder() {
    if (_formats.isEmpty()) {
        for (QByteArray arr : QImageReader::supportedImageFormats()) {
            _formats.append(QString::fromLatin1(arr));
        }
        qDebug() << "Qt plugin formats:" << _formats;
    }
}

ExifOrientation mapQtTransformationToExifOrientation(QImageIOHandler::Transformations transformation) {
    switch(transformation) {
    case QImageIOHandler::TransformationNone:
        return ExifOrientation::Horizontal;
    case QImageIOHandler::TransformationMirror:
        return ExifOrientation::MirrorHorizontal;
    case QImageIOHandler::TransformationFlip:
        return ExifOrientation::MirrorVertical;
    case QImageIOHandler::TransformationRotate180:
        return ExifOrientation::Rotate180;
    case QImageIOHandler::TransformationRotate90:
        return ExifOrientation::Rotate90CW;
    case QImageIOHandler::TransformationMirrorAndRotate90:
        return ExifOrientation::MirrorHorizontalAndRotate90CW;
    case QImageIOHandler::TransformationFlipAndRotate90:
        return ExifOrientation::MirrorHorizontalAndRotate270CW;
    case QImageIOHandler::TransformationRotate270:
        return ExifOrientation::Rotate270CW;
    default:
        return ExifOrientation::Horizontal;
    }
}

bool QtDecoder::readMetadata(ImageInfo& result) {
    if (!isFormatSupported(result.path)) {
        return false;
    }

    QImageReader reader(result.path);
    ExifOrientation orientation = mapQtTransformationToExifOrientation(reader.transformation());
    result.imageSize = rotateToOrientation(reader.size(), orientation);
    return reader.canRead();
}

QStringList QtDecoder::supportedFormats() {
    return _formats;
}

QImage QtDecoder::decode(const QString& mimeType, const QByteArray &data, QSize targetSize) {
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

