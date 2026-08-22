#include "ThumbnailLoader.h"
#include "Decoders/ImageDecoderInterface.h"
#include "DisplayColorSpace.h"

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

void ThumbnailLoader::init() {
    // Just to fully instantiate it here
    supportedFormats();
}

bool ThumbnailLoader::readMetadata(ImageInfo &result) {
    const QFileInfo fileInfo(result.path);
    if (!result.lastModified.isValid()) {
        result.lastModified = fileInfo.lastModified();
    }
    if (result.fileSize < 0) {
        result.fileSize = fileInfo.size();
    }

    bool metadataRead = false;
    for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
        const auto decoder = ImageDecoderFactory::createDecoder(i);
        if (decoder && decoder->readMetadata(result)) {
            metadataRead = true;
            break;
        }
    }
    if (!result.exif.contains("Size")) {
        result.exif["Size"] = fileInfo.size();
    }
    if (!result.exif.contains("DateTime")) {
        result.exif["DateTimeCreated"] = fileInfo.birthTime();
    }
    return metadataRead;
}

bool ThumbnailLoader::readImage(ImageData &result) {
    bool previewLoaded = false;
    for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
        const auto decoder = ImageDecoderFactory::createDecoder(i);
        previewLoaded = decoder && decoder->readPreviewAndMime(result);
        if (previewLoaded) {
            break;
        }
    }
    if (result.mimeType.isEmpty()) {
        result.mimeType = result.request.info.source.mimeType;
    }
    if (result.mimeType.isEmpty()) {
        result.mimeType = QMimeDatabase().mimeTypeForFile(
            result.request.info.formatHint()).name();
        if (isExtensionMatch(result.request.info.formatHint(), {"psd", "psb"})) {
            result.mimeType = "psd";
        } else if (isExtensionMatch(result.request.info.formatHint(), {"dds"})) {
            result.mimeType = "image/vnd.ms-dds";
        } else if (isExtensionMatch(result.request.info.formatHint(), {"webp"})) {
            result.mimeType = "image/webp";
        }
    }

    QSize targetSize = result.request.targetSize;
    if (!result.request.checkCache &&
        result.request.expandToCacheResolution) {
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
    if (!imageData.request.checkCache &&
        imageData.request.expandToCacheResolution) {
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
        const auto decoder = ImageDecoderFactory::createDecoder(i);
        if (!decoder) {
            continue;
        }
        result = decoder->decode(mimeType, data, targetSize);
        if (!result.isNull()) {
            decodedInfo.decoderUsed = decoder->decoderName();
            break;
        }
    }
    decodedInfo.decodingTookTime = t.restart();
    return normalizeToDisplayColorSpace(result);
}

QImage ThumbnailLoader::normalizeToDisplayColorSpace(QImage image) {
    return DisplayColorSpace::convertImage(image);
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
    // Function-local static initialization is serialized by C++. This keeps
    // plugin discovery lazy without allowing two first decode requests to
    // race while publishing the shared extension table.
    static const QStringList formats = [] {
        QSet<QString> formatsSet;
        for (int i = 0; i < ImageDecoderFactory::decoderCount(); i++) {
            const auto decoder = ImageDecoderFactory::createDecoder(i);
            if (!decoder) {
                continue;
            }
            const QStringList supportedFormats = decoder->supportedFormats();
            for (const QString &extension : supportedFormats) {
                formatsSet.insert(QString("*.%1").arg(extension));
            }
        }
        const QStringList result(formatsSet.begin(), formatsSet.end());
        qDebug() << "All supported formats:" << result;
        return result;
    }();
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
