#ifndef THUMBNAILLOADER_H
#define THUMBNAILLOADER_H

#include <QObject>
#include <QString>
#include <QSize>

#include "ImageFile.h"

namespace Exiv2 {
class Image;
}

class ThumbnailLoader {
public:
    static void init();

    static void readMetadata(ImageInfo &result);
    static bool readImage(ImageData &result);
    static QImage decode(const ImageData &imageData);
    static QImage createThumbnail(const QImage &image, QSize dimensions);

    static QStringList supportedFormats();
    static bool isFormatSupported(const QString &fileName);

private:
    static QStringList ImageQtExtensions;

    static bool readExif(ImageInfo &result);
    static bool readGenericInfo(ImageInfo &result);
    static bool readPreviewAndMime(ImageData &result);
    static QImage decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize);
    static QImage rotateAndFlip(const QImage &image, ExifOrientation orientation);

    static QImage loadJpegFromData(const uint8_t *data, uint32_t size, QSize targetSize);
    static ExifOrientation readOrientationFromExif(Exiv2::Image *image);
    static QVariantMap readExifToMap(Exiv2::Image *image);
    static QSize readResolutionFromExif(Exiv2::Image *image);
    static void fixMimeType(QString &mimeToUpdate, const QString &filePath);

    static bool isExtensionMatch(const QString &path, const QStringList &pattern);

    static bool isJpeg(const QString &path);
    static bool isRawOrTiff(const QString &path);
    static bool isImageOther(const QString &path);
    static bool isVectorImage(const QString &path);
    static bool isExiv2Compatible(const QString &path);
};

#endif // THUMBNAILLOADER_H
