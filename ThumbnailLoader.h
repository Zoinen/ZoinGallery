#ifndef THUMBNAILLOADER_H
#define THUMBNAILLOADER_H

#include <QObject>
#include <QString>

namespace Exiv2 {
class Image;
}

class ThumbnailLoader {
public:
    enum ExifOrientation {
        Horizontal = 1,
        MirrorHorizontal = 2,
        Rotate180 = 3,
        MirrorVertical = 4,
        MirrorHorizontalAndRotate270CW = 5,
        Rotate90CW = 6,
        MirrorHorizontalAndRotate90CW = 7,
        Rotate270CW = 8
    };

    static void init();

    QImage loadRawOrTiff(const QString &path, ExifOrientation *outOrientation = nullptr);
    QImage loadJpeg(const QString &path, ExifOrientation *outOrientation = nullptr);
    QImage loadImageOther(const QString &path, ExifOrientation *outOrientation = nullptr);

    QImage createThumbnail(const QImage &image, QSize dimensions, ExifOrientation orientation);
    QImage unsharpMask(QImage &image);
    QImage rotateAndFlip(const QImage &image, ExifOrientation orientation);

    static bool isJpeg(const QString &path);
    static bool isRawOrTiff(const QString &path);
    static bool isImageOther(const QString &path);

private:
    QImage loadFullFromData(const uint8_t *data, uint32_t size);
    ExifOrientation readOrientationFromExif(Exiv2::Image *image);

    const int ThumbnailWidthLimit = 1024;
    const int ThumbnailHeightLimit = 1024;

    QString _path;
};

#endif // THUMBNAILLOADER_H
