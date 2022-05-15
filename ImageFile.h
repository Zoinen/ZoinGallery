#ifndef IMAGEFILE_H
#define IMAGEFILE_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMetaType>
#include <QSharedPointer>

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

struct ImageFile {
    QString path;
    QImage image;
    QString imageId;
    QSize fullSize;
    bool isFolder;
    int index;

    ImageFile() : isFolder(false), index(-1) {}
};

struct ThumbnailReadRequest {
    QString sourcePath;
    QSize targetSize;
    bool requested;

    ThumbnailReadRequest() : requested(false) {}
    ThumbnailReadRequest(const QString &path) : sourcePath(path), requested(false) {}
    ThumbnailReadRequest(const QString &path, const QSize &size) : sourcePath(path), targetSize(size), requested(false) {}
};

struct ThumbnailReadResult {
    ThumbnailReadRequest request;
    QSize fullSize;
    ExifOrientation orientation;
    QString mimeType;
    QByteArray data;

    ThumbnailReadResult() : orientation(ExifOrientation::Horizontal) {}
};

Q_DECLARE_METATYPE(ImageFile *)

#endif // IMAGEFILE_H

