#include "ThumbnailLoader.h"
#include "Decoders/Exiv2MetadataReader.h"
#include "Decoders/QtMetadataReader.h"
#include "Decoders/TiffDecoder.h"
#include "Decoders/JpegDecoder.h"
#include "Decoders/QtDecoder.h"

#include <QDebug>
#include <QImage>
#include <QFile>
#include <QString>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMimeDatabase>

#include <cassert>
#include <string>

#include <memory>

//static const QStringList VectorImageExtensions = {"svg", "wmf", "emf"};
//static const QStringList ImageQtExtensions = {"bmp", "png", "gif", "jp2", "jpc", "tga", "ico", "cur", "ppm", "pgm", "pbm", "svg", "wmf", "emf", "webp", "heic"};

QStringList ThumbnailLoader::ImageQtExtensions;

void ThumbnailLoader::init() {
    QtDecoder().init();
    JpegDecoder().init();
    TiffDecoder().init();

    // Just to fully instantiate it here
    supportedFormats();
}

void ThumbnailLoader::readMetadata(ImageInfo &result) {
    if (!Exiv2MetadataReader().readMetadata(result)) {
        QtMetadataReader().readMetadata(result);
    }
}

bool ThumbnailLoader::readImage(ImageData &result) {
    bool previewLoaded = Exiv2MetadataReader().readPreviewAndMime(result);
    QSize targetSize = result.request.targetSize;
    if (!result.request.checkCache) {
        targetSize = expandToCacheImageResolution(targetSize);
    }
    QSize sizeRotated = rotateToOrientation(result.request.info.imageSize, result.request.info.orientation);
    if (!previewLoaded || sizeRotated.width() < targetSize.width() ||
                          sizeRotated.height() < targetSize.height()) {
        QFile f(result.request.info.path);
        if (!f.open(QFile::ReadOnly)) {
            return false;
        }
        result.data = f.readAll();
        f.close();
    }
    return true;
}

QImage ThumbnailLoader::decode(const ImageData &imageData) {
    QImage image;
    if (!imageData.data.isNull()) {
        QSize targetSize = imageData.request.targetSize;
        if (!imageData.request.checkCache) {
            targetSize = expandToCacheImageResolution(targetSize);
        }
        image = decodeImage(imageData.data, imageData.mimeType, rotateToOrientation(targetSize, imageData.request.info.orientation));

        if (image.isNull() && imageData.previewData) {
            QByteArray previewData = QByteArray::fromRawData(imageData.previewData.get(), imageData.previewDataSize);
            image = decodeImage(previewData, imageData.previewMimeType, rotateToOrientation(targetSize, imageData.request.info.orientation));
            image = rotateAndFlip(image, imageData.request.info.orientation);
        }
        else {
            image = rotateAndFlip(image, imageData.request.info.orientation);
        }
    }
    return image;
}

QImage ThumbnailLoader::decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize) {
    if (TiffDecoder().canDecode(mimeType)) {
        return TiffDecoder().decode(data, targetSize);
    }
    else if (JpegDecoder().canDecode(mimeType)) {
        return JpegDecoder().decode(data, targetSize);
    }
    else {
        return QtDecoder().decode(data, targetSize);
    }
    return QImage();
}

QImage ThumbnailLoader::createThumbnail(const QImage &image, QSize dimensions) {
    if (dimensions.width() < image.width() ||
        dimensions.height() < image.height()) {
        return image.scaled(dimensions, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

QImage ThumbnailLoader::rotateAndFlip(const QImage &image, ExifOrientation orientation) {
    if (orientation && orientation != ExifOrientation::Horizontal) {
        QTransform transform;
        if (orientation == ExifOrientation::Rotate270CW) {
            transform.rotate(270);
        }
        else if (orientation == ExifOrientation::Rotate90CW) {
            transform.rotate(90);
        }
        else if (orientation == ExifOrientation::Rotate180) {
            transform.rotate(180);
        }
        else if (orientation == ExifOrientation::MirrorHorizontal) {
            transform.scale(-1, 1);
        }
        else if (orientation == ExifOrientation::MirrorHorizontalAndRotate270CW) {
            transform.scale(-1, 1);
            transform.rotate(270);
        }
        else if (orientation == ExifOrientation::MirrorHorizontalAndRotate90CW) {
            transform.scale(-1, 1);
            transform.rotate(90);
        }
        else if (orientation == ExifOrientation::MirrorVertical) {
            transform.scale(1, -1);
        }
        return image.transformed(transform);
    }
    return image;
}

QStringList ThumbnailLoader::supportedFormats() {
    static QStringList formats;
    if (formats.isEmpty()) {
        QSet<QString> formatsSet;
        for (const auto &extensions : {QtDecoder().supportedFormats(), JpegDecoder().supportedFormats(), TiffDecoder().supportedFormats()}) {
            for (const QString &extension : extensions) {
                formatsSet.insert(QString("*.%1").arg(extension));
            }
        }
        formats = QList(formatsSet.begin(), formatsSet.end());
    }
    return formats;
}

bool ThumbnailLoader::isFormatSupported(const QString &fileName) {
    for (const QString &format : supportedFormats()) {
        if (fileName.endsWith(format.right(format.length() - 1), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
