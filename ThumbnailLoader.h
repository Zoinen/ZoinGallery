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

    bool readExifPreview(const QString &path, QSize preferredSize, ThumbnailReadResult &outResult);
    QImage decodeImage(const QByteArray &data, const QString &mimeType);
    QImage loadJpeg(const QString &path, QSize preferredSize = QSize(), ExifOrientation *outOrientation = nullptr, QSize *outFullResolution = nullptr);
    QImage loadImageOther(const QString &path, ExifOrientation *outOrientation = nullptr, QSize *outFullResolution = nullptr);

    QImage createThumbnail(const QImage &image, QSize dimensions);
    QImage unsharpMask(QImage &image);
    QImage rotateAndFlip(const QImage &image, ExifOrientation orientation);

    static bool isExifCompatible(const QString &path);
    static bool isJpeg(const QString &path);
    static bool isRawOrTiff(const QString &path);
    static bool isImageOther(const QString &path);

private:
    QImage loadJpegFromData(const uint8_t *data, uint32_t size);
    ExifOrientation readOrientationFromExif(Exiv2::Image *image);
    QSize readResolutionFromExif(Exiv2::Image *image);

    const int ThumbnailWidthLimit = 1024;
    const int ThumbnailHeightLimit = 1024;

    QString _path;
};

#endif // THUMBNAILLOADER_H
