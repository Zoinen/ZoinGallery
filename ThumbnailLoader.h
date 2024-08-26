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
    static QImage decode(const ImageData &imageData, DecodedImageInfo &decodedInfo);
    static QImage createThumbnail(const QImage &image, QSize dimensions);

    static QStringList supportedFormats();
    static bool isFormatSupported(const QString &fileName);

private:
    static QStringList ImageQtExtensions;

    static QImage decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize, DecodedImageInfo &decodedInfo);
    static QImage rotateAndFlip(const QImage &image, ExifOrientation orientation);
};

#endif // THUMBNAILLOADER_H
