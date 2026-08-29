#include "QtDecoder.h"
#include "ImageFile.h"

#include <QBuffer>
#include <QImageReader>

REGISTER_DECODER_DEFINITION(QtDecoder)

const QStringList &QtDecoder::formats() {
    static const QStringList supported = [] {
        QStringList result;
        for (QByteArray arr : QImageReader::supportedImageFormats()) {
            result.append(QString::fromLatin1(arr));
        }
        qDebug() << "Qt plugin formats:" << result;
        return result;
    }();
    return supported;
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
    if (!isFormatSupported(result.formatHint())) {
        return false;
    }

    QImageReader reader(result.path);
    ExifOrientation orientation = mapQtTransformationToExifOrientation(reader.transformation());
    result.imageSize = rotateToOrientation(reader.size(), orientation);
    return reader.canRead();
}

QStringList QtDecoder::supportedFormats() {
    return formats();
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
