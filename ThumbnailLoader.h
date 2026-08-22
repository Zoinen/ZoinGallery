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

    static bool readMetadata(ImageInfo &result);
    static bool readImage(ImageData &result);
    static QImage decode(const ImageData &imageData, DecodedImageInfo &decodedInfo);
    static QImage createThumbnail(const QImage &image, QSize dimensions);
    // Embedded first-pass probes decode their tiny payload independently of
    // the full-file decoder but must apply the parent image's Exif transform
    // before publishing the provisional frame.
    static QImage rotateAndFlip(const QImage &image,
                                ExifOrientation orientation);

    static QStringList supportedFormats();
    static bool isFormatSupported(const QString &fileName);

private:
    static QStringList ImageQtExtensions;

    static QImage decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize, DecodedImageInfo &decodedInfo);
    static QImage normalizeToDisplayColorSpace(QImage image);
};

#endif // THUMBNAILLOADER_H
