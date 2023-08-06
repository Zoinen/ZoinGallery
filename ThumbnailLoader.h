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

    void setPath(const QString &path);

    bool readExifPreview(const QString &path, QSize preferredSize, ImageReadResult &outResult);
    bool readGenericPreview(const QString &path, QSize preferredSize, ImageReadResult &outResult);
    QImage decodeImage(const QByteArray &data, const QString &mimeType, QSize targetSize, const ImageReadResult &readResult);

    QImage createThumbnail(const QImage &image, QSize dimensions, bool keepAspect);
    QImage unsharpMask(QImage &image);
    QImage rotateAndFlip(const QImage &image, ExifOrientation orientation);

    static QStringList supportedFormats();

    static bool isJpeg(const QString &path);
    static bool isRawOrTiff(const QString &path);
    static bool isImageOther(const QString &path);
    static bool isVectorImage(const QString &path);
    static bool isExiv2Compatible(const QString &path);

private:
    static QStringList ImageQtExtensions;

    QImage loadJpegFromData(const uint8_t *data, uint32_t size, QSize targetSize);
    ExifOrientation readOrientationFromExif(Exiv2::Image *image);
    QSize readResolutionFromExif(Exiv2::Image *image);

    static bool isExtensionMatch(const QString &path, const QStringList &pattern);

    QString _path;
};

#endif // THUMBNAILLOADER_H
