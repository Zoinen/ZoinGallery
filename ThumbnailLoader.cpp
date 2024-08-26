#include "ThumbnailLoader.h"
#include "Decoders/Exiv2MetadataReader.h"
#include "Decoders/QtMetadataReader.h"
#include "Decoders/TiffDecoder.h"
#include "Decoders/JpegDecoder.h"
#include "Decoders/QtDecoder.h"
#include "Decoders/LibRawMetadataReader.h"
#include "Decoders/TinyEXIFMetadataReader.h"

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
    // if (!Exiv2MetadataReader().readMetadata(result)) {
    if (!TinyEXIFMetadataReader().readMetadata(result)) {
        if (!LibRawMetadataReader().readMetadata((result))) {
            QtMetadataReader().readMetadata(result);
        }
    }
}

bool ThumbnailLoader::readImage(ImageData &result) {
    // TODO: make mime detection better, and probably libraw doing nothing
    bool previewLoaded = false; //Exiv2MetadataReader().readPreviewAndMime(result);
    if (!previewLoaded) {
        previewLoaded = LibRawMetadataReader().readPreviewAndMime(result);
    }
    if (!previewLoaded) {
        previewLoaded = TinyEXIFMetadataReader().readPreviewAndMime(result);
    }
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

QImage ThumbnailLoader::decode(const ImageData &imageData, DecodedImageInfo &decodedInfo) {
    QSize targetSize = imageData.request.targetSize;
    if (!imageData.request.checkCache) {
        targetSize = expandToCacheImageResolution(targetSize);
    }

    QImage image;
    if (!imageData.data.isNull()) {
        image = decodeImage(imageData.data, imageData.mimeType,
                            rotateToOrientation(targetSize, imageData.request.info.orientation), decodedInfo);
    }
    if (image.isNull() && imageData.previewData) {
        QByteArray previewData = QByteArray::fromRawData(imageData.previewData.get(), imageData.previewDataSize);
        image = decodeImage(previewData, imageData.previewMimeType,
                            rotateToOrientation(targetSize, imageData.request.info.orientation), decodedInfo);
        decodedInfo.previewUsed = "Used preview " + imageData.previewUsed;
    }
    if (!image.isNull()) {
        image = rotateAndFlip(image, imageData.request.info.orientation);
    }
    return image;
}

QImage ThumbnailLoader::decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize, DecodedImageInfo &decodedInfo) {
    QImage result;
    QElapsedTimer t;
    t.start();
    qDebug() << "ZZ MIME??" << mimeType;
    if (TiffDecoder().canDecode(mimeType)) {
        result = TiffDecoder().decode(data, targetSize);
        decodedInfo.decoderUsed = "libtiff";
    }
    else if (JpegDecoder().canDecode(mimeType)) {
        result = JpegDecoder().decode(data, targetSize);
        decodedInfo.decoderUsed = "libjpeg-turbo";
        qDebug() << "ZZ DECODED" << result.size() << targetSize;
    }
    else {
        result = QtDecoder().decode(data, targetSize);
        decodedInfo.decoderUsed = "Qt";
    }
    decodedInfo.decodingTookTime = t.restart();
    return result;
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
            transform.rotate(270);
            transform.scale(-1, 1);
        }
        else if (orientation == ExifOrientation::MirrorHorizontalAndRotate90CW) {
            transform.rotate(90);
            transform.scale(-1, 1);
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
        qDebug() << "All supported formats:" << formats;
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
