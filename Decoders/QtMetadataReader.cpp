#include "QtMetadataReader.h"
#include "ImageFile.h"

#include <QImageReader>

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

bool QtMetadataReader::readMetadata(ImageInfo& result) {
    QImageReader reader(result.path);
    ExifOrientation orientation = mapQtTransformationToExifOrientation(reader.transformation());
    result.imageSize = rotateToOrientation(reader.size(), orientation);
    return reader.canRead();
}

bool QtMetadataReader::readPreviewAndMime(ImageData &result) {
    return false;
}

bool QtMetadataReader::isFormatSupported(const QString &path) {
    return true;
}
