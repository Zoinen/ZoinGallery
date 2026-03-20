#include "ThumbnailLoader.h"
#include "Decoders/ImageDecoderInterface.h"

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
    // Just to fully instantiate it here
    supportedFormats();
}

void ThumbnailLoader::readMetadata(ImageInfo &result) {
    for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
        if (ImageDecoderFactory::createDecoder(i)->readMetadata(result)) {
            break;
        }
    }
    if (!result.exif.contains("Size")) {
        result.exif["Size"] = QFileInfo(result.path).size();
    }
    if (!result.exif.contains("DateTime")) {
        result.exif["DateTimeCreated"] = QFileInfo(result.path).birthTime();
    }
}

bool ThumbnailLoader::readImage(ImageData &result) {
    bool previewLoaded = false;
    for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
        previewLoaded = ImageDecoderFactory::createDecoder(i)->readPreviewAndMime(result);
        if (previewLoaded) {
            break;
        }
    }
    if (result.mimeType.isEmpty()) {
        result.mimeType = QMimeDatabase().mimeTypeForFile(result.request.info.path).name();
        if (isExtensionMatch(result.request.info.path, {"psd", "psb"})) {
            result.mimeType = "psd";
        } else if (isExtensionMatch(result.request.info.path, {"dds"})) {
            result.mimeType = "image/vnd.ms-dds";
        }
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
        // qDebug() << "ZZ DECODE" << imageData.request.info.path << imageData.mimeType;
        image = decodeImage(imageData.data, imageData.mimeType,
                            rotateToOrientation(targetSize, imageData.request.info.orientation), decodedInfo);
    }
    if (image.isNull() && imageData.previewData) {
        // qDebug() << "ZZ DECODE PREVIEW" << imageData.request.info.path << imageData.previewMimeType;
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
    // qDebug() << "ZZ MIME??" << mimeType;
    for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
        result = ImageDecoderFactory::createDecoder(i)->decode(mimeType, data, targetSize);
        if (!result.isNull()) {
            decodedInfo.decoderUsed = ImageDecoderFactory::createDecoder(i)->decoderName();
            break;
        }
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
        for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
            QStringList supportedFormats = ImageDecoderFactory::createDecoder(i)->supportedFormats();
            for (const QString &extension : supportedFormats) {
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
